// client.h
#ifndef CLIENT_H
#define CLIENT_H

#include <netinet/in.h>
#include <time.h>
#include "common.h"

void add_client(client_t *cl);
void remove_client(int sockfd);
client_t *get_client_by_name(const char *name);
client_t *find_client_by_name(const char *name);
void send_to_client(client_t *cli, const char *fmt, ...);
void broadcast_message(const char *msg, client_t *sender);

#endif

