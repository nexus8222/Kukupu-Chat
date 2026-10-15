#include "ratelimit.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

static pthread_mutex_t ip_lock = PTHREAD_MUTEX_INITIALIZER;
static ip_track_t ip_table[MAX_IP_TRACK];
static int ip_count = 0;

void ratelimit_init() {
    pthread_mutex_lock(&ip_lock);
    memset(ip_table, 0, sizeof(ip_table));
    ip_count = 0;
    pthread_mutex_unlock(&ip_lock);
}

void ratelimit_client_init(client_ratelimit_t *rl) {
    if (!rl) return;
    rl->tokens = BUCKET_CAPACITY;
    clock_gettime(CLOCK_MONOTONIC, &rl->last_refill);
    rl->violations = 0;
    rl->mute_until = 0;
}

int ratelimit_check_message(client_t *cli, client_ratelimit_t *rl) {
    if (!cli || !rl) return 1;

    time_t now_wall = time(NULL);
    if (rl->mute_until > 0) {
        if (now_wall < rl->mute_until) {
            char warn[128];
            snprintf(warn, sizeof(warn), "System: [RATE LIMIT] You are auto-muted for spamming (%ld seconds remaining).\n", (long)(rl->mute_until - now_wall));
            send(cli->sockfd, warn, strlen(warn), 0);
            return 0;
        } else {
            // Mute expired
            rl->mute_until = 0;
            rl->violations = 0;
            rl->tokens = BUCKET_CAPACITY;
            clock_gettime(CLOCK_MONOTONIC, &rl->last_refill);
        }
    }

    struct timespec now_mono;
    clock_gettime(CLOCK_MONOTONIC, &now_mono);
    double elapsed = (now_mono.tv_sec - rl->last_refill.tv_sec) + 
                     (now_mono.tv_nsec - rl->last_refill.tv_nsec) / 1000000000.0;
    
    rl->tokens += (float)(elapsed * REFILL_RATE);
    if (rl->tokens > BUCKET_CAPACITY) {
        rl->tokens = BUCKET_CAPACITY;
    }
    rl->last_refill = now_mono;

    if (rl->tokens >= 1.0f) {
        rl->tokens -= 1.0f;
        return 1;
    }

    // Violation
    rl->violations++;
    if (rl->violations >= SPAM_VIOLATION_THRESHOLD) {
        rl->mute_until = time(NULL) + AUTO_MUTE_SECS;
        rl->violations = 0;
        char warn[128];
        snprintf(warn, sizeof(warn), "System: [RATE LIMIT] Spam detected! Auto-muted for %d seconds.\n", AUTO_MUTE_SECS);
        send(cli->sockfd, warn, strlen(warn), 0);
    } else {
        char warn[128];
        snprintf(warn, sizeof(warn), "System: [RATE LIMIT] Sending too fast! Warning %d/%d.\n", 
                 rl->violations, SPAM_VIOLATION_THRESHOLD);
        send(cli->sockfd, warn, strlen(warn), 0);
    }
    return 0;
}

int ratelimit_allow_ip_connection(const char *ip) {
    if (!ip) return 1;
    pthread_mutex_lock(&ip_lock);

    for (int i = 0; i < ip_count; i++) {
        if (strncmp(ip_table[i].ip, ip, INET_ADDRSTRLEN) == 0) {
            if (ip_table[i].count >= MAX_CONNS_PER_IP) {
                pthread_mutex_unlock(&ip_lock);
                return 0; // Exceeded limit
            }
            ip_table[i].count++;
            pthread_mutex_unlock(&ip_lock);
            return 1;
        }
    }

    // New IP
    if (ip_count < MAX_IP_TRACK) {
        strncpy(ip_table[ip_count].ip, ip, INET_ADDRSTRLEN - 1);
        ip_table[ip_count].ip[INET_ADDRSTRLEN - 1] = '\0';
        ip_table[ip_count].count = 1;
        ip_count++;
        pthread_mutex_unlock(&ip_lock);
        return 1;
    }

    // Table full, allow but don't track
    pthread_mutex_unlock(&ip_lock);
    return 1;
}

void ratelimit_release_ip_connection(const char *ip) {
    if (!ip) return;
    pthread_mutex_lock(&ip_lock);

    for (int i = 0; i < ip_count; i++) {
        if (strncmp(ip_table[i].ip, ip, INET_ADDRSTRLEN) == 0) {
            if (ip_table[i].count > 0) {
                ip_table[i].count--;
            }
            if (ip_table[i].count == 0) {
                // Remove by shifting
                for (int j = i; j < ip_count - 1; j++) {
                    ip_table[j] = ip_table[j + 1];
                }
                ip_count--;
            }
            break;
        }
    }

    pthread_mutex_unlock(&ip_lock);
}
