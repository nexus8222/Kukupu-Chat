#ifndef UTILS_H
#define UTILS_H

#include "client.h"

void send_to_client(client_t *cli, const char *fmt, ...);
void broadcast_message(const char *msg, client_t *sender);
void trim_newline(char *s);

#endif
