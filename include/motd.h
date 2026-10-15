// motd.h
#ifndef MOTD_H
#define MOTD_H

#include "client.h"

extern char motd[2048];

void load_motd();                     // Load from file (optional)
void set_motd(const char *new_msg);  // Admin sets
void send_motd(client_t *cli);       // Send to a user

#endif
