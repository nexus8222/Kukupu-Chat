// vanish.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "vanish.h"
#include "utils.h"
#include "client.h"

static vanish_msg_t vanish_messages[MAX_VANISH_MESSAGES];
static int vanish_count = 0;
static pthread_mutex_t vanish_lock = PTHREAD_MUTEX_INITIALIZER;

void init_vanish_module()
{
    memset(vanish_messages, 0, sizeof(vanish_messages));
    vanish_count = 0;
}

static int generate_msg_id()
{
    return rand() % 1000000;
}

void handle_vanish_command(client_t *cli, const char *args)
{
    if (!args || strlen(args) == 0)
    {
        send_to_client(cli, "Usage: /vanish <seconds> <message>\n");
        return;
    }

    int seconds;
    char message[MSG_LEN];

    if (sscanf(args, "%d", &seconds) != 1 || seconds <= 0)
    {
        send_to_client(cli, "Usage: /vanish <seconds> <message>\n");
        return;
    }

    const char *msg_ptr = strchr(args, ' ');
    if (!msg_ptr || strlen(msg_ptr + 1) == 0)
    {
        send_to_client(cli, "Usage: /vanish <seconds> <message>\n");
        return;
    }

    strncpy(message, msg_ptr + 1, MSG_LEN - 1);
    message[MSG_LEN - 1] = '\0';

    int msg_id = generate_msg_id();

    pthread_mutex_lock(&vanish_lock);
    if (vanish_count < MAX_VANISH_MESSAGES)
    {
        vanish_msg_t *vm = &vanish_messages[vanish_count++];
        vm->msg_id = msg_id;
        snprintf(vm->sender, sizeof(vm->sender), "%s", cli->username);
        snprintf(vm->message, sizeof(vm->message), "%s", message);
        vm->duration = seconds;
        vm->timestamp = time(NULL);
    }
    pthread_mutex_unlock(&vanish_lock);

    char broadcast_buf[MSG_LEN + 128];
    snprintf(broadcast_buf, sizeof(broadcast_buf),
             "__VANISH__:%d:%d:%s: %s", msg_id, seconds, cli->username, message);
    broadcast_message(broadcast_buf, cli);
}

void check_and_expire_vanish_messages()
{
    pthread_mutex_lock(&vanish_lock);
    time_t now = time(NULL);
    for (int i = 0; i < vanish_count; i++)
    {
        vanish_msg_t *vm = &vanish_messages[i];
        if (difftime(now, vm->timestamp) >= vm->duration)
        {
            char delete_msg[64];
            snprintf(delete_msg, sizeof(delete_msg), "__DELETE__:%d", vm->msg_id);
            broadcast_message(delete_msg, NULL);

            vanish_messages[i] = vanish_messages[--vanish_count];
            i--;
        }
    }
    pthread_mutex_unlock(&vanish_lock);
}
