// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>
#include "common.h"
#include "emoji.h"
#include "e2ee.h"
#include "file_transfer.h"
#include "crypto_utils.h"
#include "ui.h"

#define INPUT_BUFFER 1024
#define LENGTH 4096
#define HISTORY_MAX 50
#define MAX_TRIES 5

static char input[INPUT_BUFFER];
static int input_len = 0;
static int cursor_pos = 0;
static char current_suggestion[128] = {0};

static struct termios orig_termios;
static volatile sig_atomic_t flag = 0;
static int sockfd = 0;
static char username[32];

static char history[HISTORY_MAX][INPUT_BUFFER];
static int history_count = 0;
static int history_pos = 0;

static const char *tab_commands[] = {
    "/help", "/msg", "/e2ee", "/sendfile", "/fileaccept", "/filedecline",
    "/whoami", "/users", "/ping", "/time", "/uptime", "/motd", "/clear",
    "/lastseen", "/setcolor", "/colorlist", "/createparty", "/joinparty",
    "/party", "/leaveparty", "/guessgame", "/guess", "/vanish", "/emoji",
    "/exit", "/kick", "/ban", "/unban", "/banlist", "/mute", "/unmute",
    "/mutelist", "/setadminpwd", "/broadcast", "/pin", "/setmotd", "/log",
    "/shutdown", "/parties", "/lockparty", "/invite", "/partyinfo", "/whois",
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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
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
            ui_add_message(MSG_NORMAL, sender, body, 0);
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

    if (strstr(clean, "Online users:"))
    {
        char *ptr = strstr(clean, "Online users:");
        if (ptr) ui_set_users_from_string(ptr + 13);
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
    fd_set readfds;
    unsigned char ch;
    time_t last_typing_sent = 0;

    while (!flag)
    {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;

        int sel = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (sel <= 0)
            continue;

        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            if (read(STDIN_FILENO, &ch, 1) <= 0)
                continue;

            // Handle Escape sequences (Arrows, PageUp, PageDown)
            if (ch == 27)
            {
                unsigned char seq1 = 0, seq2 = 0, seq3 = 0;
                if (read(STDIN_FILENO, &seq1, 1) > 0)
                {
                    if (seq1 == '[')
                    {
                        if (read(STDIN_FILENO, &seq2, 1) > 0)
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
                                if (read(STDIN_FILENO, &seq3, 1) > 0 && seq3 == '~')
                                {
                                    ui_scroll_up(10);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == '6') // PageDown (seq: [6~)
                            {
                                if (read(STDIN_FILENO, &seq3, 1) > 0 && seq3 == '~')
                                {
                                    ui_scroll_down(10);
                                    ui_render(input, cursor_pos, current_suggestion);
                                }
                                continue;
                            }
                            else if (seq2 == '3') // Delete (seq: [3~)
                            {
                                if (read(STDIN_FILENO, &seq3, 1) > 0 && seq3 == '~')
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
                    continue;
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
    if (argc != 2)
    {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip = argv[1];
    int port = 9001;

    signal(SIGINT, catch_ctrl_c_and_exit);
    signal(SIGPIPE, SIG_IGN);

    printf("============================================================\n");
    printf("   KUKUPU CHAT - CLIENT INITIALIZATION\n");
    printf("============================================================\n");
    printf("Enter your username: ");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL)
    {
        return EXIT_FAILURE;
    }
    str_trim_lf(username, sizeof(username));

    if (strlen(username) < 2 || strlen(username) >= 32)
    {
        printf("Username must be between 2 and 31 characters.\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connect error");
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
