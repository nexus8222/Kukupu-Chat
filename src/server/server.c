// server.c
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
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
#include "motd.h"

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

void party_broadcast_system(const char *msg, socket_t except_sock)
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

static int is_valid_username(const char *name)
{
    if (!name) return 0;
    size_t len = strlen(name);
    if (len < 2 || len > 28) return 0;

    // Check reserved names
    if (strcasecmp(name, "server") == 0 ||
        strcasecmp(name, "system") == 0 ||
        strcasecmp(name, "all") == 0 ||
        strcasecmp(name, "root") == 0 ||
        strcasecmp(name, "public") == 0)
    {
        return 0;
    }

    // Only allow alphanumeric, underscore, hyphen
    for (size_t i = 0; i < len; ++i)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
        {
            return 0;
        }
    }
    return 1;
}

int add_client(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);
    strncpy(cl->color, "\033[0;37m", sizeof(cl->color) - 1);
    cl->color[sizeof(cl->color) - 1] = '\0';

    int added = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (!clients[i])
        {
            clients[i] = cl;
            added = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return added;
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

void remove_client(socket_t sockfd)
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
            if (fsize == 0 || fsize > MAX_FILE_SIZE || strstr(filename, "..") != NULL ||
                strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL)
            {
                return;
            }
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
    char name[32];
    client_t *cli = (client_t *)arg;

    int authenticated = 0;
    while (!authenticated)
    {
        char auth_buf[512] = {0};
        int rlen = recv(cli->sockfd, auth_buf, sizeof(auth_buf) - 1, 0);
        if (rlen <= 0)
        {
            ratelimit_release_ip_connection(cli->ip);
            close(cli->sockfd);
            free(cli);
            pthread_detach(pthread_self());
            return NULL;
        }
        auth_buf[rlen] = '\0';
        trim_newline(auth_buf);
        if (strlen(auth_buf) == 0) continue;

        // Protocol 1: Registration
        if (strncmp(auth_buf, "__REGISTER__:", 13) == 0)
        {
            char reg_user[64] = {0};
            char reg_pwd[128] = {0};
            if (sscanf(auth_buf + 13, "%63[^:]:%127s", reg_user, reg_pwd) == 2)
            {
                char reg_err[128] = {0};
                if (db_register_user(reg_user, reg_pwd, reg_err, sizeof(reg_err)))
                {
                    send(cli->sockfd, "__REG_OK__:Account registered successfully!\n", 44, 0);
                }
                else
                {
                    char resp[256];
                    snprintf(resp, sizeof(resp), "__REG_ERR__:%s\n", reg_err);
                    send(cli->sockfd, resp, strlen(resp), 0);
                }
            }
            else
            {
                send(cli->sockfd, "__REG_ERR__:Invalid registration syntax\n", 40, 0);
            }
            continue;
        }

        // Protocol 2: Registered User Login
        if (strncmp(auth_buf, "__LOGIN__:", 10) == 0)
        {
            char log_user[64] = {0};
            char log_pwd[128] = {0};
            if (sscanf(auth_buf + 10, "%63[^:]:%127s", log_user, log_pwd) == 2)
            {
                if (!db_user_exists(log_user) || !db_verify_user(log_user, log_pwd))
                {
                    send(cli->sockfd, "__LOGIN_ERR__:Invalid username or password.\n", 44, 0);
                    continue;
                }

                pthread_mutex_lock(&clients_mutex);
                int online = 0;
                for (int i = 0; i < MAX_CLIENTS; ++i)
                {
                    if (clients[i] && clients[i]->active && strcasecmp(clients[i]->username, log_user) == 0)
                    {
                        online = 1;
                        break;
                    }
                }
                if (online)
                {
                    pthread_mutex_unlock(&clients_mutex);
                    send(cli->sockfd, "__LOGIN_ERR__:Account is already logged in online.\n", 51, 0);
                    continue;
                }
                pthread_mutex_unlock(&clients_mutex);

                strncpy(cli->username, log_user, sizeof(cli->username) - 1);
                cli->username[sizeof(cli->username) - 1] = '\0';
                cli->is_registered = 1;
                authenticated = 1;
                send(cli->sockfd, "__LOGIN_OK__\n", 13, 0);
                break;
            }
            else
            {
                send(cli->sockfd, "__LOGIN_ERR__:Invalid login syntax.\n", 36, 0);
                continue;
            }
        }

        // Protocol 3: Guest / Anonymous Login
        if (strncmp(auth_buf, "__GUEST__:", 10) == 0)
        {
            char guest_user[64] = {0};
            snprintf(guest_user, sizeof(guest_user), "%.63s", auth_buf + 10);
            trim_newline(guest_user);

            if (!is_valid_username(guest_user))
            {
                send(cli->sockfd, "__GUEST_ERR__:Invalid username format.\n", 39, 0);
                continue;
            }

            if (db_user_exists(guest_user))
            {
                send(cli->sockfd, "__GUEST_ERR__:Username is registered. Please log in with password.\n", 67, 0);
                continue;
            }

            pthread_mutex_lock(&clients_mutex);
            int online = 0;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (clients[i] && clients[i]->active && strcasecmp(clients[i]->username, guest_user) == 0)
                {
                    online = 1;
                    break;
                }
            }
            if (online)
            {
                pthread_mutex_unlock(&clients_mutex);
                send(cli->sockfd, "__GUEST_ERR__:Username already taken online.\n", 45, 0);
                continue;
            }
            pthread_mutex_unlock(&clients_mutex);

            strncpy(cli->username, guest_user, sizeof(cli->username) - 1);
            cli->username[sizeof(cli->username) - 1] = '\0';
            cli->is_registered = 0;
            authenticated = 1;
            send(cli->sockfd, "__GUEST_OK__\n", 13, 0);
            break;
        }

        // Legacy / Direct Join (Automated tests & Admin challenge)
        strncpy(name, auth_buf, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        if (strlen(name) == 0 || (strcmp(name, "admin") != 0 && !is_valid_username(name)))
        {
            send_to_client(cli, "\033[1;31m[SERVER] Invalid username. Allowed: 2-28 chars (alphanumeric, _, -).\033[0m\n");
            ratelimit_release_ip_connection(cli->ip);
            close(cli->sockfd);
            free(cli);
            pthread_detach(pthread_self());
            return NULL;
        }

        // Check if username is a registered account
        if (strcmp(name, "admin") != 0 && db_user_exists(name))
        {
            char pwd[64];
            int auth_success = 0;
            for (int i = 0; i < MAX_TRIES; i++)
            {
                send(cli->sockfd, "ask\n", 4, 0);
                int plen = recv(cli->sockfd, pwd, sizeof(pwd) - 1, 0);
                if (plen <= 0) break;
                pwd[plen] = '\0';
                trim_newline(pwd);

                if (!db_verify_user(name, pwd))
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
                    cli->is_registered = 1;
                    auth_success = 1;
                    strncpy(cli->username, name, sizeof(cli->username) - 1);
                    cli->username[sizeof(cli->username) - 1] = '\0';
                    send(cli->sockfd, "true\n", 5, 0);
                    authenticated = 1;
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
            break;
        }

        // Legacy Admin Challenge
        if (strcmp(name, "admin") == 0)
        {
            char pwd[64];
            int auth_success = 0;
            for (int i = 0; i < MAX_TRIES; i++)
            {
                send(cli->sockfd, "ask\n", 4, 0);
                int plen = recv(cli->sockfd, pwd, sizeof(pwd) - 1, 0);
                if (plen <= 0) break;
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
                    cli->is_registered = 1;
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
                    authenticated = 1;
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
            break;
        }

        // Standard Unregistered Anonymous / Guest User
        pthread_mutex_lock(&clients_mutex);
        int taken = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (clients[i] && strcmp(clients[i]->username, name) == 0)
            {
                taken = 1;
                break;
            }
        }
        if (taken)
        {
            pthread_mutex_unlock(&clients_mutex);
            send_to_client(cli, "\033[1;31m[SERVER] Username already taken.\033[0m\n");
            ratelimit_release_ip_connection(cli->ip);
            close(cli->sockfd);
            free(cli);
            pthread_detach(pthread_self());
            return NULL;
        }
        pthread_mutex_unlock(&clients_mutex);

        strncpy(cli->username, name, sizeof(cli->username) - 1);
        cli->username[sizeof(cli->username) - 1] = '\0';
        cli->is_registered = 0;
        authenticated = 1;
        break;
    }

    cli->join_time = time(NULL);
    inet_ntop(AF_INET, &cli->address.sin_addr, cli->ip, INET_ADDRSTRLEN);
    log_connection(cli->username, cli->ip);
    if (cli->is_admin)
        printf("\033[1;35m %s JOINED AS -> ADMIN.\033[0m\n", cli->username);
    else if (cli->is_registered)
        printf("\033[1;32m %s JOINED AS -> REGISTERED MEMBER.\033[0m\n", cli->username);
    else
        printf("\033[1;39m %s  JOINED AS -> USER.\033[0m\n", cli->username);

    fflush(stdout);
    strncpy(cli->party_code, "public", PARTY_CODE_LEN);

    if (!add_client(cli))
    {
        send_to_client(cli, "\033[1;31m[SERVER] Server is full.\033[0m\n");
        ratelimit_release_ip_connection(cli->ip);
        close(cli->sockfd);
        free(cli);
        pthread_detach(pthread_self());
        return NULL;
    }

    if (cli->is_registered)
        send_to_client(cli, "__STATUS_REG__:1\n");
    else
        send_to_client(cli, "__STATUS_REG__:0\n");

    char join_msg[128];
    if (cli->is_admin)
        snprintf(join_msg, sizeof(join_msg), "\033[1;35m[JOIN] %s (Admin) has entered the chat.\033[0m\n", cli->username);
    else if (cli->is_registered)
        snprintf(join_msg, sizeof(join_msg), "\033[1;32m[JOIN] @%s (Registered) has entered the chat.\033[0m\n", cli->username);
    else
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

    char rx_buf[16384];
    int rx_len = 0;

    while (1)
    {
        int space = (int)sizeof(rx_buf) - 1 - rx_len;
        if (space <= 0)
        {
            rx_len = 0;
            space = (int)sizeof(rx_buf) - 1;
        }

        int rlen = recv(cli->sockfd, rx_buf + rx_len, space, 0);
        if (rlen <= 0)
            break;

        rx_len += rlen;
        rx_buf[rx_len] = '\0';

        char *newline_pos;
        while ((newline_pos = memchr(rx_buf, '\n', rx_len)) != NULL)
        {
            int line_len = (int)(newline_pos - rx_buf);
            char line[4096];
            if (line_len >= (int)sizeof(line))
                line_len = (int)sizeof(line) - 1;

            memcpy(line, rx_buf, line_len);
            line[line_len] = '\0';

            int consumed = (int)(newline_pos - rx_buf) + 1;
            int remain = rx_len - consumed;
            if (remain > 0)
            {
                memmove(rx_buf, rx_buf + consumed, remain);
            }
            rx_len = remain;
            rx_buf[rx_len] = '\0';

            trim_newline(line);
            if (strlen(line) == 0)
                continue;

            if (strncmp(line, "__", 2) == 0)
            {
                handle_internal_protocol(line, cli);
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

            if (line[0] == '/')
            {
                if (handle_command(line, cli))
                    continue; // command handled
            }
            else
            {
                for (size_t i = 0; line[i]; i++)
                {
                    if (line[i] == '\r' || line[i] == '\n')
                    {
                        line[i] = ' ';
                    }
                }
                char msg[sizeof(line) + 512];
                snprintf(msg, sizeof(msg), "%s[%s]:%s %s\n",
                         cli->color,
                         cli->username,
                         COLOR_RESET,
                         line);
                broadcast_message(msg, cli);
                strncpy(cli->last_msg, line, BUFFER_SIZE - 1);
                cli->last_msg[BUFFER_SIZE - 1] = '\0';
                cli->last_msg_time = time(NULL);
            }
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
    if (net_init() != 0)
    {
        fprintf(stderr, "Network initialization failed\n");
        exit(1);
    }
    atexit(net_cleanup);

    ratelimit_init();
    db_init_admin("admin123");

    socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALID_SOCKET(listener))
    {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in server_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

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
    load_motd();
    server_start_time = time(NULL);
    init_vanish_module();
    pthread_t vanish_thread;
    pthread_create(&vanish_thread, NULL, vanish_cleaner_thread, NULL);
    create_party("public");
    
    while (1)
    {
        socket_t new_sock = accept(listener, (struct sockaddr *)&cli_addr, &cli_len);
        if (!IS_VALID_SOCKET(new_sock))
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
        if (!cli)
        {
            ratelimit_release_ip_connection(cli_ip);
            close(new_sock);
            continue;
        }

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
        if (pthread_create(&tid, NULL, handle_client, (void *)cli) != 0)
        {
            ratelimit_release_ip_connection(cli_ip);
            close(new_sock);
            free(cli);
            continue;
        }
    }

    return 0;
}
