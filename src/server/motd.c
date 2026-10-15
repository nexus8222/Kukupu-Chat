#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "motd.h"
#include "utils.h"

char motd[2048] = "Welcome to Kukupu Chat Server!";
static pthread_mutex_t motd_mutex = PTHREAD_MUTEX_INITIALIZER;

void load_motd() {
    pthread_mutex_lock(&motd_mutex);
    FILE *fp = fopen("motd.txt", "r");
    if (fp) {
        if (fgets(motd, sizeof(motd), fp) != NULL) {
            trim_newline(motd);
        }
        fclose(fp);
    }
    pthread_mutex_unlock(&motd_mutex);
}

void set_motd(const char *new_msg) {
    if (!new_msg) return;

    pthread_mutex_lock(&motd_mutex);
    strncpy(motd, new_msg, sizeof(motd) - 1);
    motd[sizeof(motd) - 1] = '\0';

    FILE *fp = fopen("motd.txt", "w");
    if (fp) {
        fprintf(fp, "%s\n", motd);
        fclose(fp);
    }
    pthread_mutex_unlock(&motd_mutex);
}

void send_motd(client_t *cli) {
    pthread_mutex_lock(&motd_mutex);
    send_to_client(cli, "\033[1;36m[MOTD]\033[0m %s\n", motd);
    pthread_mutex_unlock(&motd_mutex);
}
