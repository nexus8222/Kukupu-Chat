// admin.c
/*
THIS PART CONTAINS ALL LOGICS 
OF COMMANDS AVAILABLE TO ADMIN!
*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> 
#include "admin.h"
#include "client.h"
#include "banlist.h"
#include "mute.h"
#include "utils.h"
#include "common.h"

extern client_t *clients[MAX_CLIENTS];

client_t* find_client_by_name(const char *name) {
    if (!name || strlen(name) == 0) return NULL;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcmp(clients[i]->username, name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return clients[i];
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

int kick_user(const char *target, client_t *requester) {
    if (!target || strlen(target) == 0) {
        send_to_client(requester, "\033[1;33m[ADMIN] Usage: /kick <user>\033[0m\n");
        return 0;
    }
    client_t *victim = find_client_by_name(target);
    if (!victim) {
        send_to_client(requester, "\033[1;33m[ADMIN] User not found.\033[0m\n");
        return 0;
    }

    send_to_client(victim, "\033[1;31m[SERVER] You have been kicked by admin.\033[0m\n");
    shutdown(victim->sockfd, SHUT_RDWR);
    close(victim->sockfd);
    send_to_client(requester, "\033[1;32m[ADMIN] User kicked.\033[0m\n");
    return 1;
}

int ban_user(const char *target, client_t *requester) {
    if (!target || strlen(target) == 0) {
        send_to_client(requester, "\033[1;33m[ADMIN] Usage: /ban <user>\033[0m\n");
        return 0;
    }
    client_t *victim = find_client_by_name(target);
    if (victim) {
        ban_ip(victim->ip);
        send_to_client(victim, "\033[1;31m[SERVER] You are banned.\033[0m\n");
        shutdown(victim->sockfd, SHUT_RDWR);
        close(victim->sockfd);
    } else {
        ban_ip(target);  // fallback for raw IP banning
    }

    send_to_client(requester, "\033[1;33m[ADMIN] Ban applied.\033[0m\n");
    return 1;
}

int unban_user(const char *ip, client_t *requester) {
    if (!ip || strlen(ip) == 0) {
        send_to_client(requester, "\033[1;33m[ADMIN] Usage: /unban <ip>\033[0m\n");
        return 0;
    }
    if (!is_ip_banned(ip)) {
        send_to_client(requester, "\033[1;33m[ADMIN] IP not in ban list.\033[0m\n");
        return 0;
    }

    unban_ip(ip);
    send_to_client(requester, "\033[1;32m[ADMIN] Unban applied.\033[0m\n");
    return 1;
}

int handle_admin_command(char *cmd, client_t *cli) {
    if (!cli->is_admin) {
        send_to_client(cli, "\033[1;31m[SERVER] Unauthorized command.\033[0m\n");
        return 1;
    }

    char arg[64];

    if (sscanf(cmd, "/kick %63s", arg) == 1) {
        return kick_user(arg, cli);
    } else if (sscanf(cmd, "/ban %63s", arg) == 1) {
        return ban_user(arg, cli);
    } else if (sscanf(cmd, "/unban %63s", arg) == 1) {
        return unban_user(arg, cli);
    } else if (sscanf(cmd, "/mute %63s", arg) == 1) {
        return mute_user(arg, cli);     // From mute.c
    } else if (sscanf(cmd, "/unmute %63s", arg) == 1) {
        return unmute_user(arg, cli);   // From mute.c
    }

    return 0; // Not handled — fallback to other command processors
}
