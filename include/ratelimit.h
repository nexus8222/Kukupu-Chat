#ifndef RATELIMIT_H
#define RATELIMIT_H

#include "common.h"
#include <time.h>

#define MAX_IP_TRACK 128
#define MAX_CONNS_PER_IP 25
#define BUCKET_CAPACITY 5.0f
#define REFILL_RATE 1.5f // tokens per second
#define SPAM_VIOLATION_THRESHOLD 3
#define AUTO_MUTE_SECS 15

typedef struct {
    float tokens;
    struct timespec last_refill;
    int violations;
    time_t mute_until;
} client_ratelimit_t;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int count;
} ip_track_t;

void ratelimit_init();

// Per-client token bucket message rate limiting
void ratelimit_client_init(client_ratelimit_t *rl);
int ratelimit_check_message(client_t *cli, client_ratelimit_t *rl);

// Per-IP connection rate limiting & DoS defense
int ratelimit_allow_ip_connection(const char *ip);
void ratelimit_release_ip_connection(const char *ip);

#endif // RATELIMIT_H
