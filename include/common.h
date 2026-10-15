//Handle which have all the commonly used system of varibales.
//and some datasets
#ifndef COMMON_H
#define COMMON_H
#define PARTY_CODE_LEN 64
#include <strings.h>

#include <time.h>
#include <netinet/in.h>  // for struct sockaddr_in
#include <pthread.h>
#define ANSI_COLOR_MAX 16
#define USERNAME_LEN 32
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 100
#define COLOR_RESET "\033[0m"

#define MSG_LEN 1024


typedef struct client {
    int sockfd;
    char username[USERNAME_LEN];
    
    int active; 
    char ip[INET_ADDRSTRLEN];   // use system-defined 16
    int is_admin;
    int is_muted;
    char color[ANSI_COLOR_MAX];
    char party_code[PARTY_CODE_LEN];
    time_t connected_time;
    struct sockaddr_in address;  // used by inet_ntop etc.
    time_t join_time;
    time_t last_seen;
    char last_msg[BUFFER_SIZE];
    time_t last_msg_time;
    int last_msg_id;
    char invited_party[PARTY_CODE_LEN];
    
    struct client *next;
} client_t;

extern client_t *clients[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;

#endif
