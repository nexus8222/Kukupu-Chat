// vanish.h
#ifndef VANISH_H
#define VANISH_H

#include <time.h>

#include "common.h"    // for MSG_LEN, USERNAME_LEN
#include "client.h"    // for client_t*

#define MAX_VANISH_MESSAGES 100

typedef struct {
    int msg_id;
    char sender[USERNAME_LEN];
    char message[MSG_LEN];
    int duration; // in seconds
    time_t timestamp;
} vanish_msg_t;

// Called when user sends "/vanish <seconds> <message>"
void handle_vanish_command(client_t *cli, const char *args);

// Periodic check from server thread to expire messages
void check_and_expire_vanish_messages();

// Called during server startup
void init_vanish_module();

#endif // VANISH_H
