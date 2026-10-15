#include "utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

extern client_t *clients[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;

void send_to_client(client_t *cli, const char *fmt, ...)
{
    if (!cli || cli->sockfd < 0 || !fmt) return;
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    send(cli->sockfd, buffer, strlen(buffer), 0);
}

void trim_newline(char *s)
{
    if (!s) return;
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
    {
        s[len - 1] = '\0';
        len--;
    }
}

void broadcast_message(const char *msg, client_t *sender)
{
    if (!msg) return;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i])
        {
            if (!sender || strcmp(clients[i]->party_code, sender->party_code) == 0)
            {
                send(clients[i]->sockfd, msg, strlen(msg), 0);
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}
