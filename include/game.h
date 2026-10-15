#ifndef GAME_H
#define GAME_H

#include <time.h>
#include "client.h"

typedef struct {
    int active;
    int number_to_guess;
    time_t start_time;
    char started_by[USERNAME_LEN];
    char current_turn[USERNAME_LEN];
    char current_player[USERNAME_LEN];
    int total_players;
} guess_game_t;

extern guess_game_t guess_game;

void start_guess_game(client_t *cli);
void handle_guess_command(client_t *cli, const char *arg);

#endif
