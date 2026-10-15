#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <ctype.h>

#define MAX_UI_MESSAGES 600
#define MAX_RENDER_LINES 1200
#define MAX_SIDEBAR_USERS 64
#define FRAME_BUF_SIZE 262144

typedef struct {
    ui_msg_type_t type;
    char sender[32];
    char text[1024];
    time_t timestamp;
    int msg_id;
    int vanished;
} ui_message_t;

typedef struct {
    char plain[512];
    char color[1024];
    int plain_len;
} render_line_t;

static pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;

static ui_message_t messages[MAX_UI_MESSAGES];
static int msg_count = 0;
static int scroll_offset = 0;

static char my_username[32] = "user";
static char current_room[16] = "0000";
static int e2ee_active = 1;

static char typing_peer[32] = {0};
static time_t typing_peer_time = 0;

static char file_progress_name[128] = {0};
static int file_progress_cur = 0;
static int file_progress_tot = 0;

static char sidebar_users[MAX_SIDEBAR_USERS][32];
static int sidebar_user_count = 0;

static int term_cols = 80;
static int term_rows = 24;

static void update_term_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
    {
        term_cols = ws.ws_col;
        term_rows = ws.ws_row;
    }
    else
    {
        term_cols = 80;
        term_rows = 24;
    }
    if (term_cols < 40) term_cols = 40;
    if (term_rows < 10) term_rows = 10;
}

static void sigwinch_handler(int sig)
{
    (void)sig;
    pthread_mutex_lock(&ui_lock);
    update_term_size();
    pthread_mutex_unlock(&ui_lock);
}

void ui_init(const char *username)
{
    pthread_mutex_lock(&ui_lock);

    if (username && strlen(username) > 0)
    {
        strncpy(my_username, username, sizeof(my_username) - 1);
        my_username[sizeof(my_username) - 1] = '\0';
    }

    update_term_size();

    // Enter alternate screen buffer, clear screen, hide cursor during setup
    ssize_t w = write(STDOUT_FILENO, "\033[?1049h\033[2J\033[H\033[?25h", 18);
    (void)w;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);

    // Initial system banner
    messages[0].type = MSG_SYSTEM;
    strncpy(messages[0].sender, "System", 31);
    strncpy(messages[0].text, "Connected to Kukupu Chat.", 1023);
    messages[0].timestamp = time(NULL);
    messages[0].msg_id = 0;

    messages[1].type = MSG_SYSTEM;
    strncpy(messages[1].sender, "System", 31);
    strncpy(messages[1].text, "Signal Double Ratchet E2EE active. Type /help for command catalog.", 1023);
    messages[1].timestamp = time(NULL);
    messages[1].msg_id = 0;
    msg_count = 2;

    // Add self to sidebar users
    snprintf(sidebar_users[0], sizeof(sidebar_users[0]), "%s", my_username);
    sidebar_user_count = 1;

    pthread_mutex_unlock(&ui_lock);
}

void ui_shutdown(void)
{
    pthread_mutex_lock(&ui_lock);
    // Leave alternate screen buffer, restore cursor, clear attributes
    ssize_t w = write(STDOUT_FILENO, "\033[0m\033[?1049l\033[?25h\n", 18);
    (void)w;
    pthread_mutex_unlock(&ui_lock);
}

void ui_resize(void)
{
    pthread_mutex_lock(&ui_lock);
    update_term_size();
    pthread_mutex_unlock(&ui_lock);
}

void ui_add_message(ui_msg_type_t type, const char *sender, const char *text, int msg_id)
{
    if (!text || strlen(text) == 0) return;

    pthread_mutex_lock(&ui_lock);

    int idx = msg_count % MAX_UI_MESSAGES;
    messages[idx].type = type;
    messages[idx].timestamp = time(NULL);
    messages[idx].msg_id = msg_id;
    messages[idx].vanished = 0;

    if (sender)
    {
        strncpy(messages[idx].sender, sender, sizeof(messages[idx].sender) - 1);
        messages[idx].sender[sizeof(messages[idx].sender) - 1] = '\0';
    }
    else
    {
        messages[idx].sender[0] = '\0';
    }

    strncpy(messages[idx].text, text, sizeof(messages[idx].text) - 1);
    messages[idx].text[sizeof(messages[idx].text) - 1] = '\0';

    msg_count++;

    // If viewing latest messages, stick to bottom
    if (scroll_offset > 0)
    {
        scroll_offset++;
    }

    pthread_mutex_unlock(&ui_lock);
}

void ui_edit_message(int msg_id, const char *new_text)
{
    if (!new_text || msg_id <= 0) return;

    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < MAX_UI_MESSAGES && i < msg_count; ++i)
    {
        if (messages[i].msg_id == msg_id)
        {
            strncpy(messages[i].text, new_text, sizeof(messages[i].text) - 1);
            messages[i].text[sizeof(messages[i].text) - 1] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_delete_message(int msg_id)
{
    if (msg_id <= 0) return;

    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < MAX_UI_MESSAGES && i < msg_count; ++i)
    {
        if (messages[i].msg_id == msg_id)
        {
            messages[i].vanished = 1;
            strncpy(messages[i].text, "[Message vanished]", sizeof(messages[i].text) - 1);
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_clear_messages(void)
{
    pthread_mutex_lock(&ui_lock);
    msg_count = 0;
    scroll_offset = 0;
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_room(const char *room)
{
    if (!room) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(current_room, room, sizeof(current_room) - 1);
    current_room[sizeof(current_room) - 1] = '\0';
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_typing(const char *username)
{
    if (!username) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(typing_peer, username, sizeof(typing_peer) - 1);
    typing_peer[sizeof(typing_peer) - 1] = '\0';
    typing_peer_time = time(NULL);
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_e2ee(int active)
{
    pthread_mutex_lock(&ui_lock);
    e2ee_active = active;
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_file_progress(const char *filename, int cur, int tot)
{
    pthread_mutex_lock(&ui_lock);
    if (filename)
    {
        strncpy(file_progress_name, filename, sizeof(file_progress_name) - 1);
        file_progress_name[sizeof(file_progress_name) - 1] = '\0';
    }
    file_progress_cur = cur;
    file_progress_tot = tot;
    pthread_mutex_unlock(&ui_lock);
}

void ui_clear_file_progress(void)
{
    pthread_mutex_lock(&ui_lock);
    file_progress_name[0] = '\0';
    file_progress_cur = 0;
    file_progress_tot = 0;
    pthread_mutex_unlock(&ui_lock);
}

void ui_add_user(const char *username)
{
    if (!username || strlen(username) == 0) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < sidebar_user_count; ++i)
    {
        if (strcmp(sidebar_users[i], username) == 0)
        {
            pthread_mutex_unlock(&ui_lock);
            return;
        }
    }
    if (sidebar_user_count < MAX_SIDEBAR_USERS)
    {
        strncpy(sidebar_users[sidebar_user_count], username, 31);
        sidebar_users[sidebar_user_count][31] = '\0';
        sidebar_user_count++;
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_remove_user(const char *username)
{
    if (!username || strlen(username) == 0) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < sidebar_user_count; ++i)
    {
        if (strcmp(sidebar_users[i], username) == 0)
        {
            for (int j = i; j < sidebar_user_count - 1; ++j)
            {
                strcpy(sidebar_users[j], sidebar_users[j + 1]);
            }
            sidebar_user_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_clear_users(void)
{
    pthread_mutex_lock(&ui_lock);
    sidebar_user_count = 0;
    snprintf(sidebar_users[0], sizeof(sidebar_users[0]), "%s", my_username);
    sidebar_user_count = 1;
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_users_from_string(const char *raw_list)
{
    if (!raw_list) return;
    pthread_mutex_lock(&ui_lock);
    sidebar_user_count = 0;
    snprintf(sidebar_users[0], sizeof(sidebar_users[0]), "%s", my_username);
    sidebar_user_count = 1;

    char copy[512];
    strncpy(copy, raw_list, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *token = strtok(copy, ", \t\n\r");
    while (token != NULL)
    {
        if (strlen(token) > 0 && strcmp(token, my_username) != 0 && sidebar_user_count < MAX_SIDEBAR_USERS)
        {
            int exists = 0;
            for (int i = 0; i < sidebar_user_count; ++i)
            {
                if (strcmp(sidebar_users[i], token) == 0)
                {
                    exists = 1;
                    break;
                }
            }
            if (!exists)
            {
                snprintf(sidebar_users[sidebar_user_count], sizeof(sidebar_users[0]), "%s", token);
                sidebar_user_count++;
            }
        }
        token = strtok(NULL, ", \t\n\r");
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_scroll_up(int lines)
{
    pthread_mutex_lock(&ui_lock);
    scroll_offset += lines;
    if (scroll_offset > msg_count) scroll_offset = msg_count;
    pthread_mutex_unlock(&ui_lock);
}

void ui_scroll_down(int lines)
{
    pthread_mutex_lock(&ui_lock);
    scroll_offset -= lines;
    if (scroll_offset < 0) scroll_offset = 0;
    pthread_mutex_unlock(&ui_lock);
}

void ui_scroll_bottom(void)
{
    pthread_mutex_lock(&ui_lock);
    scroll_offset = 0;
    pthread_mutex_unlock(&ui_lock);
}

// Word-wraps a single message into visual render lines
static int wrap_message(const ui_message_t *msg, int max_width, render_line_t *out_lines, int max_lines)
{
    if (!msg || max_width <= 10) return 0;

    char prefix_plain[128] = {0};
    char prefix_color[256] = {0};

    char tstr[16];
    struct tm *tm_info = localtime(&msg->timestamp);
    strftime(tstr, sizeof(tstr), "%H:%M", tm_info);

    switch (msg->type)
    {
        case MSG_SELF:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [YOU] > ", tstr);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;32m[YOU]\033[0m \033[1;37m>\033[0m ", tstr);
            break;
        case MSG_NORMAL:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] <%s> ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;36m<%s>\033[0m ", tstr, msg->sender);
            break;
        case MSG_E2EE:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [E2EE:%s] > ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;35m[E2EE:%s]\033[0m \033[1;37m>\033[0m ", tstr, msg->sender);
            break;
        case MSG_PRIVATE:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [WHISPER:%s] > ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;33m[WHISPER:%s]\033[0m \033[33m>\033[0m ", tstr, msg->sender);
            break;
        case MSG_FILE:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [FILE:%s] ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;34m[FILE:%s]\033[0m ", tstr, msg->sender);
            break;
        case MSG_VANISH:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [VANISH:%s] ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;31m[VANISH:%s]\033[0m ", tstr, msg->sender);
            break;
        case MSG_ERROR:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] [!] ERROR: ", tstr);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;31m[!] ERROR:\033[0m ", tstr);
            break;
        case MSG_SYSTEM:
        default:
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] --- [SYS] ", tstr);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m \033[1;33m--- [SYS]\033[0m ", tstr);
            break;
    }

    int prefix_len = (int)strlen(prefix_plain);
    int first_line_budget = max_width - prefix_len;
    if (first_line_budget < 5) first_line_budget = 5;

    int indent_len = prefix_len;
    if (indent_len > 18) indent_len = 18;
    int next_line_budget = max_width - indent_len;
    if (next_line_budget < 5) next_line_budget = 5;

    char indent_spaces[32];
    memset(indent_spaces, ' ', indent_len);
    indent_spaces[indent_len] = '\0';

    const char *p = msg->text;
    int line_idx = 0;

    while (*p && line_idx < max_lines)
    {
        int budget = (line_idx == 0) ? first_line_budget : next_line_budget;

        // Skip leading whitespace on subsequent lines
        if (line_idx > 0)
        {
            while (*p == ' ') p++;
        }
        if (!*p) break;

        int rem_len = (int)strlen(p);
        int take = rem_len;

        if (rem_len > budget)
        {
            // Find word boundary backwards from budget
            take = budget;
            while (take > 0 && p[take] != ' ')
            {
                take--;
            }
            if (take == 0)
            {
                take = budget; // Hard break if word is longer than budget
            }
        }

        char chunk[256];
        if (take > 250) take = 250;
        strncpy(chunk, p, take);
        chunk[take] = '\0';

        if (line_idx == 0)
        {
            snprintf(out_lines[line_idx].plain, sizeof(out_lines[line_idx].plain), "%s%s", prefix_plain, chunk);
            snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s\033[37m%s\033[0m", prefix_color, chunk);
            out_lines[line_idx].plain_len = prefix_len + take;
        }
        else
        {
            snprintf(out_lines[line_idx].plain, sizeof(out_lines[line_idx].plain), "%s%s", indent_spaces, chunk);
            snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s\033[37m%s\033[0m", indent_spaces, chunk);
            out_lines[line_idx].plain_len = indent_len + take;
        }

        line_idx++;
        p += take;
        if (*p == ' ') p++;
    }

    return line_idx;
}

void ui_render(const char *input_buf, int cursor_pos, const char *suggestion)
{
    pthread_mutex_lock(&ui_lock);

    update_term_size();

    int cols = term_cols;
    int rows = term_rows;

    // Sidebar dimensions
    int show_sidebar = (cols >= 80);
    int sidebar_width = show_sidebar ? 22 : 0;
    int chat_width = show_sidebar ? (cols - sidebar_width - 3) : (cols - 2);

    int chat_rows = rows - 5; // Rows for message area
    if (chat_rows < 3) chat_rows = 3;

    // 1. Collect and wrap visual lines from messages
    render_line_t all_lines[MAX_RENDER_LINES];
    int total_lines = 0;

    int start_m = (msg_count > MAX_UI_MESSAGES) ? (msg_count - MAX_UI_MESSAGES) : 0;
    for (int m = start_m; m < msg_count && total_lines < MAX_RENDER_LINES - 20; ++m)
    {
        const ui_message_t *msg = &messages[m % MAX_UI_MESSAGES];
        render_line_t wrapped[16];
        int n = wrap_message(msg, chat_width - 1, wrapped, 16);
        for (int i = 0; i < n && total_lines < MAX_RENDER_LINES; ++i)
        {
            all_lines[total_lines++] = wrapped[i];
        }
    }

    // Scroll calculations
    int display_start = total_lines - chat_rows - scroll_offset;
    if (display_start < 0) display_start = 0;

    // Buffer for entire frame to write atomically (0 flicker)
    static char frame[FRAME_BUF_SIZE];
    int fidx = 0;

    // Reset cursor to top-left
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[H");

    // -------------------------------------------------------------
    // ROW 1: HEADER BAR
    // -------------------------------------------------------------
    char head_left[64] = "┌── [KUKUPU CHAT] ";
    char head_mid[128];
    snprintf(head_mid, sizeof(head_mid), "── Room: #%s ── [E2EE: %s] ",
             current_room, e2ee_active ? "ACTIVE" : "OFF");
    char head_right[64];
    snprintf(head_right, sizeof(head_right), "── User: @%s ──┐", my_username);

    int visual_header_len = (int)(strlen(head_left) + strlen(head_mid) + strlen(head_right));
    int head_dash = cols - visual_header_len;
    if (head_dash < 0) head_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;36m%s\033[0m", head_left);
    for (int i = 0; i < head_dash / 2; ++i)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;33m── Room: #%s\033[0m \033[38;5;240m──\033[0m \033[1;32m[E2EE: %s]\033[0m ",
                     current_room, e2ee_active ? "ACTIVE" : "OFF");
    for (int i = 0; i < head_dash - (head_dash / 2); ++i)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;35m── User: @%s\033[0m\033[1;36m ──┐\033[0m\n", my_username);

    // -------------------------------------------------------------
    // ROWS 2 .. (rows - 4): CHAT VIEWPORT + SIDEBAR
    // -------------------------------------------------------------
    for (int r = 0; r < chat_rows; ++r)
    {
        int line_idx = display_start + r;
        char chat_content[512] = {0};
        int plain_len = 0;

        if (line_idx < total_lines)
        {
            snprintf(chat_content, sizeof(chat_content), "%s", all_lines[line_idx].color);
            plain_len = all_lines[line_idx].plain_len;
        }

        // Padding spaces for chat width
        int pad = chat_width - plain_len;
        if (pad < 0) pad = 0;

        // Render chat line
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m│\033[0m %s", chat_content);
        for (int p = 0; p < pad; ++p)
        {
            if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
        }

        // Render sidebar if wide terminal
        if (show_sidebar)
        {
            char side_text[128] = {0};
            if (r == 0)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;36m[MEMBERS (%d)]\033[0m", sidebar_user_count);
            }
            else if (r <= sidebar_user_count)
            {
                const char *u = sidebar_users[r - 1];
                if (strcmp(u, my_username) == 0)
                    snprintf(side_text, sizeof(side_text), "\033[1;32m* %s (you)\033[0m", u);
                else
                    snprintf(side_text, sizeof(side_text), "\033[37m* %s\033[0m", u);
            }
            else if (r == sidebar_user_count + 1)
            {
                snprintf(side_text, sizeof(side_text), "\033[38;5;240m────────────────────\033[0m");
            }
            else if (r == sidebar_user_count + 2)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;33m[CHANNEL #%s]\033[0m", current_room);
            }
            else if (r == sidebar_user_count + 3)
            {
                snprintf(side_text, sizeof(side_text), "\033[2mMode: Public\033[0m");
            }

            // Calculate visible plain length for sidebar text
            int side_plain_len = 0;
            const char *sp = side_text;
            int in_esc = 0;
            while (*sp)
            {
                if (*sp == '\033') in_esc = 1;
                else if (in_esc && *sp == 'm') in_esc = 0;
                else if (!in_esc) side_plain_len++;
                sp++;
            }

            int side_pad = sidebar_width - 2 - side_plain_len;
            if (side_pad < 0) side_pad = 0;

            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                             "\033[38;5;240m│\033[0m %s", side_text);
            for (int sp = 0; sp < side_pad; ++sp)
            {
                if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
            }
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m│\033[0m\n");
        }
        else
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m│\033[0m\n");
        }
    }

    // -------------------------------------------------------------
    // ROW (rows - 3): STATUS / TYPING / PROGRESS DIVIDER
    // -------------------------------------------------------------
    char stat_content[256];
    time_t now = time(NULL);

    if (file_progress_tot > 0)
    {
        int pct = (file_progress_cur * 100) / file_progress_tot;
        if (pct > 100) pct = 100;
        int bar_len = 20;
        int filled = bar_len * pct / 100;
        char bar[32];
        for (int b = 0; b < bar_len; ++b)
        {
            if (b < filled) bar[b] = '=';
            else if (b == filled) bar[b] = '>';
            else bar[b] = ' ';
        }
        bar[bar_len] = '\0';
        snprintf(stat_content, sizeof(stat_content),
                 "File Xfer: %s [%s] %d%% (%d/%d chunks)",
                 file_progress_name, bar, pct, file_progress_cur, file_progress_tot);
    }
    else if (now - typing_peer_time < 3 && strlen(typing_peer) > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "Typing: %s is typing a message...", typing_peer);
    }
    else if (scroll_offset > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "SCROLLBACK MODE (+%d lines) • Press [PgDn] or type to return", scroll_offset);
    }
    else if (suggestion && strlen(suggestion) > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "Suggestions: %s", suggestion);
    }
    else
    {
        snprintf(stat_content, sizeof(stat_content), "Online • Room #%s • Signal Double Ratchet Ready", current_room);
    }

    int stat_len = (int)strlen(stat_content);
    int stat_dash = cols - stat_len - 14;
    if (stat_dash < 0) stat_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[38;5;240m├─\033[0m \033[1;33m[STATUS]\033[0m \033[37m%s\033[0m ", stat_content);
    for (int d = 0; d < stat_dash; ++d)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m┤\033[0m\n");

    // -------------------------------------------------------------
    // ROW (rows - 2): INPUT BOX TOP BORDER
    // -------------------------------------------------------------
    char in_head[64];
    snprintf(in_head, sizeof(in_head), "╭─ Message #%s ", current_room);
    int in_head_len = (int)strlen(in_head);
    int in_dash = cols - in_head_len - 1;
    if (in_dash < 0) in_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;36m%s\033[0m", in_head);
    for (int d = 0; d < in_dash; ++d)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m╮\033[0m\n");

    // -------------------------------------------------------------
    // ROW (rows - 1): INPUT TEXT LINE
    // -------------------------------------------------------------
    const char *in_text = input_buf ? input_buf : "";
    int in_len = (int)strlen(in_text);
    int in_space_pad = cols - in_len - 5;
    if (in_space_pad < 0) in_space_pad = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;36m│\033[0m \033[1;32m>\033[0m \033[1;37m%s\033[0m", in_text);
    for (int s = 0; s < in_space_pad; ++s)
    {
        if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m│\033[0m\n");

    // -------------------------------------------------------------
    // ROW (rows): INPUT BOX BOTTOM & HOTKEY GUIDE
    // -------------------------------------------------------------
    char in_foot[128] = "╰─ [Tab] Autocomplete  •  [PgUp/PgDn] Scroll  •  [/exit] Quit ";
    int in_foot_len = (int)strlen(in_foot);
    int in_foot_dash = cols - in_foot_len - 1;
    if (in_foot_dash < 0) in_foot_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                     "\033[1;36m%s\033[0m", in_foot);
    for (int d = 0; d < in_foot_dash; ++d)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m╯\033[0m");

    // Reposition cursor at input line (Row: rows - 1, Col: 5 + cursor_pos)
    int target_col = 5 + cursor_pos;
    if (target_col > cols - 2) target_col = cols - 2;
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[%d;%dH\033[?25h", rows - 1, target_col);

    // Atomically write entire frame
    ssize_t w = write(STDOUT_FILENO, frame, fidx);
    (void)w;

    pthread_mutex_unlock(&ui_lock);
}
