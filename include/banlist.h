// banlist.h
#ifndef BANLIST_H
#define BANLIST_H

#include "common.h"
#include <netinet/in.h>
void list_banned(client_t *cli);

#define MAX_BANNED 100

void ban_ip(const char *ip);
void unban_ip(const char *ip);
int is_ip_banned(const char *ip);
void load_banlist(void);
int handle_admin_command(char *cmd, client_t *cli);

#endif
