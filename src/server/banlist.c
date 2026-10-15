// banlist.c
//Handles banlist and storage related things
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h> 
#include "banlist.h"
#include "common.h"
#include "client.h" 
#define BAN_FILE "banned.txt"

static char banned_ips[MAX_BANNED][INET_ADDRSTRLEN];
static int banned_count = 0;
static pthread_mutex_t banlist_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_trim_lf(char *s, int size) {
    for (int i = 0; i < size; i++) {
        if (s[i] == '\n' || s[i] == '\r') {
            s[i] = '\0';
            break;
        }
    }
}

void ban_ip(const char *ip) {
    if (!ip || strlen(ip) == 0) return;

    pthread_mutex_lock(&banlist_mutex);
    for (int i = 0; i < banned_count; ++i) {
        if (strcmp(banned_ips[i], ip) == 0) {
            pthread_mutex_unlock(&banlist_mutex);
            return; // already banned
        }
    }

    if (banned_count < MAX_BANNED) {
        snprintf(banned_ips[banned_count], sizeof(banned_ips[banned_count]), "%s", ip);
        banned_count++;
        FILE *fp = fopen(BAN_FILE, "a");
        if (fp) {
            fprintf(fp, "%s\n", ip);
            fclose(fp);
        }
    }
    pthread_mutex_unlock(&banlist_mutex);
}

void unban_ip(const char *ip) {
    if (!ip || strlen(ip) == 0) return;

    pthread_mutex_lock(&banlist_mutex);
    int found = 0;
    for (int i = 0; i < banned_count; ++i) {
        if (strcmp(banned_ips[i], ip) == 0) {
            found = 1;
            for (int j = i; j < banned_count - 1; ++j) {
                snprintf(banned_ips[j], sizeof(banned_ips[j]), "%s", banned_ips[j + 1]);
            }
            banned_count--;
            break;
        }
    }

    if (found) {
        FILE *fp = fopen(BAN_FILE, "w");
        if (fp) {
            for (int i = 0; i < banned_count; ++i) {
                fprintf(fp, "%s\n", banned_ips[i]);
            }
            fclose(fp);
        }
    }
    pthread_mutex_unlock(&banlist_mutex);
}

int is_ip_banned(const char *ip) {
    if (!ip || strlen(ip) == 0) return 0;

    pthread_mutex_lock(&banlist_mutex);
    for (int i = 0; i < banned_count; ++i) {
        if (strcmp(banned_ips[i], ip) == 0) {
            pthread_mutex_unlock(&banlist_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&banlist_mutex);
    return 0;
}

void load_banlist() {
    pthread_mutex_lock(&banlist_mutex);
    FILE *fp = fopen(BAN_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&banlist_mutex);
        return;
    }

    char line[INET_ADDRSTRLEN];
    while (fgets(line, sizeof(line), fp)) {
        str_trim_lf(line, sizeof(line));
        if (strlen(line) > 0 && banned_count < MAX_BANNED) {
            snprintf(banned_ips[banned_count], sizeof(banned_ips[banned_count]), "%s", line);
            banned_count++;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&banlist_mutex);
}
 
void list_banned(client_t *cli) {
    pthread_mutex_lock(&banlist_mutex);
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "\033[1m[SERVER] Banned IPs:\033[0m\n");
    send(cli->sockfd, buffer, strlen(buffer), 0);

    if (banned_count == 0) {
        send(cli->sockfd, "None\n", 5, 0);
        pthread_mutex_unlock(&banlist_mutex);
        return;
    }

    for (int i = 0; i < banned_count; ++i) {
        snprintf(buffer, sizeof(buffer), "%s\n", banned_ips[i]);
        send(cli->sockfd, buffer, strlen(buffer), 0);
    }
    pthread_mutex_unlock(&banlist_mutex);
}
