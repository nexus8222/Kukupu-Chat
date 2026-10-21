// client.c
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "common.h"
#include "emoji.h"
#include "e2ee.h"
#include "file_transfer.h"
#include "crypto_utils.h"
#include "ui.h"
#include "login_ui.h"

#define INPUT_BUFFER 1024
#define LENGTH 4096
#define HISTORY_MAX 50
#define MAX_TRIES 5

static char input[INPUT_BUFFER];
static int input_len = 0;
static int cursor_pos = 0;
static char current_suggestion[128] = {0};

static volatile sig_atomic_t flag = 0;
static socket_t sockfd = 0;
static char username[32];

static char history[HISTORY_MAX][INPUT_BUFFER];
static int history_count = 0;
static int history_pos = 0;

static const char *tab_commands[] = {
    "/help", "/msg", "/e2ee", "/sendfile", "/fileaccept", "/filedecline",
    "/editlast", "/deletelast",
    "/whoami", "/users", "/ping", "/time", "/uptime", "/motd", "/clear",
    "/lastseen", "/setcolor", "/colorlist", "/createparty", "/joinparty",
    "/party", "/leaveparty", "/guessgame", "/guess", "/vanish", "/emoji",
    "/exit", "/kick", "/ban", "/unban", "/banlist", "/mute", "/unmute",
    "/mutelist", "/setadminpwd", "/broadcast", "/pin", "/setmotd", "/log",
    "/shutdown", "/parties", "/lockparty", "/unlockparty", "/invite", "/partyinfo", "/whois",
    NULL
};

static void history_add(const char *cmd)
{
    if (!cmd || strlen(cmd) == 0) return;
    if (history_count > 0 && strcmp(history[(history_count - 1) % HISTORY_MAX], cmd) == 0)
    {
        history_pos = history_count;
        return;
    }
    snprintf(history[history_count % HISTORY_MAX], INPUT_BUFFER, "%s", cmd);
    history_count++;
    history_pos = history_count;
}

static void str_trim_lf(char *arr, int length)
{
    for (int i = 0; i < length; i++)
    {
        if (arr[i] == '\n' || arr[i] == '\r')
        {
            arr[i] = '\0';
            break;
        }
    }
}

static void strip_ansi(const char *src, char *dst, size_t max)
{
    size_t d = 0;
    int in_esc = 0;
    for (size_t i = 0; src[i] && d < max - 1; i++)
    {
        if (src[i] == '\033')
        {
            in_esc = 1;
        }
        else if (in_esc && (src[i] == 'm' || src[i] == 'K' || src[i] == 'H' || src[i] == 'J'))
        {
            in_esc = 0;
        }
        else if (!in_esc)
        {
            dst[d++] = src[i];
        }
    }
    dst[d] = '\0';
}

static void update_command_suggestion(void)
{
    if (input_len > 0 && input[0] == '/')
    {
        char matches[128] = {0};
        int count = 0;
        for (int i = 0; tab_commands[i] != NULL && count < 5; ++i)
        {
            if (strncmp(tab_commands[i], input, input_len) == 0)
            {
                if (count > 0) strncat(matches, "  ", sizeof(matches) - strlen(matches) - 1);
                strncat(matches, tab_commands[i], sizeof(matches) - strlen(matches) - 1);
                count++;
            }
        }
        if (count > 0)
        {
            snprintf(current_suggestion, sizeof(current_suggestion), "%s", matches);
            return;
        }
    }
    current_suggestion[0] = '\0';
}

static void catch_ctrl_c_and_exit(int sig)
{
    (void)sig;
    flag = 1;
}

static void disable_raw_mode(void)
{
    platform_disable_raw_mode();
}

static void enable_raw_mode(void)
{
    platform_enable_raw_mode();
}

static void handle_received_line(char *line)
{
    str_trim_lf(line, strlen(line));
    if (strlen(line) == 0) return;

    if (strncmp(line, "__KEYRESP__:", 12) == 0)
    {
        char target[32] = {0};
        char pubhex[128] = {0};
        if (sscanf(line, "__KEYRESP__:%31[^:]:%127s", target, pubhex) == 2)
        {
            if (strcmp(pubhex, "NOT_FOUND") != 0)
            {
                unsigned char raw_pub[X25519_KEY_LEN];
                if (hex_decode(pubhex, raw_pub, sizeof(raw_pub)) == X25519_KEY_LEN)
                {
                    e2ee_get_or_create_session(target, raw_pub);
                }
            }
        }
        return;
    }

    if (strncmp(line, "__E2EE__:", 9) == 0)
    {
        char from[USERNAME_LEN] = {0};
        char plaintext[BUFFER_SIZE] = {0};
        int seq = 0;
        if (e2ee_decrypt_packet(line, from, plaintext, sizeof(plaintext), &seq) == 0)
        {
            ui_add_message(MSG_E2EE, from, plaintext, 0);
        }
        else
        {
            ui_add_message(MSG_ERROR, "E2EE", "Failed to decrypt Double Ratchet envelope.", 0);
        }
        return;
    }

    if (strncmp(line, "__FILEREQ__:", 12) == 0)
    {
        int fid = 0;
        char from[32] = {0}, filename[256] = {0}, sha[SHA256_HEX_LEN] = {0};
        size_t fsize = 0;
        if (sscanf(line, "__FILEREQ__:%d:%31[^:]:%255[^:]:%zu:%64s",
                   &fid, from, filename, &fsize, sha) == 5)
        {
            file_transfer_handle_request(fid, from, filename, fsize, sha);
            char offer[512];
            snprintf(offer, sizeof(offer), "Offer: '%s' (%zu B) -> Type /fileaccept %d or /filedecline %d",
                     filename, fsize, fid, fid);
            ui_add_message(MSG_FILE, from, offer, fid);
        }
        return;
    }

    if (strncmp(line, "__FILEACCEPT__:", 15) == 0)
    {
        int fid = 0;
        char from[32] = {0};
        if (sscanf(line, "__FILEACCEPT__:%d:%31s", &fid, from) == 2)
        {
            file_transfer_handle_accept(fid, sockfd);
            ui_add_message(MSG_FILE, from, "Accepted transfer. Streaming data...", fid);
        }
        return;
    }

    if (strncmp(line, "__FILEDECLINE__:", 16) == 0)
    {
        int fid = 0;
        char from[32] = {0};
        if (sscanf(line, "__FILEDECLINE__:%d:%31s", &fid, from) == 2)
        {
            char dec[128];
            snprintf(dec, sizeof(dec), "Declined file transfer #%d.", fid);
            ui_add_message(MSG_FILE, from, dec, fid);
        }
        return;
    }

    if (strncmp(line, "__FILECHUNK__:", 14) == 0)
    {
        int fid = 0, cseq = 0, ctot = 0;
        char b64[FILE_CHUNK_SIZE * 2 + 128] = {0};
        if (sscanf(line, "__FILECHUNK__:%d:%d:%d:%4095s", &fid, &cseq, &ctot, b64) == 4)
        {
            file_transfer_receive_chunk(fid, cseq, ctot, b64);
        }
        return;
    }

    if (strncmp(line, "__FILEDONE__:", 13) == 0)
    {
        int fid = 0;
        if (sscanf(line, "__FILEDONE__:%d", &fid) == 1)
        {
            file_transfer_complete(fid);
            ui_clear_file_progress();
            ui_add_message(MSG_FILE, "System", "File transfer completed and verified with SHA-256.", fid);
        }
        return;
    }

    if (strncmp(line, "__TYPING__:", 11) == 0)
    {
        char typing_user[32] = {0};
        if (sscanf(line, "__TYPING__:%31s", typing_user) == 1)
        {
            ui_set_typing(typing_user);
        }
        return;
    }

    if (strncmp(line, "__PRIVATE__:", 12) == 0)
    {
        char *from = strtok(line + 12, ":");
        char *msg = strtok(NULL, "");
        if (from && msg)
            ui_add_message(MSG_PRIVATE, from, msg, 0);
        return;
    }

    if (strncmp(line, "__VANISH__:", 11) == 0)
    {
        int id, duration;
        char sender[32], msg[1024];
        if (sscanf(line, "__VANISH__:%d:%d:%31[^:]: %1023[^\n]", &id, &duration, sender, msg) == 4)
        {
            char short_msg[1100];
            snprintf(short_msg, sizeof(short_msg), "%s (expires in %ds)", msg, duration);
            ui_add_message(MSG_VANISH, sender, short_msg, id);
        }
        return;
    }

    if (strncmp(line, "__EDIT__:", 9) == 0)
    {
        int id;
        char newmsg[BUFFER_SIZE];
        if (sscanf(line, "__EDIT__:%d:%2047[^\n]", &id, newmsg) == 2)
        {
            ui_edit_message(id, newmsg);
        }
        return;
    }

    if (strncmp(line, "__DELETE__:", 11) == 0)
    {
        int id;
        if (sscanf(line, "__DELETE__:%d", &id) == 1)
        {
            ui_delete_message(id);
        }
        return;
    }

    if (strncmp(line, "__STATUS_REG__:", 15) == 0)
    {
        int reg = 0;
        if (sscanf(line, "__STATUS_REG__:%d", &reg) == 1)
        {
            ui_set_registered(reg);
        }
        return;
    }

    if (strstr(line, "\033[2J") != NULL)
    {
        ui_clear_messages();
        return;
    }

    // Extract sender color from original line before strip_ansi
    char sender_color[32] = {0};
    if (line[0] == '\033' && line[1] == '[')
    {
        char *end_m = strchr(line, 'm');
        if (end_m && (end_m - line) < 20)
        {
            size_t clen = (end_m - line) + 1;
            strncpy(sender_color, line, clen);
            sender_color[clen] = '\0';
        }
    }

    // Standard message: Strip ANSI color codes to parse logically
    char clean[2048];
    strip_ansi(line, clean, sizeof(clean));
    str_trim_lf(clean, strlen(clean));
    if (strlen(clean) == 0) return;

    char sender[32] = {0};
    char body[1024] = {0};

    if (sscanf(clean, "[%31[^]]]: %1023[^\n]", sender, body) == 2)
    {
        if (strcmp(sender, username) == 0)
            ui_add_message(MSG_SELF, sender, body, 0);
        else
            ui_add_message_with_color(MSG_NORMAL, sender, body, 0, sender_color);
        return;
    }

    if (sscanf(clean, "[JOIN] %31s has entered", sender) == 1)
    {
        ui_add_user(sender);
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if (sscanf(clean, "[LEAVE] %31s has left", sender) == 1)
    {
        ui_remove_user(sender);
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if (strstr(clean, "Online users:") || strstr(clean, "Connected users in your party:"))
    {
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if (strncmp(clean, " - ", 3) == 0)
    {
        char uname[32] = {0};
        if (sscanf(clean + 3, "%31s", uname) == 1)
        {
            ui_add_user(uname);
        }
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if ((strstr(clean, "Party '") && strstr(clean, "created")) || strstr(clean, "Joined party '"))
    {
        char room_code[16] = {0};
        char *q1 = strchr(clean, '\'');
        if (q1)
        {
            char *q2 = strchr(q1 + 1, '\'');
            if (q2 && (q2 - q1 - 1 < (int)sizeof(room_code)))
            {
                strncpy(room_code, q1 + 1, q2 - q1 - 1);
                ui_set_room(room_code);
            }
        }
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if (strstr(clean, "rejoined the public room"))
    {
        ui_set_room("0000");
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    if (strstr(clean, "joined party") || strstr(clean, "Room changed to"))
    {
        char room_code[16] = {0};
        char *hash = strchr(clean, '#');
        if (hash) sscanf(hash + 1, "%15s", room_code);
        if (strlen(room_code) > 0) ui_set_room(room_code);
        ui_add_message(MSG_SYSTEM, "Server", clean, 0);
        return;
    }

    ui_add_message(MSG_SYSTEM, "Server", clean, 0);
}

static void *recv_msg_handler(void *arg)
{
    (void)arg;
    char buffer[LENGTH] = {};

    while (!flag)
    {
        fd_set read_fds;
        struct timeval timeout;

        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity > 0 && FD_ISSET(sockfd, &read_fds))
        {
            int receive = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (receive > 0)
            {
                buffer[receive] = '\0';
                char *saveptr = NULL;
                char *line = strtok_r(buffer, "\n", &saveptr);
                while (line != NULL)
                {
                    handle_received_line(line);
                    line = strtok_r(NULL, "\n", &saveptr);
                }
                ui_render(input, cursor_pos, current_suggestion);
            }
            else
            {
                flag = 1;
                ui_add_message(MSG_ERROR, "Client", "Connection to server lost.", 0);
                ui_render(input, cursor_pos, NULL);
                break;
            }
        }
        else
        {
            // Heartbeat redraw to expire typing indicators or progress bars
            ui_render(input, cursor_pos, current_suggestion);
        }

        memset(buffer, 0, sizeof(buffer));
    }

    return NULL;
}

static void *file_ticker_thread(void *arg)
{
    (void)arg;
    while (!flag)
    {
        file_transfer_sender_tick(sockfd);
        usleep(15000); // 15ms pace
    }
    return NULL;
}

static void chat_loop(void)
{
    unsigned char ch;
    time_t last_typing_sent = 0;

    while (!flag)
    {
        if (!platform_wait_input(50))
            continue;

        if (platform_read_char(&ch) <= 0)
            continue;

        // Handle Escape sequences (Arrows, PageUp, PageDown)
        if (ch == 27)
        {
            unsigned char seq1 = 0, seq2 = 0, seq3 = 0;
            if (platform_read_char(&seq1) > 0)
            {
                if (seq1 == '[')
                {
                    if (platform_read_char(&seq2) > 0)
                    {
                            if (seq2 == 'A') // Up Arrow
                            {
                                if (input_len == 0)
                                {
                                    ui_scroll_up(1);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                else if (history_pos > 0)
                                {
                                    history_pos--;
                                    strncpy(input, history[history_pos % HISTORY_MAX], sizeof(input) - 1);
                                    input[sizeof(input) - 1] = '\0';
                                    input_len = (int)strlen(input);
                                    cursor_pos = input_len;
                                    update_command_suggestion();
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == 'B') // Down Arrow
                            {
                                if (input_len == 0)
                                {
                                    ui_scroll_down(1);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                else if (history_pos < history_count)
                                {
                                    history_pos++;
                                    if (history_pos == history_count)
                                    {
                                        input[0] = '\0';
                                        input_len = 0;
                                        cursor_pos = 0;
                                    }
                                    else
                                    {
                                        strncpy(input, history[history_pos % HISTORY_MAX], sizeof(input) - 1);
                                        input[sizeof(input) - 1] = '\0';
                                        input_len = (int)strlen(input);
                                        cursor_pos = input_len;
                                    }
                                    update_command_suggestion();
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == 'C') // Right Arrow
                            {
                                if (cursor_pos < input_len) cursor_pos++;
                                ui_render(input, cursor_pos, current_suggestion);
                                continue;
                            }
                            else if (seq2 == 'D') // Left Arrow
                            {
                                if (cursor_pos > 0) cursor_pos--;
                                ui_render(input, cursor_pos, current_suggestion);
                                continue;
                            }
                            else if (seq2 == 'H') // Home
                            {
                                cursor_pos = 0;
                                ui_render(input, cursor_pos, current_suggestion);
                                continue;
                            }
                            else if (seq2 == 'F') // End
                            {
                                cursor_pos = input_len;
                                ui_render(input, cursor_pos, current_suggestion);
                                continue;
                            }
                            else if (seq2 == '5') // PageUp (seq: [5~)
                            {
                                if (platform_read_char(&seq3) > 0 && seq3 == '~')
                                {
                                    ui_scroll_up(10);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == '6') // PageDown (seq: [6~)
                            {
                                if (platform_read_char(&seq3) > 0 && seq3 == '~')
                                {
                                    ui_scroll_down(10);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == '3') // Delete (seq: [3~)
                            {
                                if (platform_read_char(&seq3) > 0 && seq3 == '~')
                                {
                                    if (cursor_pos < input_len)
                                    {
                                        memmove(&input[cursor_pos], &input[cursor_pos + 1], input_len - cursor_pos);
                                        input_len--;
                                        input[input_len] = '\0';
                                        update_command_suggestion();
                                        ui_render(input, cursor_pos, current_suggestion);
                                    }
                                }
                                continue;
                            }
                        }
                    }
                }
                continue;
            }

            // Ctrl+L (Redraw screen)
            if (ch == 12)
            {
                ui_resize();
                ui_render(input, cursor_pos, current_suggestion);
                continue;
            }

            // Tab Completion
            if (ch == '\t')
            {
                if (input_len > 0 && input[0] == '/')
                {
                    const char *match = NULL;
                    int matches = 0;
                    for (int i = 0; tab_commands[i] != NULL; ++i)
                    {
                        if (strncmp(tab_commands[i], input, input_len) == 0)
                        {
                            match = tab_commands[i];
                            matches++;
                        }
                    }
                    if (matches == 1 && match)
                    {
                        snprintf(input, sizeof(input), "%s ", match);
                        input_len = (int)strlen(input);
                        cursor_pos = input_len;
                        update_command_suggestion();
                        ui_render(input, cursor_pos, current_suggestion);
                    }
                    else if (matches > 1)
                    {
                        update_command_suggestion();
                        ui_render(input, cursor_pos, current_suggestion);
                    }
                }
                continue;
            }

            // Backspace
            if (ch == 127 || ch == 8)
            {
                if (cursor_pos > 0)
                {
                    memmove(&input[cursor_pos - 1], &input[cursor_pos], input_len - cursor_pos + 1);
                    cursor_pos--;
                    input_len--;
                    input[input_len] = '\0';
                    update_command_suggestion();
                    ui_render(input, cursor_pos, current_suggestion);
                }
                continue;
            }

            // Enter (Send message or execute command)
            if (ch == '\n' || ch == '\r')
            {
                input[input_len] = '\0';
                history_add(input);

                if (strcmp(input, "/exit") == 0)
                {
                    flag = 1;
                    break;
                }

                if (strcmp(input, "/clear") == 0)
                {
                    ui_clear_messages();
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_render(input, cursor_pos, current_suggestion);
                    send(sockfd, "/clear\n", 7, 0);
                    continue;
                }

                if (strncmp(input, "/setcolor", 9) == 0)
                {
                    char col[32] = {0};
                    if (sscanf(input + 9, "%31s", col) == 1)
                    {
                        const char *code = NULL;
                        if (strcasecmp(col, "red") == 0) code = "\033[1;31m";
                        else if (strcasecmp(col, "green") == 0) code = "\033[1;32m";
                        else if (strcasecmp(col, "yellow") == 0) code = "\033[1;33m";
                        else if (strcasecmp(col, "blue") == 0) code = "\033[1;34m";
                        else if (strcasecmp(col, "magenta") == 0) code = "\033[1;35m";
                        else if (strcasecmp(col, "cyan") == 0) code = "\033[1;36m";
                        else if (strcasecmp(col, "white") == 0) code = "\033[1;37m";

                        if (code)
                        {
                            ui_set_user_color(code);
                            char col_msg[128];
                            snprintf(col_msg, sizeof(col_msg), "Color scheme set to %s%s\033[0m", code, col);
                            ui_add_message(MSG_SYSTEM, "Color", col_msg, 0);
                        }
                    }
                }

                if (strncmp(input, "/e2ee ", 6) == 0)
                {
                    char target[32] = {0};
                    char plaintext[BUFFER_SIZE] = {0};
                    if (sscanf(input + 6, "%31s %2047[^\n]", target, plaintext) == 2)
                    {
                        ratchet_session_t *s = e2ee_find_session(target);
                        if (!s || !s->active)
                        {
                            char kreq[128];
                            snprintf(kreq, sizeof(kreq), "__KEYREQ__:%s\n", target);
                            send(sockfd, kreq, strlen(kreq), 0);
                            for (int w = 0; w < 20; w++)
                            {
                                usleep(50000);
                                s = e2ee_find_session(target);
                                if (s && s->active) break;
                            }
                        }

                        char e2ee_packet[4096];
                        if (e2ee_encrypt_message(target, plaintext, e2ee_packet, sizeof(e2ee_packet)) == 0)
                        {
                            char send_buf[sizeof(e2ee_packet) + 32];
                            snprintf(send_buf, sizeof(send_buf), "%s\n", e2ee_packet);
                            send(sockfd, send_buf, strlen(send_buf), 0);
                            ui_add_message(MSG_E2EE, target, plaintext, 0);
                        }
                        else
                        {
                            ui_add_message(MSG_ERROR, "E2EE", "Could not establish Double Ratchet session.", 0);
                        }
                    }
                    else
                    {
                        ui_add_message(MSG_SYSTEM, "Usage", "/e2ee <username> <message>", 0);
                    }
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_scroll_bottom();
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strncmp(input, "/sendfile ", 10) == 0)
                {
                    char target[32] = {0};
                    char filepath[256] = {0};
                    if (sscanf(input + 10, "%31s %255s", target, filepath) == 2)
                    {
                        file_transfer_start_send(target, filepath, sockfd);
                        char msg[512];
                        snprintf(msg, sizeof(msg), "Sent transfer offer for '%s' to %s.", filepath, target);
                        ui_add_message(MSG_FILE, target, msg, 0);
                    }
                    else
                    {
                        ui_add_message(MSG_SYSTEM, "Usage", "/sendfile <username> <filepath>", 0);
                    }
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_scroll_bottom();
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strncmp(input, "/fileaccept ", 12) == 0)
                {
                    int fid = 0;
                    if (sscanf(input + 12, "%d", &fid) == 1)
                    {
                        file_transfer_accept(fid, sockfd);
                        ui_add_message(MSG_FILE, "Me", "Accepted file transfer.", fid);
                    }
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strncmp(input, "/filedecline ", 13) == 0)
                {
                    int fid = 0;
                    if (sscanf(input + 13, "%d", &fid) == 1)
                    {
                        file_transfer_decline(fid, sockfd);
                        ui_add_message(MSG_FILE, "Me", "Declined file transfer.", fid);
                    }
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strcmp(input, "/emoji") == 0)
                {
                    ui_add_message(MSG_SYSTEM, "Emoji", "======[ Emoji Catalog (use /emoji <id>) ]======", 0);
                    char row[256];
                    row[0] = '\0';
                    for (int i = 0; i < EMOJI_COUNT; ++i)
                    {
                        char item[32];
                        snprintf(item, sizeof(item), "%2d. %s  ", i + 1, get_emoji_by_index(i + 1));
                        strncat(row, item, sizeof(row) - strlen(row) - 1);
                        if ((i + 1) % 8 == 0 || i == EMOJI_COUNT - 1)
                        {
                            ui_add_message(MSG_SYSTEM, "Emoji", row, 0);
                            row[0] = '\0';
                        }
                    }
                    ui_add_message(MSG_SYSTEM, "Emoji", "Type /emoji <number> to send an emoji.", 0);
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strncmp(input, "/emoji ", 7) == 0)
                {
                    int num = 0;
                    if (sscanf(input + 7, "%d", &num) == 1)
                    {
                        const char *em = get_emoji_by_index(num);
                        if (em)
                        {
                            char send_buf[INPUT_BUFFER + 4];
                            snprintf(send_buf, sizeof(send_buf), "%s\n", em);
                            send(sockfd, send_buf, strlen(send_buf), 0);
                        }
                        else
                        {
                            ui_add_message(MSG_ERROR, "Emoji", "Invalid emoji index. Type /emoji to view all.", 0);
                        }
                    }
                    input_len = 0;
                    cursor_pos = 0;
                    input[0] = '\0';
                    current_suggestion[0] = '\0';
                    ui_render(input, cursor_pos, current_suggestion);
                    continue;
                }

                if (strncmp(input, "/createparty ", 13) == 0)
                {
                    char pcode[16] = {0};
                    if (sscanf(input + 13, "%15s", pcode) == 1)
                    {
                        ui_set_room(pcode);
                    }
                }

                if (strncmp(input, "/joinparty ", 11) == 0)
                {
                    char pcode[16] = {0};
                    if (sscanf(input + 11, "%15s", pcode) == 1)
                    {
                        ui_set_room(pcode);
                    }
                }

                if (strcmp(input, "/leaveparty") == 0)
                {
                    ui_set_room("0000");
                }

                if (input_len > 0)
                {
                    char send_buf[INPUT_BUFFER + 4];
                    snprintf(send_buf, sizeof(send_buf), "%s\n", input);
                    send(sockfd, send_buf, strlen(send_buf), 0);
                }

                input_len = 0;
                cursor_pos = 0;
                input[0] = '\0';
                current_suggestion[0] = '\0';
                ui_scroll_bottom();
                ui_render(input, cursor_pos, current_suggestion);
                continue;
            }

            // Printable character input
            if (ch >= 32 && ch <= 126)
            {
                if (input_len < INPUT_BUFFER - 4)
                {
                    if (cursor_pos < input_len)
                    {
                        memmove(&input[cursor_pos + 1], &input[cursor_pos], input_len - cursor_pos);
                    }
                    input[cursor_pos++] = ch;
                    input_len++;
                    input[input_len] = '\0';
                    update_command_suggestion();
                    ui_render(input, cursor_pos, current_suggestion);

                    // Send live typing pulse every 3 seconds for non-command typing
                    if (input[0] != '/' && (time(NULL) - last_typing_sent >= 3))
                    {
                        send(sockfd, "__TYPING__\n", 11, 0);
                        last_typing_sent = time(NULL);
                    }
                }
            }
        }
    }

static int client_recv_line(int s, char *buf, size_t max_len)
{
    size_t idx = 0;
    while (idx < max_len - 1)
    {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';
    return (int)idx;
}

int main(int argc, char **argv)
{
    if (net_init() != 0)
    {
        fprintf(stderr, "Network initialization failed\n");
        return EXIT_FAILURE;
    }
    atexit(net_cleanup);

    char host[256] = "";
    char port_str[16] = "9001";

    if (argc < 2)
    {
        printf("\n============================================================\n");
        printf("   KUKUPU CHAT - CLIENT CONNECTION SETUP\n");
        printf("============================================================\n");
        printf("Enter Server Host or IP (default: 127.0.0.1): ");
        fflush(stdout);
        char input_line[256];
        if (fgets(input_line, sizeof(input_line), stdin) == NULL)
        {
            return EXIT_FAILURE;
        }
        str_trim_lf(input_line, sizeof(input_line));
        if (strlen(input_line) == 0)
        {
            strcpy(input_line, "127.0.0.1");
        }

        char *colon = strrchr(input_line, ':');
        if (colon)
        {
            size_t hlen = (size_t)(colon - input_line);
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            strncpy(host, input_line, hlen);
            host[hlen] = '\0';
            strncpy(port_str, colon + 1, sizeof(port_str) - 1);
            port_str[sizeof(port_str) - 1] = '\0';
        }
        else
        {
            strncpy(host, input_line, sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';
            printf("Enter Server Port (default: 9001): ");
            fflush(stdout);
            char port_line[32];
            if (fgets(port_line, sizeof(port_line), stdin) != NULL)
            {
                str_trim_lf(port_line, sizeof(port_line));
                if (strlen(port_line) > 0)
                {
                    strncpy(port_str, port_line, sizeof(port_str) - 1);
                    port_str[sizeof(port_str) - 1] = '\0';
                }
            }
        }
    }
    else
    {
        char *colon = strrchr(argv[1], ':');
        if (colon)
        {
            size_t hlen = (size_t)(colon - argv[1]);
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            strncpy(host, argv[1], hlen);
            host[hlen] = '\0';
            strncpy(port_str, colon + 1, sizeof(port_str) - 1);
            port_str[sizeof(port_str) - 1] = '\0';
        }
        else
        {
            strncpy(host, argv[1], sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';
            if (argc == 3)
            {
                strncpy(port_str, argv[2], sizeof(port_str) - 1);
                port_str[sizeof(port_str) - 1] = '\0';
            }
        }
    }

    signal(SIGINT, catch_ctrl_c_and_exit);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        fprintf(stderr, "Could not resolve server address: %s\n", host);
        return EXIT_FAILURE;
    }

    int connected = 0;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (!IS_VALID_SOCKET(sockfd))
            continue;

        if (connect(sockfd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0)
        {
            connected = 1;
            break;
        }

        close(sockfd);
        sockfd = 0;
    }
    freeaddrinfo(res);

    if (!connected)
    {
        fprintf(stderr, "\n[ERROR] Failed to connect to server at %s:%s\n", host, port_str);
        fprintf(stderr, "  -> Verify that the Kukupu server is running on that machine.\n");
        fprintf(stderr, "  -> If using a tunnel (e.g. Pinggy/Ngrok), ensure the tunnel is active.\n\n");
#ifdef _WIN32
        printf("Press Enter to exit...");
        fflush(stdout);
        getchar();
#endif
        return EXIT_FAILURE;
    }

    int is_registered_user = 0;

    // Interactive Terminal: Launch Full TUI Landing and Authentication Screen
    if (isatty(STDIN_FILENO))
    {
        if (run_login_tui(sockfd, username, sizeof(username), &is_registered_user) != 0)
        {
            close(sockfd);
            return EXIT_SUCCESS;
        }
        ui_set_registered(is_registered_user);
    }
    else
    {
        // Non-interactive fallback for automated testing suites and scripts
        printf("============================================================\n");
        printf("   KUKUPU CHAT - CLIENT INITIALIZATION\n");
        printf("============================================================\n");
        printf("Enter your username: ");
        fflush(stdout);
        if (fgets(username, sizeof(username), stdin) == NULL)
        {
            close(sockfd);
            return EXIT_FAILURE;
        }
        str_trim_lf(username, sizeof(username));

        if (strlen(username) < 2 || strlen(username) >= 32)
        {
            printf("Username must be between 2 and 31 characters.\n");
            close(sockfd);
            return EXIT_FAILURE;
        }

        send(sockfd, username, strlen(username), 0);

        // Administrative Challenge-Response Authentication
        if (strcmp(username, "admin") == 0)
    {
        for (int i = 0; i < MAX_TRIES; i++)
        {
            char res[200];
            int rlen = client_recv_line(sockfd, res, sizeof(res));
            if (rlen <= 0)
            {
                printf("Cannot receive from server!\n");
                close(sockfd);
                return EXIT_FAILURE;
            }
            str_trim_lf(res, sizeof(res));

            if (strcmp(res, "ask") == 0)
            {
                char pwd[64];
                printf("Enter Admin Password: ");
                fflush(stdout);
                if (fgets(pwd, sizeof(pwd), stdin) == NULL)
                {
                    close(sockfd);
                    return EXIT_FAILURE;
                }
                str_trim_lf(pwd, sizeof(pwd));
                send(sockfd, pwd, strlen(pwd), 0);

                char result[200];
                int res_len = client_recv_line(sockfd, result, sizeof(result));
                if (res_len <= 0)
                {
                    printf("Cannot receive from server!\n");
                    close(sockfd);
                    return EXIT_FAILURE;
                }
                str_trim_lf(result, sizeof(result));

                if (strcmp(result, "true") == 0)
                {
                    printf("Enter Joining Alias: ");
                    fflush(stdout);
                    if (fgets(username, sizeof(username), stdin) == NULL)
                    {
                        close(sockfd);
                        return EXIT_FAILURE;
                    }
                    str_trim_lf(username, sizeof(username));
                    send(sockfd, username, strlen(username), 0);
                    break;
                }
                else if (strcmp(result, "try") == 0)
                {
                    printf("\n [!] %d tries left!\n", MAX_TRIES - i - 1);
                    fflush(stdout);
                }
                else
                {
                    printf("\n%s\nexiting..\n", result);
                    close(sockfd);
                    return EXIT_FAILURE;
                }
            }
            else
            {
                printf("\n%s\nexiting..\n", res);
                close(sockfd);
                return EXIT_FAILURE;
            }
        }
        sleep(1);
    }
}

    // Initialize cryptographic Double Ratchet and file transfer engines
    e2ee_init();
    file_transfer_init();

    // Register our Double Ratchet identity public key with server
    char my_pub_hex[128];
    hex_encode(g_e2ee.identity_pub, X25519_KEY_LEN, my_pub_hex, sizeof(my_pub_hex));
    char key_pkt[256];
    snprintf(key_pkt, sizeof(key_pkt), "__IDKEY__:%s\n", my_pub_hex);
    send(sockfd, key_pkt, strlen(key_pkt), 0);

    // Initialize the full-screen terminal TUI interface
    ui_init(username);
    file_transfer_set_progress_callback(ui_set_file_progress);

    enable_raw_mode();

    pthread_t recv_thread, file_thread;
    pthread_create(&recv_thread, NULL, recv_msg_handler, NULL);
    pthread_create(&file_thread, NULL, file_ticker_thread, NULL);

    // Query active members to populate sidebar
    send(sockfd, "/users\n", 7, 0);

    ui_render(input, 0, NULL);

    chat_loop();

    disable_raw_mode();
    ui_shutdown();

    file_transfer_cleanup();
    e2ee_cleanup();
    close(sockfd);
    return EXIT_SUCCESS;
}
