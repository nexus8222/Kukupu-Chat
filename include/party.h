#ifndef PARTY_H
#define PARTY_H

#include "client.h"

#define MAX_PARTIES 100


typedef struct {
    char code[PARTY_CODE_LEN];
    int is_locked;
    char owner[USERNAME_LEN];
    int privacy; // 0: anyone can join (open), 1: ask to join (approval required)
    char admins[MAX_CLIENTS][USERNAME_LEN];
    int admin_count;
    char invited_users[MAX_CLIENTS][USERNAME_LEN];
    int invite_count;
} party_info_t;

int party_exists(const char *code);
int create_party(const char *code);
int create_party_with_owner(const char *code, const char *owner, int privacy);
int party_is_admin(const char *code, const char *username);
int party_get_privacy(const char *code);
const char *party_get_owner(const char *code);
int party_add_admin(const char *code, const char *username);
void list_active_parties(client_t *cli);
void party_broadcast(const char *msg, const char *party_code, socket_t except_sock);

extern party_info_t party_list[MAX_PARTIES];
extern int party_count;
extern pthread_mutex_t party_mutex;

#endif
