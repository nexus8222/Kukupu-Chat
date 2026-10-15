#ifndef MUTE_H
#define MUTE_H

#include "client.h"

int mute_user(const char *username, client_t *requester);
int unmute_user(const char *username, client_t *requester);

void list_muted(client_t *cli);

#endif
