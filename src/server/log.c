#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "log.h"
#include "common.h"
#include <string.h> 
#include <stdarg.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void timestamp(char *buffer, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, len, "%Y-%m-%d %H:%M:%S", t);
}

void log_event(const char *fmt, ...) {
    char ts[64];
    timestamp(ts, sizeof(ts));

    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&log_mutex);
    printf("\033[1;35m[%s] %s\033[0m\n", ts, msg);

    FILE *fp = fopen("server.log", "a");
    if (fp) {
        fprintf(fp, "[%s] %s\n", ts, msg);
        fclose(fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

void log_connection(const char *username, const char *ip) {
    char ts[64];
    timestamp(ts, sizeof(ts));
    pthread_mutex_lock(&log_mutex);
    printf("\033[1;32m[%s] CONNECT: %s (%s)\033[0m\n", ts, username, ip);
    pthread_mutex_unlock(&log_mutex);
}

void log_disconnection(const char *username, const char *ip) {
    char ts[64];
    timestamp(ts, sizeof(ts));
    pthread_mutex_lock(&log_mutex);
    printf("\033[1;31m[%s] DISCONNECT: %s (%s)\033[0m\n", ts, username, ip);
    pthread_mutex_unlock(&log_mutex);
}

void log_server_start(int port) {
    char ts[64];
    timestamp(ts, sizeof(ts));
    pthread_mutex_lock(&log_mutex);
    printf("\033[1;34m[%s] SERVER STARTED on port %d\033[0m\n", ts, port);
    pthread_mutex_unlock(&log_mutex);
}

void send_log_to_client(client_t *cli) {
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen("server.log", "r");
    if (!fp) {
        pthread_mutex_unlock(&log_mutex);
        send(cli->sockfd, "[SERVER] Log file not found.\n", 30, 0);
        return;
    }

    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        send(cli->sockfd, line, strlen(line), 0);
    }

    fclose(fp);
    pthread_mutex_unlock(&log_mutex);
}
