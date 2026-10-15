// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "common.h"
#include "lastseen.h"
#include "vanish.h"
#include "client.h"
#include "commands.h"
#include "admin.h"
#include "log.h"
#include "banlist.h"
#include "utils.h"
#include "party.h"
#include "ratelimit.h"
#include "db.h"
#include "crypto_utils.h"
#include "file_transfer.h"
#include "pwdgen.h"

#define PORT 9001
#define MAX_TRIES 5
char pinned_message[BUFFER_SIZE] = "";

client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
time_t server_start_time;

void *vanish_cleaner_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        check_and_expire_vanish_messages();
        sleep(1);
    }
    return NULL;
}

void party_broadcast_system(const char *msg, int except_sock)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i] && clients[i]->sockfd != except_sock)
        {
            send(clients[i]->sockfd, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void add_client(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);
    strcpy(cl->color, "\033[0;37m");

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (!clients[i])
        {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

client_t *get_client_by_name(const char *name)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i] && strcmp(clients[i]->username, name) == 0)
        {
            pthread_mutex_unlock(&clients_mutex);
            return clients[i];
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

void remove_client(int sockfd)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i] && clients[i]->sockfd == sockfd)
        {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static void handle_internal_protocol(const char *buffer, client_t *cli)
{
    if (strncmp(buffer, "__IDKEY__:", 10) == 0)
    {
        char phex[128] = {0};
        if (sscanf(buffer, "__IDKEY__:%127s", phex) == 1)
        {
            db_save_user_key(cli->username, phex);
        }
        return;
    }

    if (strncmp(buffer, "__KEYREQ__:", 11) == 0)
    {
        char target[32] = {0};
        if (sscanf(buffer, "__KEYREQ__:%31s", target) == 1)
        {
            char pubhex[128] = {0};
            char resp[256];
            if (db_get_user_key(target, pubhex, sizeof(pubhex)))
            {
                snprintf(resp, sizeof(resp), "__KEYRESP__:%s:%s\n", target, pubhex);
            }
            else
            {
                snprintf(resp, sizeof(resp), "__KEYRESP__:%s:NOT_FOUND\n", target);
            }
            send(cli->sockfd, resp, strlen(resp), 0);
        }
        return;
    }

    if (strncmp(buffer, "__TYPING__", 10) == 0)
    {
        char typing_pkt[64];
        snprintf(typing_pkt, sizeof(typing_pkt), "__TYPING__:%s\n", cli->username);
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (clients[i] && clients[i]->sockfd != cli->sockfd &&
                strcmp(clients[i]->party_code, cli->party_code) == 0)
            {
                send(clients[i]->sockfd, typing_pkt, strlen(typing_pkt), 0);
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    if (strncmp(buffer, "__E2EE__:", 9) == 0)
    {
        char target_name[32] = {0};
        if (sscanf(buffer, "__E2EE__:%31[^:]:", target_name) == 1)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }

            char forward_pkt[4096];
            const char *rest = buffer + 9 + strlen(target_name) + 1;
            snprintf(forward_pkt, sizeof(forward_pkt), "__E2EE__:%s:%s:%s\n",
                     cli->username, target_name, rest);

            if (target)
            {
                send(target->sockfd, forward_pkt, strlen(forward_pkt), 0);
                pthread_mutex_unlock(&clients_mutex);
            }
            else
            {
                pthread_mutex_unlock(&clients_mutex);
                db_store_offline_msg(cli->username, target_name, forward_pkt);
                char notice[128];
                snprintf(notice, sizeof(notice), "\033[1;33m[E2EE] %s is offline. Encrypted message queued in offline inbox.\033[0m\n", target_name);
                send(cli->sockfd, notice, strlen(notice), 0);
            }
        }
        return;
    }

    if (strncmp(buffer, "__FILEREQ__:", 12) == 0)
    {
        int fid = 0;
        char target_name[32] = {0}, filename[256] = {0}, sha[SHA256_HEX_LEN] = {0};
        size_t fsize = 0;
        if (sscanf(buffer, "__FILEREQ__:%d:%31[^:]:%255[^:]:%zu:%64s",
                   &fid, target_name, filename, &fsize, sha) == 5)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }
            if (target)
            {
                char req_pkt[512];
                snprintf(req_pkt, sizeof(req_pkt), "__FILEREQ__:%d:%s:%s:%zu:%s\n",
                         fid, cli->username, filename, fsize, sha);
                send(target->sockfd, req_pkt, strlen(req_pkt), 0);
            }
            else
            {
                char notfound[128];
                snprintf(notfound, sizeof(notfound), "\033[1;31m[FILE] User '%s' is not online.\033[0m\n", target_name);
                send(cli->sockfd, notfound, strlen(notfound), 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        return;
    }

    if (strncmp(buffer, "__FILEACCEPT__:", 15) == 0)
    {
        int fid = 0;
        char target_name[32] = {0};
        if (sscanf(buffer, "__FILEACCEPT__:%d:%31s", &fid, target_name) == 2)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }
            if (target)
            {
                char acc_pkt[128];
                snprintf(acc_pkt, sizeof(acc_pkt), "__FILEACCEPT__:%d:%s\n", fid, cli->username);
                send(target->sockfd, acc_pkt, strlen(acc_pkt), 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        return;
    }

    if (strncmp(buffer, "__FILEDECLINE__:", 16) == 0)
    {
        int fid = 0;
        char target_name[32] = {0};
        if (sscanf(buffer, "__FILEDECLINE__:%d:%31s", &fid, target_name) == 2)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }
            if (target)
            {
                char dec_pkt[128];
                snprintf(dec_pkt, sizeof(dec_pkt), "__FILEDECLINE__:%d:%s\n", fid, cli->username);
                send(target->sockfd, dec_pkt, strlen(dec_pkt), 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        return;
    }

    if (strncmp(buffer, "__FILECHUNK__:", 14) == 0)
    {
        int fid = 0, cseq = 0, ctot = 0;
        char target_name[32] = {0};
        char b64[FILE_CHUNK_SIZE * 2 + 128] = {0};
        if (sscanf(buffer, "__FILECHUNK__:%d:%31[^:]:%d:%d:%4095s",
                   &fid, target_name, &cseq, &ctot, b64) == 5)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }
            if (target)
            {
                char chunk_pkt[sizeof(b64) + 128];
                snprintf(chunk_pkt, sizeof(chunk_pkt), "__FILECHUNK__:%d:%d:%d:%s\n",
                         fid, cseq, ctot, b64);
                send(target->sockfd, chunk_pkt, strlen(chunk_pkt), 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        return;
    }

    if (strncmp(buffer, "__FILEDONE__:", 13) == 0)
    {
        int fid = 0;
        char target_name[32] = {0};
        if (sscanf(buffer, "__FILEDONE__:%d:%31s", &fid, target_name) == 2)
        {
            pthread_mutex_lock(&clients_mutex);
            client_t *target = NULL;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && strcmp(clients[i]->username, target_name) == 0)
                {
                    target = clients[i];
                    break;
                }
            }
            if (target)
            {
                char done_pkt[64];
                snprintf(done_pkt, sizeof(done_pkt), "__FILEDONE__:%d\n", fid);
                send(target->sockfd, done_pkt, strlen(done_pkt), 0);
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        return;
    }
}

void *handle_client(void *arg)
{
    char buffer[4096];
    char name[32];
    client_t *cli = (client_t *)arg;

    int rlen = recv(cli->sockfd, name, sizeof(name) - 1, 0);
    if (rlen <= 0)
    {
        ratelimit_release_ip_connection(cli->ip);
        close(cli->sockfd);
        free(cli);
        pthread_detach(pthread_self());
        return NULL;
    }
    name[rlen] = '\0';
    trim_newline(name);

    if (strlen(name) == 0)
    {
        send_to_client(cli, "\033[1;31m[SERVER] Invalid username.\033[0m\n");
        ratelimit_release_ip_connection(cli->ip);
        close(cli->sockfd);
        free(cli);
        pthread_detach(pthread_self());
        return NULL;
    }

    strncpy(cli->username, name, sizeof(cli->username) - 1);
    cli->username[sizeof(cli->username) - 1] = '\0';

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i] && strcmp(clients[i]->username, cli->username) == 0)
        {
            pthread_mutex_unlock(&clients_mutex);
            send_to_client(cli, "\033[1;31m[SERVER] Username already taken.\033[0m\n");
            ratelimit_release_ip_connection(cli->ip);
            close(cli->sockfd);
            free(cli);
            pthread_detach(pthread_self());
            return NULL;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (strcmp(cli->username, "admin") == 0)
    {
        char pwd[64];
        int auth_success = 0;
        for (int i = 0; i < MAX_TRIES; i++)
        {
            send(cli->sockfd, "ask\n", 4, 0);
            int plen = recv(cli->sockfd, pwd, sizeof(pwd) - 1, 0);
            if (plen <= 0)
            {
                break;
            }
            pwd[plen] = '\0';
            trim_newline(pwd);

            if (!db_verify_admin_password(pwd))
            {
                if (i == MAX_TRIES - 1)
                {
                    send_to_client(cli, "password wrong disconnecting...\n");
                    break;
                }
                else
                {
                    send(cli->sockfd, "try\n", 4, 0);
                }
            }
            else
            {
                cli->is_admin = 1;
                auth_success = 1;
                send(cli->sockfd, "true\n", 5, 0);

                char uname[32];
                int ulen = recv(cli->sockfd, uname, sizeof(uname) - 1, 0);
                if (ulen > 0)
                {
                    uname[ulen] = '\0';
                    char *nl = strchr(uname, '\n');
                    if (nl) *nl = '\0';
                    nl = strchr(uname, '\r');
                    if (nl) *nl = '\0';
                    trim_newline(uname);
                    if (strlen(uname) > 0)
                    {
                        // Check if admin's chosen alias is taken
                        pthread_mutex_lock(&clients_mutex);
                        int alias_taken = 0;
                        for (int j = 0; j < MAX_CLIENTS; ++j)
                        {
                            if (clients[j] && strcmp(clients[j]->username, uname) == 0)
                            {
                                alias_taken = 1;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&clients_mutex);

                        if (!alias_taken)
                        {
                            strncpy(cli->username, uname, sizeof(cli->username) - 1);
                            cli->username[sizeof(cli->username) - 1] = '\0';
                        }
                    }
                }
                break;
            }
        }

        if (!auth_success)
        {
            ratelimit_release_ip_connection(cli->ip);
            close(cli->sockfd);
            free(cli);
            pthread_detach(pthread_self());
            return NULL;
        }
    }

    cli->join_time = time(NULL);
    inet_ntop(AF_INET, &cli->address.sin_addr, cli->ip, INET_ADDRSTRLEN);
    log_connection(cli->username, cli->ip);
    if (cli->is_admin)
        printf("\033[1;35m %s JOINED AS -> ADMIN.\033[0m\n", cli->username);
    else
        printf("\033[1;39m %s  JOINED AS -> USER.\033[0m\n", cli->username);

    fflush(stdout);
    strncpy(cli->party_code, "public", PARTY_CODE_LEN);

    add_client(cli);

    char join_msg[128];
    snprintf(join_msg, sizeof(join_msg), "\033[1;32m[JOIN] %s has entered the chat.\033[0m\n", cli->username);
    party_broadcast_system(join_msg, cli->sockfd);

    send_to_client(cli, "\033[1;36mWelcome! Type /help for commands.\033[0m\n");
    send_to_client(cli, "\033[1;34m--- Message of the Day ---\033[0m\n");
    handle_command("/motd", cli);
    if (strlen(pinned_message) > 0)
    {
        send_to_client(cli,
                       "\n\033[1;33m=== PINNED MESSAGE ===\033[0m\n"
                       "%s\n"
                       "\033[1;33m======================\033[0m\n",
                       pinned_message);
    }

    // Deliver offline inbox messages
    offline_msg_t off_msgs[MAX_OFFLINE_PER_USER];
    int off_cnt = db_fetch_and_clear_offline_msgs(cli->username, off_msgs, MAX_OFFLINE_PER_USER);
    if (off_cnt > 0)
    {
        send_to_client(cli, "\033[1;36m=== [OFFLINE INBOX] You received %d message(s) while away ===\033[0m\n", off_cnt);
        for (int m = 0; m < off_cnt; ++m)
        {
            if (strncmp(off_msgs[m].content, "__E2EE__:", 9) == 0)
            {
                send(cli->sockfd, off_msgs[m].content, strlen(off_msgs[m].content), 0);
            }
            else
            {
                char tbuf[32];
                struct tm *tmi = localtime(&off_msgs[m].timestamp);
                strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tmi);
                send_to_client(cli, "\033[1;35m[OFFLINE PM from %s at %s]: %s\033[0m\n",
                               off_msgs[m].sender, tbuf, off_msgs[m].content);
            }
        }
        send_to_client(cli, "\033[1;36m===============================================================\033[0m\n");
    }

    client_ratelimit_t rl;
    ratelimit_client_init(&rl);

    while (1)
    {
        int rlen = recv(cli->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (rlen <= 0)
            break;

        buffer[rlen] = '\0';
        trim_newline(buffer);
        if (strlen(buffer) == 0)
            continue;

        if (strncmp(buffer, "__", 2) == 0)
        {
            handle_internal_protocol(buffer, cli);
            continue;
        }

        // Message rate limiting check
        if (!ratelimit_check_message(cli, &rl))
        {
            continue;
        }

        if (cli->is_muted)
        {
            send_to_client(cli, "\033[1;31m[SERVER] You are muted.\033[0m\n");
            continue;
        }

        if (buffer[0] == '/')
        {
            if (handle_command(buffer, cli))
                continue; // command handled
        }
        else
        {
            char msg[sizeof(buffer) + 512];
            snprintf(msg, sizeof(msg), "%s[%s]:%s %s\n",
                     cli->color,
                     cli->username,
                     COLOR_RESET,
                     buffer);
            broadcast_message(msg, cli);
            strncpy(cli->last_msg, buffer, BUFFER_SIZE - 1);
            cli->last_msg[BUFFER_SIZE - 1] = '\0';
            cli->last_msg_time = time(NULL);
        }
    }

    char leave_msg[128];
    snprintf(leave_msg, sizeof(leave_msg), "\033[1;31m[LEAVE] %s has left the chat.\033[0m\n", cli->username);
    party_broadcast_system(leave_msg, cli->sockfd);
    log_disconnection(cli->username, cli->ip);
    update_lastseen(cli->username);
    save_lastseen_to_file();

    cli->active = 0;
    ratelimit_release_ip_connection(cli->ip);
    close(cli->sockfd);
    remove_client(cli->sockfd);

    free(cli);
    pthread_detach(pthread_self());
    return NULL;
}

int main()
{
    ratelimit_init();
    db_init_admin("admin123");

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
    {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    signal(SIGPIPE, SIG_IGN);

    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    if (listen(listener, 10) < 0)
    {
        perror("Listen failed");
        return 1;
    }

    printf("Server started on port %d\n", PORT);
    log_server_start(PORT);
    load_banlist();
    server_start_time = time(NULL);
    init_vanish_module();
    pthread_t vanish_thread;
    pthread_create(&vanish_thread, NULL, vanish_cleaner_thread, NULL);
    create_party("public");
    
    while (1)
    {
        int new_sock = accept(listener, (struct sockaddr *)&cli_addr, &cli_len);
        if (new_sock < 0)
        {
            perror("Accept failed");
            continue;
        }

        char cli_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, cli_ip, INET_ADDRSTRLEN);

        if (is_ip_banned(cli_ip))
        {
            printf("Rejected banned IP: %s\n", cli_ip);
            close(new_sock);
            continue;
        }

        if (!ratelimit_allow_ip_connection(cli_ip))
        {
            printf("Rejected connection from %s: IP connection limit exceeded\n", cli_ip);
            char rej[] = "System: [DoS Protection] Connection limit exceeded for your IP.\n";
            send(new_sock, rej, strlen(rej), 0);
            close(new_sock);
            continue;
        }

        client_t *cli = (client_t *)malloc(sizeof(client_t));
        memset(cli, 0, sizeof(client_t));
        cli->active = 1;
        cli->sockfd = new_sock;
        cli->address = cli_addr;
        cli->is_admin = 0;
        cli->is_muted = 0;
        cli->last_seen = time(NULL);
        snprintf(cli->ip, sizeof(cli->ip), "%s", cli_ip);
        strncpy(cli->party_code, "public", PARTY_CODE_LEN);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, (void *)cli);
    }

    return 0;
}
