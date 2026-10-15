#ifndef PARTY_H
#define PARTY_H

#include "client.h"

#define MAX_PARTIES 100


typedef struct {
    char code[PARTY_CODE_LEN];
    int is_locked;
    char invited_users[MAX_CLIENTS][USERNAME_LEN];
    int invite_count;
} party_info_t;

int party_exists(const char *code);
int create_party(const char *code);
void list_active_parties(client_t *cli);
void party_broadcast(const char *msg, const char *party_code, int except_sock);

extern party_info_t party_list[MAX_PARTIES];
extern int party_count;
extern pthread_mutex_t party_mutex;

#endif
