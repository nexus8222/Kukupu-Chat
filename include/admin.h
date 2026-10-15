// admin.h
#ifndef ADMIN_H
#define ADMIN_H

#include "client.h"

int kick_user(const char *username, client_t *requester);
int ban_user(const char *username, client_t *requester);
int unban_user(const char *username, client_t *requester);
void list_banned(client_t *requester);

#endif
