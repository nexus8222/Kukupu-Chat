#include "lastseen.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

lastseen_t lastseen_list[MAX_CLIENTS];
static pthread_mutex_t lastseen_mutex = PTHREAD_MUTEX_INITIALIZER;

void update_lastseen(const char *username) {
    if (!username || strlen(username) == 0) return;

    pthread_mutex_lock(&lastseen_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (strcmp(lastseen_list[i].username, username) == 0) {
            lastseen_list[i].timestamp = time(NULL);
            pthread_mutex_unlock(&lastseen_mutex);
            return;
        }
        if (strlen(lastseen_list[i].username) == 0) {
            strncpy(lastseen_list[i].username, username, sizeof(lastseen_list[i].username) - 1);
            lastseen_list[i].username[sizeof(lastseen_list[i].username) - 1] = '\0';
            lastseen_list[i].timestamp = time(NULL);
            pthread_mutex_unlock(&lastseen_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&lastseen_mutex);
}

void save_lastseen_to_file() {
    pthread_mutex_lock(&lastseen_mutex);
    FILE *fp = fopen(".lastseen", "wb");
    if (!fp) {
        pthread_mutex_unlock(&lastseen_mutex);
        return;
    }
    fwrite(lastseen_list, sizeof(lastseen_t), MAX_CLIENTS, fp);
    fclose(fp);
    pthread_mutex_unlock(&lastseen_mutex);
}

void load_lastseen_from_file() {
    pthread_mutex_lock(&lastseen_mutex);
    FILE *fp = fopen(".lastseen", "rb");
    if (!fp) {
        pthread_mutex_unlock(&lastseen_mutex);
        return;
    }
    size_t n = fread(lastseen_list, sizeof(lastseen_t), MAX_CLIENTS, fp);
    (void)n;
    fclose(fp);
    pthread_mutex_unlock(&lastseen_mutex);
}

int get_lastseen(const char *username, time_t *out_time) {
    if (!username || !out_time) return 0;

    pthread_mutex_lock(&lastseen_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (strlen(lastseen_list[i].username) > 0 &&
            strcmp(lastseen_list[i].username, username) == 0) {
            *out_time = lastseen_list[i].timestamp;
            pthread_mutex_unlock(&lastseen_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&lastseen_mutex);
    return 0;
}
