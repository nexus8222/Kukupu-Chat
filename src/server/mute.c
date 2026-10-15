#include <string.h>
#include "mute.h"
#include "client.h"
#include "utils.h"
#include "common.h"
#include <stdio.h>


extern client_t *clients[MAX_CLIENTS];

client_t* find_client_by_name(const char *name);  // Defined in admin.c

int mute_user(const char *target, client_t *requester) {
    if (!target || strlen(target) == 0) {
        send_to_client(requester, "\033[1;33m[ADMIN] Usage: /mute <user>\033[0m\n");
        return 0;
    }
    client_t *victim = find_client_by_name(target);
    if (!victim) {
        send_to_client(requester, "\033[1;33m[ADMIN] User not found.\033[0m\n");
        return 0;
    }

    if (victim->is_admin) {
        send_to_client(requester, "\033[1;31m[ADMIN] You cannot mute another admin.\033[0m\n");
        return 0;
    }

    victim->is_muted = 1;
    send_to_client(victim, "\033[1;33m[SERVER] You have been muted.\033[0m\n");
    send_to_client(requester, "\033[1;32m[ADMIN] User muted.\033[0m\n");
    return 1;
}

int unmute_user(const char *target, client_t *requester) {
    if (!target || strlen(target) == 0) {
        send_to_client(requester, "\033[1;33m[ADMIN] Usage: /unmute <user>\033[0m\n");
        return 0;
    }
    client_t *victim = find_client_by_name(target);
    if (!victim) {
        send_to_client(requester, "\033[1;33m[ADMIN] User not found.\033[0m\n");
        return 0;
    }

    victim->is_muted = 0;
    send_to_client(victim, "\033[1;32m[SERVER] You have been unmuted.\033[0m\n");
    send_to_client(requester, "\033[1;32m[ADMIN] User unmuted.\033[0m\n");
    return 1;
}

void list_muted(client_t *cli) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "\033[1m[SERVER] Muted Users:\033[0m\n");
    send(cli->sockfd, buffer, strlen(buffer), 0);

    pthread_mutex_lock(&clients_mutex);
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->is_muted) {
            snprintf(buffer, sizeof(buffer), "%s\n", clients[i]->username);
            send(cli->sockfd, buffer, strlen(buffer), 0);
            count++;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (count == 0) {
        send(cli->sockfd, "None\n", 5, 0);
    }
}
