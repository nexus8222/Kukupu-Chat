#ifndef LASTSEEN_H
#define LASTSEEN_H
#include "common.h"
#include <time.h>

typedef struct {
    char username[32];
    time_t timestamp;
} lastseen_t;

extern lastseen_t lastseen_list[MAX_CLIENTS];

void update_lastseen(const char *username);
void save_lastseen_to_file();
void load_lastseen_from_file();
int get_lastseen(const char *username, time_t *out_time);

#endif
