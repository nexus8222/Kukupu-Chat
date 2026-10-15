// commands.h
#ifndef COMMANDS_H
#define COMMANDS_H

#include "common.h"

int handle_command(const char *cmdline, client_t *cli);
void broadcast_motd(client_t *requester);
void broadcast_system(const char *msg);

#endif  
