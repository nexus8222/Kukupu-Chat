#include "ui.h"
#include "compat.h"
#include "emoji.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>

#define MAX_UI_MESSAGES 600
#define MAX_RENDER_LINES 1200
#define MAX_SIDEBAR_USERS 64
#define MAX_JOINED_PARTIES 8
#define MAX_UI_NOTIFS 16
#define FRAME_BUF_SIZE 262144

typedef struct {
    ui_msg_type_t type;
    char sender[32];
    char sender_color[32];
    char text[1024];
    time_t timestamp;
    int msg_id;
    int vanished;
    char room[32];
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
static char my_profile_name[32] = {0};
static char my_user_color[32] = "\033[1;32m"; // Default green
static int my_is_registered = 0;
static char current_room[16] = "0000";
static int e2ee_active = 1;

static char typing_peer[32] = {0};
static time_t typing_peer_time = 0;

static char file_progress_name[128] = {0};
static int file_progress_cur = 0;
static int file_progress_tot = 0;

static char sidebar_users[MAX_SIDEBAR_USERS][32];
static int sidebar_user_count = 0;

static char joined_parties[MAX_JOINED_PARTIES][16];
static int joined_party_count = 0;

#define MAX_PM_CONTACTS 16
static char pm_contacts[MAX_PM_CONTACTS][32];
static int pm_contact_unread[MAX_PM_CONTACTS];
static int pm_contact_count = 0;

static char toast_text[128] = {0};
static time_t toast_expiry = 0;

static char modal_input_buf[64] = {0};
static int modal_input_len = 0;
static int party_modal_tab = 0; // 0: select, 1: join, 2: create
static int party_create_privacy = 0; // 0: Open, 1: Approval required

static ui_notif_t notifications[MAX_UI_NOTIFS];
static int notif_count = 0;
static int next_notif_id = 1;

static ui_modal_t active_modal = UI_MODAL_NONE;
static char modal_target_user[32] = {0};
static int emoji_picker_page = 0;

static int term_cols = 80;
static int term_rows = 24;

// Forward declaration
static int visual_str_len(const char *s);

static int visual_str_len(const char *s)
{
    if (!s) return 0;
    int len = 0;
    int in_esc = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p)
    {
        if (*p == '\033')
        {
            in_esc = 1;
            p++;
        }
        else if (in_esc)
        {
            if (*p == 'm' || *p == 'H' || *p == 'J' || *p == 'h' || *p == 'l')
                in_esc = 0;
            p++;
        }
        else
        {
            if ((*p & 0x80) == 0)
            {
                len += 1;
                p++;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                len += 1;
                p += (p[1] ? 2 : 1);
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                len += 1;
                p += (p[1] && p[2] ? 3 : (p[1] ? 2 : 1));
            }
            else if ((*p & 0xF8) == 0xF0)
            {
                len += 2; // Emoji visual width is 2 terminal columns
                p += (p[1] && p[2] && p[3] ? 4 : (p[1] && p[2] ? 3 : (p[1] ? 2 : 1)));
            }
            else
            {
                p++;
            }
        }
    }
    return len;
}

static void update_term_size(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top;
    }
    else
    {
        term_cols = 80;
        term_rows = 24;
    }
#else
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
#endif
    if (term_cols < 40) term_cols = 40;
    if (term_rows < 12) term_rows = 12;
}

#ifndef _WIN32
static void sigwinch_handler(int sig)
{
    (void)sig;
    pthread_mutex_lock(&ui_lock);
    update_term_size();
    pthread_mutex_unlock(&ui_lock);
}
#endif

void ui_init(const char *username)
{
    pthread_mutex_lock(&ui_lock);

    if (username && strlen(username) > 0)
    {
        strncpy(my_username, username, sizeof(my_username) - 1);
        my_username[sizeof(my_username) - 1] = '\0';
    }

    update_term_size();

    // Alternate screen buffer, disable line wrap, clear screen, hide cursor during setup,
    // and enable SGR 1006 mouse tracking
    const char *init_seq = "\033[?1049h\033[?7l\033[2J\033[H\033[?25l\033[?1000h\033[?1006h";
    platform_write_console(init_seq, (int)strlen(init_seq));

#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
#endif

    // Initial system banner
    messages[0].type = MSG_SYSTEM;
    strncpy(messages[0].sender, "System", 31);
    strncpy(messages[0].text, "Connected to Kukupu Chat. Interactive TUI with Mouse enabled.", 1023);
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
    // Disable mouse tracking, restore line wrap, leave alternate screen buffer, restore cursor, clear attributes
    const char *shut_seq = "\033[?1006l\033[?1000l\033[0m\033[?7h\033[?1049l\033[?25h\n";
    platform_write_console(shut_seq, (int)strlen(shut_seq));
    pthread_mutex_unlock(&ui_lock);
}

void ui_resize(void)
{
    pthread_mutex_lock(&ui_lock);
    update_term_size();
    pthread_mutex_unlock(&ui_lock);
}

void ui_set_profile_name(const char *name)
{
    if (!name) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(my_profile_name, name, sizeof(my_profile_name) - 1);
    my_profile_name[sizeof(my_profile_name) - 1] = '\0';
    pthread_mutex_unlock(&ui_lock);
}

const char *ui_get_profile_name(void)
{
    return my_profile_name[0] ? my_profile_name : my_username;
}

void ui_add_message_to_room(ui_msg_type_t type, const char *sender, const char *text, int msg_id, const char *color, const char *room)
{
    if (!text || strlen(text) == 0) return;

    pthread_mutex_lock(&ui_lock);

    int idx = msg_count % MAX_UI_MESSAGES;
    messages[idx].type = type;
    messages[idx].timestamp = time(NULL);
    messages[idx].msg_id = msg_id;
    messages[idx].vanished = 0;

    if (room && strlen(room) > 0)
    {
        strncpy(messages[idx].room, room, sizeof(messages[idx].room) - 1);
        messages[idx].room[sizeof(messages[idx].room) - 1] = '\0';
    }
    else
    {
        messages[idx].room[0] = '\0';
    }

    if (color && strlen(color) > 0)
    {
        strncpy(messages[idx].sender_color, color, sizeof(messages[idx].sender_color) - 1);
        messages[idx].sender_color[sizeof(messages[idx].sender_color) - 1] = '\0';
    }
    else
    {
        messages[idx].sender_color[0] = '\0';
    }

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

    if (scroll_offset > 0)
    {
        scroll_offset++;
    }

    pthread_mutex_unlock(&ui_lock);
}

void ui_add_message_with_color(ui_msg_type_t type, const char *sender, const char *text, int msg_id, const char *color)
{
    ui_add_message_to_room(type, sender, text, msg_id, color, current_room);
}

void ui_add_message(ui_msg_type_t type, const char *sender, const char *text, int msg_id)
{
    ui_add_message_with_color(type, sender, text, msg_id, NULL);
}

void ui_add_pm_contact(const char *username)
{
    if (!username || strlen(username) == 0 || strcmp(username, my_username) == 0) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            pthread_mutex_unlock(&ui_lock);
            return;
        }
    }
    if (pm_contact_count < MAX_PM_CONTACTS)
    {
        strncpy(pm_contacts[pm_contact_count], username, sizeof(pm_contacts[0]) - 1);
        pm_contacts[pm_contact_count][sizeof(pm_contacts[0]) - 1] = '\0';
        pm_contact_unread[pm_contact_count] = 0;
        pm_contact_count++;
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_remove_pm_contact(const char *username)
{
    if (!username) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            for (int j = i; j < pm_contact_count - 1; ++j)
            {
                strcpy(pm_contacts[j], pm_contacts[j + 1]);
                pm_contact_unread[j] = pm_contact_unread[j + 1];
            }
            pm_contact_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

int ui_has_pm_contact(const char *username)
{
    if (!username) return 0;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            pthread_mutex_unlock(&ui_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&ui_lock);
    return 0;
}

int ui_get_pm_contact_count(void)
{
    return pm_contact_count;
}

const char *ui_get_pm_contact(int idx)
{
    if (idx < 0 || idx >= pm_contact_count) return NULL;
    return pm_contacts[idx];
}

void ui_inc_pm_unread(const char *username)
{
    if (!username) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            pm_contact_unread[i]++;
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

int ui_get_pm_unread(const char *username)
{
    if (!username) return 0;
    pthread_mutex_lock(&ui_lock);
    int unread = 0;
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            unread = pm_contact_unread[i];
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
    return unread;
}

void ui_clear_pm_unread(const char *username)
{
    if (!username) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < pm_contact_count; ++i)
    {
        if (strcmp(pm_contacts[i], username) == 0)
        {
            pm_contact_unread[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_trigger_toast(const char *text, int sound_type)
{
    if (!text) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(toast_text, text, sizeof(toast_text) - 1);
    toast_text[sizeof(toast_text) - 1] = '\0';
    toast_expiry = time(NULL) + 4;
    pthread_mutex_unlock(&ui_lock);

    platform_play_sound(sound_type);
}

void ui_modal_input_char(char ch)
{
    pthread_mutex_lock(&ui_lock);
    if (modal_input_len < (int)sizeof(modal_input_buf) - 2)
    {
        modal_input_buf[modal_input_len++] = ch;
        modal_input_buf[modal_input_len] = '\0';
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_modal_input_backspace(void)
{
    pthread_mutex_lock(&ui_lock);
    if (modal_input_len > 0)
    {
        modal_input_len--;
        modal_input_buf[modal_input_len] = '\0';
    }
    pthread_mutex_unlock(&ui_lock);
}

const char *ui_modal_get_input(void)
{
    return modal_input_buf;
}

void ui_modal_clear_input(void)
{
    pthread_mutex_lock(&ui_lock);
    modal_input_buf[0] = '\0';
    modal_input_len = 0;
    pthread_mutex_unlock(&ui_lock);
}

int ui_modal_is_input_active(void)
{
    if (active_modal == UI_MODAL_EDIT_NAME || active_modal == UI_MODAL_CHANGE_PWD)
        return 1;
    if (active_modal == UI_MODAL_PARTY_JOIN_CREATE && (party_modal_tab == 1 || party_modal_tab == 2))
        return 1;
    return 0;
}

void ui_party_modal_toggle_privacy(void)
{
    pthread_mutex_lock(&ui_lock);
    party_create_privacy = !party_create_privacy;
    pthread_mutex_unlock(&ui_lock);
}

int ui_party_modal_get_privacy(void)
{
    return party_create_privacy;
}

void ui_set_party_modal_tab(int tab)
{
    pthread_mutex_lock(&ui_lock);
    party_modal_tab = tab;
    modal_input_buf[0] = '\0';
    modal_input_len = 0;
    pthread_mutex_unlock(&ui_lock);
}

int ui_get_party_modal_tab(void)
{
    return party_modal_tab;
}

void ui_set_user_color(const char *color_code)
{
    if (!color_code) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(my_user_color, color_code, sizeof(my_user_color) - 1);
    my_user_color[sizeof(my_user_color) - 1] = '\0';
    pthread_mutex_unlock(&ui_lock);
}

const char *ui_get_user_color(void)
{
    return my_user_color;
}

void ui_set_registered(int is_registered)
{
    pthread_mutex_lock(&ui_lock);
    my_is_registered = is_registered;
    pthread_mutex_unlock(&ui_lock);
}

int ui_is_registered(void)
{
    return my_is_registered;
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
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_edit_last_message(const char *sender, const char *new_text)
{
    if (!sender || !new_text) return;

    pthread_mutex_lock(&ui_lock);
    int start_m = (msg_count > MAX_UI_MESSAGES) ? (msg_count - MAX_UI_MESSAGES) : 0;
    for (int i = msg_count - 1; i >= start_m; --i)
    {
        ui_message_t *m = &messages[i % MAX_UI_MESSAGES];
        if (!m->vanished && (strcmp(m->sender, sender) == 0 || (strcmp(sender, my_username) == 0 && m->type == MSG_SELF)))
        {
            snprintf(m->text, sizeof(m->text), "%s (edited)", new_text);
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_delete_last_message(const char *sender)
{
    if (!sender || strlen(sender) == 0) return;

    pthread_mutex_lock(&ui_lock);
    int start_m = (msg_count > MAX_UI_MESSAGES) ? (msg_count - MAX_UI_MESSAGES) : 0;
    for (int i = msg_count - 1; i >= start_m; --i)
    {
        ui_message_t *m = &messages[i % MAX_UI_MESSAGES];
        if (!m->vanished && (strcmp(m->sender, sender) == 0 || (strcmp(sender, my_username) == 0 && m->type == MSG_SELF)))
        {
            m->vanished = 1;
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
    memset(messages, 0, sizeof(messages));
    pthread_mutex_unlock(&ui_lock);
    // Keep schema static: we do NOT clear the whole screen with \033[2J.
    // ui_render() will redraw only the chat message viewport cleanly.
}

void ui_set_room(const char *room)
{
    if (!room) return;
    pthread_mutex_lock(&ui_lock);
    strncpy(current_room, room, sizeof(current_room) - 1);
    current_room[sizeof(current_room) - 1] = '\0';
    pthread_mutex_unlock(&ui_lock);
}

const char *ui_get_room(void)
{
    return current_room;
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

int ui_get_e2ee(void)
{
    return e2ee_active;
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

void ui_add_party(const char *party_code)
{
    if (!party_code || strlen(party_code) == 0) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < joined_party_count; ++i)
    {
        if (strcmp(joined_parties[i], party_code) == 0)
        {
            pthread_mutex_unlock(&ui_lock);
            return;
        }
    }
    if (joined_party_count < MAX_JOINED_PARTIES)
    {
        strncpy(joined_parties[joined_party_count], party_code, 15);
        joined_parties[joined_party_count][15] = '\0';
        joined_party_count++;
    }
    pthread_mutex_unlock(&ui_lock);
}

void ui_remove_party(const char *party_code)
{
    if (!party_code || strlen(party_code) == 0) return;
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < joined_party_count; ++i)
    {
        if (strcmp(joined_parties[i], party_code) == 0)
        {
            for (int j = i; j < joined_party_count - 1; ++j)
            {
                strcpy(joined_parties[j], joined_parties[j + 1]);
            }
            joined_party_count--;
            break;
        }
    }
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

int ui_add_notification(notif_type_t type, const char *sender, const char *title, const char *details, int action_param)
{
    pthread_mutex_lock(&ui_lock);
    if (notif_count >= MAX_UI_NOTIFS)
    {
        for (int i = 0; i < notif_count - 1; ++i)
        {
            notifications[i] = notifications[i + 1];
        }
        notif_count--;
    }

    int id = next_notif_id++;
    ui_notif_t *n = &notifications[notif_count++];
    n->id = id;
    n->type = type;
    if (sender) strncpy(n->sender, sender, sizeof(n->sender) - 1);
    else n->sender[0] = '\0';
    if (title) strncpy(n->title, title, sizeof(n->title) - 1);
    else n->title[0] = '\0';
    if (details) strncpy(n->details, details, sizeof(n->details) - 1);
    else n->details[0] = '\0';
    n->action_param = action_param;
    n->created_at = time(NULL);

    pthread_mutex_unlock(&ui_lock);
    return id;
}

void ui_remove_notification(int notif_id)
{
    pthread_mutex_lock(&ui_lock);
    for (int i = 0; i < notif_count; ++i)
    {
        if (notifications[i].id == notif_id)
        {
            for (int j = i; j < notif_count - 1; ++j)
            {
                notifications[j] = notifications[j + 1];
            }
            notif_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ui_lock);
}

int ui_get_notification_count(void)
{
    return notif_count;
}

ui_notif_t *ui_get_notification_by_index(int idx)
{
    if (idx < 0 || idx >= notif_count) return NULL;
    return &notifications[idx];
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

void ui_open_modal(ui_modal_t modal)
{
    pthread_mutex_lock(&ui_lock);
    active_modal = modal;
    pthread_mutex_unlock(&ui_lock);
}

void ui_close_modal(void)
{
    pthread_mutex_lock(&ui_lock);
    active_modal = UI_MODAL_NONE;
    pthread_mutex_unlock(&ui_lock);
}

ui_modal_t ui_get_active_modal(void)
{
    return active_modal;
}

void ui_set_modal_target_user(const char *user)
{
    pthread_mutex_lock(&ui_lock);
    if (user) strncpy(modal_target_user, user, sizeof(modal_target_user) - 1);
    else modal_target_user[0] = '\0';
    pthread_mutex_unlock(&ui_lock);
}

const char *ui_get_modal_target_user(void)
{
    return modal_target_user;
}

void ui_emoji_picker_next_page(void)
{
    pthread_mutex_lock(&ui_lock);
    int total_pages = (EMOJI_COUNT + 19) / 20;
    emoji_picker_page = (emoji_picker_page + 1) % total_pages;
    pthread_mutex_unlock(&ui_lock);
}

void ui_emoji_picker_prev_page(void)
{
    pthread_mutex_lock(&ui_lock);
    int total_pages = (EMOJI_COUNT + 19) / 20;
    emoji_picker_page = (emoji_picker_page - 1 + total_pages) % total_pages;
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
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m %s[YOU]\033[0m \033[1;37m>\033[0m ", tstr, my_user_color);
            break;
        case MSG_NORMAL:
        {
            const char *scolor = (msg->sender_color[0] != '\0') ? msg->sender_color : "\033[1;36m";
            snprintf(prefix_plain, sizeof(prefix_plain), "[%s] <%s> ", tstr, msg->sender);
            snprintf(prefix_color, sizeof(prefix_color), "\033[2m[%s]\033[0m %s<%s>\033[0m ", tstr, scolor, msg->sender);
            break;
        }
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

    int prefix_len = visual_str_len(prefix_plain);
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

        if (line_idx > 0)
        {
            while (*p == ' ') p++;
        }
        if (!*p) break;

        int rem_len = (int)strlen(p);
        int take = rem_len;

        if (rem_len > budget)
        {
            take = budget;
            while (take > 0 && p[take] != ' ')
            {
                take--;
            }
            if (take == 0)
            {
                take = budget;
            }
            while (take > 0 && ((unsigned char)p[take] & 0xC0) == 0x80)
            {
                take--;
            }
            if (take == 0)
            {
                take = 1;
                while (p[take] && ((unsigned char)p[take] & 0xC0) == 0x80)
                {
                    take++;
                }
            }
        }

        char chunk[256];
        if (take > 250) take = 250;
        strncpy(chunk, p, take);
        chunk[take] = '\0';

        if (line_idx == 0)
        {
            snprintf(out_lines[line_idx].plain, sizeof(out_lines[line_idx].plain), "%s%s", prefix_plain, chunk);
            if (msg->type == MSG_SELF)
                snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s%s%s\033[0m", prefix_color, my_user_color, chunk);
            else
                snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s\033[37m%s\033[0m", prefix_color, chunk);
            out_lines[line_idx].plain_len = visual_str_len(out_lines[line_idx].plain);
        }
        else
        {
            snprintf(out_lines[line_idx].plain, sizeof(out_lines[line_idx].plain), "%s%s", indent_spaces, chunk);
            if (msg->type == MSG_SELF)
                snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s%s%s\033[0m", indent_spaces, my_user_color, chunk);
            else
                snprintf(out_lines[line_idx].color, sizeof(out_lines[line_idx].color), "%s\033[37m%s\033[0m", indent_spaces, chunk);
            out_lines[line_idx].plain_len = visual_str_len(out_lines[line_idx].plain);
        }

        line_idx++;
        p += take;
        if (*p == ' ') p++;
    }

    return line_idx;
}

// Cyberpunk ASCII banner art lines for KUKUPU CHAT (fits cleanly on 80-col terminals)
static const char *cyberpunk_art[] = {
    "╦╔═ ╦ ╦ ╦╔═ ╦ ╦ ╔═╗ ╦ ╦   ╔═╗ ╦ ╦ ╔═╗ ╔╦╗",
    "╠╩╗ ║ ║ ╠╩╗ ║ ║ ╠═╝ ║ ║   ║   ╠═╣ ╠═╣  ║ ",
    "╩ ╩ ╚═╝ ╩ ╩ ╚═╝ ╩   ╚═╝   ╚═╝ ╩ ╩ ╩ ╩  ╩ "
};

void ui_render(const char *input_buf, int cursor_pos, const char *suggestion)
{
    pthread_mutex_lock(&ui_lock);

    update_term_size();

    int cols = term_cols;
    int rows = term_rows;

    int show_banner = (rows >= 22 && cols >= 80);
    int banner_height = show_banner ? 5 : 2;

    int show_sidebar = (cols >= 80);
    int sidebar_width = show_sidebar ? 24 : 0;
    int chat_width = show_sidebar ? (cols - sidebar_width - 3) : (cols - 2);

    int chat_rows = rows - banner_height - 4;
    if (chat_rows < 3) chat_rows = 3;

    // Collect and wrap messages for the active room (Lobby #0000, Party #code, or PM @user)
    render_line_t all_lines[MAX_RENDER_LINES];
    int total_lines = 0;

    int start_m = (msg_count > MAX_UI_MESSAGES) ? (msg_count - MAX_UI_MESSAGES) : 0;
    for (int m = start_m; m < msg_count && total_lines < MAX_RENDER_LINES - 20; ++m)
    {
        const ui_message_t *msg = &messages[m % MAX_UI_MESSAGES];
        if (msg->vanished) continue;

        // Isolated chat viewports
        if (current_room[0] == '@')
        {
            // In a PM viewport: show only messages belonging to this user or global system messages
            if (msg->room[0] != '\0' && strcmp(msg->room, current_room) != 0)
                continue;
        }
        else if (strcmp(current_room, "0000") == 0)
        {
            // In public lobby: show lobby messages or unassigned system messages
            if (msg->room[0] != '\0' && strcmp(msg->room, "0000") != 0 && strcmp(msg->room, "public") != 0)
                continue;
        }
        else
        {
            // In a private party: show party messages
            if (msg->room[0] != '\0' && strcmp(msg->room, current_room) != 0)
                continue;
        }

        render_line_t wrapped[16];
        int n = wrap_message(msg, chat_width - 1, wrapped, 16);
        for (int i = 0; i < n && total_lines < MAX_RENDER_LINES; ++i)
        {
            all_lines[total_lines++] = wrapped[i];
        }
    }

    int display_start = total_lines - chat_rows - scroll_offset;
    if (display_start < 0) display_start = 0;

    static char frame[FRAME_BUF_SIZE];
    int fidx = 0;

    // Reset frame buffer: hide cursor and home
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[?25l\033[H");

    // -------------------------------------------------------------
    // TOP PINNED BANNER & ASCII PAINTING
    // -------------------------------------------------------------
    if (show_banner)
    {
        // ROW 1: Header top border
        char top_left[64] = "┌── [⚡ KUKUPU CHAT] ";
        char top_mid[128];
        if (current_room[0] == '@')
            snprintf(top_mid, sizeof(top_mid), "── Room: [PM: %s] ── [E2EE: %s] ",
                     current_room, e2ee_active ? "ON" : "OFF");
        else
            snprintf(top_mid, sizeof(top_mid), "── Room: #%s ── [E2EE: %s] ",
                     current_room, e2ee_active ? "ON" : "OFF");

        char top_right[64];
        const char *disp_name = (my_profile_name[0] != '\0') ? my_profile_name : my_username;
        if (strcmp(my_username, "admin") == 0)
            snprintf(top_right, sizeof(top_right), "── %s [ADMIN] ──┐", disp_name);
        else if (my_is_registered)
            snprintf(top_right, sizeof(top_right), "── %s [REG] ──┐", disp_name);
        else
            snprintf(top_right, sizeof(top_right), "── %s [GUEST] ──┐", disp_name);

        int v_len = visual_str_len(top_left) + visual_str_len(top_mid) + visual_str_len(top_right);
        int d_len = cols - v_len;
        if (d_len < 0) d_len = 0;

        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m%s\033[0m", top_left);
        for (int i = 0; i < d_len / 2; ++i)
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
        }
        if (current_room[0] == '@')
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;35m── Room: [PM: %s]\033[0m \033[38;5;240m──\033[0m \033[1;32m[E2EE: %s]\033[0m ",
                             current_room, e2ee_active ? "ON" : "OFF");
        else
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;33m── Room: #%s\033[0m \033[38;5;240m──\033[0m \033[1;32m[E2EE: %s]\033[0m ",
                             current_room, e2ee_active ? "ON" : "OFF");
        for (int i = 0; i < d_len - (d_len / 2); ++i)
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
        }
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "%s\033[1;35m%s\033[0m\n",
                         my_is_registered ? "\033[1;32m" : "\033[1;36m", top_right);

        // ROWS 2 to 4: Cyberpunk ASCII Banner + Action Buttons (ACCOUNT, NOTIFS, ROOM)
        for (int b = 0; b < 3; ++b)
        {
            const char *art = cyberpunk_art[b];
            int art_len = visual_str_len(art);

            char btn[64] = {0};
            if (b == 0)
            {
                snprintf(btn, sizeof(btn), "\033[1;33m[ \xf0\x9f\x91\xa4 Account ]\033[0m");
            }
            else if (b == 1)
            {
                if (notif_count > 0)
                    snprintf(btn, sizeof(btn), "\033[1;31m[ \xf0\x9f\x94\x94 Notifs (%d) ]\033[0m", notif_count);
                else
                    snprintf(btn, sizeof(btn), "\033[1;36m[ \xf0\x9f\x94\x94 Notifs (0) ]\033[0m");
            }
            else
            {
                if (current_room[0] == '@')
                    snprintf(btn, sizeof(btn), "\033[1;32m[ %s PM ]\033[0m", current_room);
                else
                    snprintf(btn, sizeof(btn), "\033[1;32m[ #%s Room ]\033[0m", current_room);
            }

            int btn_plain_len = visual_str_len(btn);
            int mid_spaces = cols - art_len - btn_plain_len - 5;
            if (mid_spaces < 1) mid_spaces = 1;

            const char *art_col = (b == 0) ? "\033[38;5;51m" : ((b == 1) ? "\033[38;5;201m" : "\033[38;5;141m");

            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                             "\033[38;5;240m│\033[0m %s%s\033[0m", art_col, art);
            for (int s = 0; s < mid_spaces; ++s)
            {
                if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
            }
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "%s \033[38;5;240m│\033[0m\n", btn);
        }

        // ROW 5: Divider between Banner and Chat Viewport
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m├");
        int div_left = show_sidebar ? (chat_width + 1) : (cols - 2);
        for (int i = 0; i < div_left; ++i)
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "─");
        }
        if (show_sidebar)
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "┬");
            for (int i = 0; i < sidebar_width; ++i)
            {
                fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "─");
            }
        }
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "┤\033[0m\n");
    }
    else
    {
        // Compact Top Header for small terminals
        char top_line[256];
        snprintf(top_line, sizeof(top_line),
                 "┌── [⚡ KUKUPU] ── %s ── [ \xf0\x9f\x91\xa4 Account ] ── [ \xf0\x9f\x94\x94 Notifs (%d) ] ",
                 current_room, notif_count);
        int tlen = visual_str_len(top_line);
        int tdash = cols - tlen - 1;
        if (tdash < 0) tdash = 0;

        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m%s\033[0m", top_line);
        for (int i = 0; i < tdash; ++i)
        {
            fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
        }
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m┐\033[0m\n");
    }

    // -------------------------------------------------------------
    // CHAT VIEWPORT + SIDEBAR (OR MODAL OVERLAY)
    // -------------------------------------------------------------
    // Calculate Modal geometry if modal is active
    int modal_w = 66;
    int modal_h = 13;
    if (active_modal == UI_MODAL_PM_INVITE) { modal_w = 54; modal_h = 8; }
    else if (active_modal == UI_MODAL_EDIT_NAME) { modal_w = 56; modal_h = 8; }
    else if (active_modal == UI_MODAL_CHANGE_PWD) { modal_w = 56; modal_h = 8; }
    else if (active_modal == UI_MODAL_PARTY_JOIN_CREATE) { modal_w = 58; modal_h = (party_modal_tab == 2 ? 9 : 8); }
    else if (active_modal == UI_MODAL_RBAC_USERS) { modal_w = 66; modal_h = 10; }
    else if (active_modal == UI_MODAL_ACCOUNT) { modal_w = 66; modal_h = 13; }
    if (modal_w > cols - 4) modal_w = cols - 4;
    if (modal_h > chat_rows) modal_h = chat_rows;

    int modal_r_start = banner_height + (chat_rows - modal_h) / 2;
    int modal_c_start = (cols - modal_w) / 2;

    for (int r = 0; r < chat_rows; ++r)
    {
        int screen_row = banner_height + r + 1;

        if (active_modal != UI_MODAL_NONE && screen_row >= modal_r_start && screen_row < modal_r_start + modal_h)
        {
            // Render Modal line
            int m_line = screen_row - modal_r_start;
            char m_content[512] = {0};

            if (m_line == 0)
            {
                const char *m_title = "[ DIALOG ]";
                if (active_modal == UI_MODAL_ACCOUNT) m_title = "ACCOUNT MANAGEMENT PORTAL";
                else if (active_modal == UI_MODAL_NOTIFS) m_title = "NOTIFICATIONS PORTAL";
                else if (active_modal == UI_MODAL_PM_INVITE) m_title = "PERSONAL MESSAGE INVITE";
                else if (active_modal == UI_MODAL_EMOJI_PICKER) m_title = "EMOJI SELECTION PALETTE";
                else if (active_modal == UI_MODAL_PARTY_JOIN_CREATE) m_title = "PARTY PORTAL";
                else if (active_modal == UI_MODAL_RBAC_USERS) m_title = "RBAC USER MANAGEMENT";
                else if (active_modal == UI_MODAL_EDIT_NAME) m_title = "EDIT PROFILE NAME";
                else if (active_modal == UI_MODAL_CHANGE_PWD) m_title = "CHANGE PASSWORD";

                int t_len = visual_str_len(m_title);
                int dash = (modal_w - t_len - 4) / 2;
                if (dash > 50) dash = 50;
                if (dash < 1) dash = 1;
                char dstr[64];
                memset(dstr, '-', dash);
                dstr[dash] = '\0';
                snprintf(m_content, sizeof(m_content), "\033[1;33m+-%s %s %s-+\033[0m", dstr, m_title, dstr);
            }
            else if (m_line == modal_h - 1)
            {
                int dash = modal_w - 2;
                if (dash > 100) dash = 100;
                if (dash < 1) dash = 1;
                char dstr[128];
                memset(dstr, '-', dash);
                dstr[dash] = '\0';
                snprintf(m_content, sizeof(m_content), "\033[1;33m+-%s-+\033[0m", dstr);
            }
            else
            {
                // Modal body
                if (active_modal == UI_MODAL_ACCOUNT)
                {
                    const char *role = (strcmp(my_username, "admin") == 0) ? "Administrator" : (my_is_registered ? "Registered" : "Guest");
                    const char *dname = (my_profile_name[0] != '\0') ? my_profile_name : my_username;
                    if (m_line == 1) snprintf(m_content, sizeof(m_content), " Username:     \033[1;32m@%s\033[0m (immutable login)", my_username);
                    else if (m_line == 2) snprintf(m_content, sizeof(m_content), " Profile Name: \033[1;36m%-16s\033[0m  \033[1;33m[ \xe2\x9c\x8e Edit Name ]\033[0m", dname);
                    else if (m_line == 3) snprintf(m_content, sizeof(m_content), " Status:       %s  -  Room: #%s", role, current_room);
                    else if (m_line == 4) snprintf(m_content, sizeof(m_content), "--------------------------------------------------------------");
                    else if (m_line == 5) snprintf(m_content, sizeof(m_content), " Select Theme Color: (Click or press 1-7)");
                    else if (m_line == 6) snprintf(m_content, sizeof(m_content), " \033[1;32m[1:Grn]\033[0m \033[1;36m[2:Cyn]\033[0m \033[1;33m[3:Yel]\033[0m \033[1;35m[4:Mag]\033[0m \033[1;34m[5:Blu]\033[0m \033[1;31m[6:Red]\033[0m \033[1;37m[7:Wht]\033[0m");
                    else if (m_line == 7) snprintf(m_content, sizeof(m_content), "--------------------------------------------------------------");
                    else if (m_line == 8) snprintf(m_content, sizeof(m_content), " \033[1;33m[P: Change Password]\033[0m        \033[1;31m[L: Log Out]\033[0m");
                    else if (m_line == 9)
                    {
                        if (strcmp(my_username, "admin") == 0)
                            snprintf(m_content, sizeof(m_content), " \033[1;35m[A: \xf0\x9f\x9b\xa1 RBAC User Moderation]\033[0m");
                        else
                            snprintf(m_content, sizeof(m_content), " Security: \033[1;35mSignal Double Ratchet E2EE Active\033[0m");
                    }
                    else if (m_line == 10) snprintf(m_content, sizeof(m_content), "                    \033[1;37m[X: Close Portal]\033[0m (or Esc)");
                }
                else if (active_modal == UI_MODAL_EDIT_NAME)
                {
                    const char *dname = (my_profile_name[0] != '\0') ? my_profile_name : my_username;
                    if (m_line == 1) snprintf(m_content, sizeof(m_content), " Current Display Name: \033[1;36m%s\033[0m", dname);
                    else if (m_line == 2) snprintf(m_content, sizeof(m_content), " Enter new display name (max 24 characters):");
                    else if (m_line == 3) snprintf(m_content, sizeof(m_content), " > \033[1;32m[%-32s]\033[0m", modal_input_buf);
                    else if (m_line == 5) snprintf(m_content, sizeof(m_content), "      \033[1;32m[ Enter: Save ]\033[0m           \033[1;31m[ Esc: Cancel ]\033[0m");
                }
                else if (active_modal == UI_MODAL_CHANGE_PWD)
                {
                    char stars[64];
                    int slen = (int)strlen(modal_input_buf);
                    if (slen > 32) slen = 32;
                    memset(stars, '*', slen);
                    stars[slen] = '\0';
                    if (m_line == 1) snprintf(m_content, sizeof(m_content), " Account: \033[1;32m@%s\033[0m", my_username);
                    else if (m_line == 2) snprintf(m_content, sizeof(m_content), " Enter new password (min 6 characters):");
                    else if (m_line == 3) snprintf(m_content, sizeof(m_content), " > \033[1;33m[%-32s]\033[0m", stars);
                    else if (m_line == 5) snprintf(m_content, sizeof(m_content), "      \033[1;32m[ Enter: Update ]\033[0m         \033[1;31m[ Esc: Cancel ]\033[0m");
                }
                else if (active_modal == UI_MODAL_PARTY_JOIN_CREATE)
                {
                    if (party_modal_tab == 0)
                    {
                        if (m_line == 1) snprintf(m_content, sizeof(m_content), " Select party action:");
                        else if (m_line == 3) snprintf(m_content, sizeof(m_content), "   \033[1;36m[ 1: JOIN PARTY ]\033[0m         \033[1;32m[ 2: CREATE PARTY ]\033[0m");
                        else if (m_line == 5) snprintf(m_content, sizeof(m_content), "                    \033[1;37m[ Esc: Cancel ]\033[0m");
                    }
                    else if (party_modal_tab == 1)
                    {
                        if (m_line == 1) snprintf(m_content, sizeof(m_content), " Enter 4-15 char Party Code to join:");
                        else if (m_line == 2) snprintf(m_content, sizeof(m_content), " > \033[1;36m[%-24s]\033[0m", modal_input_buf);
                        else if (m_line == 4) snprintf(m_content, sizeof(m_content), "      \033[1;32m[ Enter: Join ]\033[0m            \033[1;31m[ Esc: Back ]\033[0m");
                    }
                    else if (party_modal_tab == 2)
                    {
                        const char *priv_str = party_create_privacy ? "\033[1;33mApproval Req (Private)\033[0m" : "\033[1;32mOpen (Anyone Can Join)\033[0m";
                        if (m_line == 1) snprintf(m_content, sizeof(m_content), " Party Code (leave empty to auto-generate):");
                        else if (m_line == 2) snprintf(m_content, sizeof(m_content), " > \033[1;32m[%-24s]\033[0m", modal_input_buf);
                        else if (m_line == 3) snprintf(m_content, sizeof(m_content), " Privacy Mode: %s  [Toggle]", priv_str);
                        else if (m_line == 5) snprintf(m_content, sizeof(m_content), "   \033[1;32m[ Enter: Create & Launch ]\033[0m     \033[1;31m[ Esc: Back ]\033[0m");
                    }
                }
                else if (active_modal == UI_MODAL_RBAC_USERS)
                {
                    if (m_line == 1) snprintf(m_content, sizeof(m_content), " Administrator Moderation Panel (Online Members):");
                    else if (m_line >= 2 && m_line <= 6)
                    {
                        int u_idx = m_line - 2;
                        if (u_idx < sidebar_user_count)
                        {
                            const char *u = sidebar_users[u_idx];
                            if (strcmp(u, my_username) != 0)
                                snprintf(m_content, sizeof(m_content), " * %-8s \033[1;31m[Kick]\033[0m \033[1;33m[Ban]\033[0m \033[1;35m[Mute 60s]\033[0m \033[1;32m[Unmute]\033[0m", u);
                            else
                                snprintf(m_content, sizeof(m_content), " * %-8s (Admin Self - Protected)", u);
                        }
                    }
                    else if (m_line == 8) snprintf(m_content, sizeof(m_content), "                    \033[1;37m[ Esc: Close ]\033[0m");
                }
                else if (active_modal == UI_MODAL_NOTIFS)
                {
                    if (notif_count == 0)
                    {
                        if (m_line == 2) snprintf(m_content, sizeof(m_content), "        No pending notifications at this time.");
                        else if (m_line == 4) snprintf(m_content, sizeof(m_content), "                   \033[1;37m[X:Close Portal]\033[0m");
                    }
                    else
                    {
                        int n_idx = (m_line - 1) / 3;
                        int n_sub = (m_line - 1) % 3;
                        if (n_idx < notif_count)
                        {
                            ui_notif_t *nt = &notifications[n_idx];
                            if (n_sub == 0) snprintf(m_content, sizeof(m_content), " [%d] %s from \033[1;32m@%s\033[0m", nt->id, nt->title, nt->sender);
                            else if (n_sub == 1) snprintf(m_content, sizeof(m_content), "     %s", nt->details);
                            else snprintf(m_content, sizeof(m_content), "     -> \033[1;32m[ ACCEPT ]\033[0m      \033[1;31m[ DECLINE ]\033[0m");
                        }
                        else if (m_line == modal_h - 2)
                        {
                            snprintf(m_content, sizeof(m_content), "                   \033[1;37m[X:Close Portal]\033[0m");
                        }
                    }
                }
                else if (active_modal == UI_MODAL_PM_INVITE)
                {
                    if (m_line == 2) snprintf(m_content, sizeof(m_content), " Send personal E2EE chat invite to \033[1;32m@%s\033[0m?", modal_target_user);
                    else if (m_line == 4) snprintf(m_content, sizeof(m_content), "      \033[1;32m[ SEND INVITE ]\033[0m       \033[1;31m[ CANCEL ]\033[0m");
                }
                else if (active_modal == UI_MODAL_EMOJI_PICKER)
                {
                    int total_pages = (EMOJI_COUNT + 19) / 20;
                    if (m_line == 1) snprintf(m_content, sizeof(m_content), " Page %d of %d - Click emoji to insert into message:", emoji_picker_page + 1, total_pages);
                    else if (m_line >= 2 && m_line <= 5)
                    {
                        int row_idx = m_line - 2;
                        char e_row[256] = {0};
                        for (int col_i = 0; col_i < 5; ++col_i)
                        {
                            int em_idx = emoji_picker_page * 20 + row_idx * 5 + col_i + 1;
                            if (em_idx <= EMOJI_COUNT)
                            {
                                char item[48];
                                snprintf(item, sizeof(item), " [%2d]%s ", em_idx, get_emoji_by_index(em_idx));
                                strncat(e_row, item, sizeof(e_row) - strlen(e_row) - 1);
                            }
                        }
                        snprintf(m_content, sizeof(m_content), " %s", e_row);
                    }
                    else if (m_line == 6)
                    {
                        snprintf(m_content, sizeof(m_content), " \033[1;36m[<< PREV]\033[0m           Page %d/%d          \033[1;36m[NEXT >>]\033[0m", emoji_picker_page + 1, total_pages);
                    }
                    else if (m_line == 7)
                    {
                        snprintf(m_content, sizeof(m_content), "                   \033[1;37m[X:CLOSE PALETTE]\033[0m");
                    }
                }
            }

            int body_vis_len = visual_str_len(m_content);
            int m_pad = modal_w - 4 - body_vis_len;
            if (m_pad < 0) m_pad = 0;

            // Render modal row centered
            for (int p = 0; p < modal_c_start; ++p)
            {
                if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
            }
            if (m_line == 0 || m_line == modal_h - 1)
            {
                fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "%s\n", m_content);
            }
            else
            {
                fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx,
                                 "\033[1;33m| \033[0m%s", m_content);
                for (int p = 0; p < m_pad; ++p)
                {
                    if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
                }
                fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;33m |\033[0m\n");
            }
            continue;
        }

        // Normal Viewport Line (with floating toast overlay on line 0 if active)
        char chat_content[512] = {0};
        int plain_len = 0;

        time_t now = time(NULL);
        if (now < toast_expiry && toast_text[0] != '\0' && r == 0)
        {
            snprintf(chat_content, sizeof(chat_content), "\033[1;33m[\xf0\x9f\x94\x94 NOTIFICATION]\033[0m \033[1;37m%s\033[0m", toast_text);
            plain_len = visual_str_len(chat_content);
        }
        else
        {
            int line_idx = display_start + r;
            if (line_idx < total_lines)
            {
                snprintf(chat_content, sizeof(chat_content), "%s", all_lines[line_idx].color);
                plain_len = all_lines[line_idx].plain_len;
            }
        }

        int pad = chat_width - plain_len;
        if (pad < 0) pad = 0;

        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m│\033[0m %s", chat_content);
        for (int p = 0; p < pad; ++p)
        {
            if (fidx < FRAME_BUF_SIZE - 64) frame[fidx++] = ' ';
        }

        // Render Sidebar
        if (show_sidebar)
        {
            char side_text[128] = {0};
            int member_rows = sidebar_user_count;
            if (member_rows > 5) member_rows = 5;

            if (r == 0)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;36m[MEMBERS (%d)]\033[0m", sidebar_user_count);
            }
            else if (r <= member_rows)
            {
                const char *u = sidebar_users[r - 1];
                if (strcmp(u, my_username) == 0)
                    snprintf(side_text, sizeof(side_text), "\033[1;32m* %s (you)\033[0m", u);
                else
                    snprintf(side_text, sizeof(side_text), "\033[37m* %s\033[0m", u);
            }
            else if (r == member_rows + 1)
            {
                snprintf(side_text, sizeof(side_text), "\033[38;5;240m──────────────────────\033[0m");
            }
            else if (r == member_rows + 2)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;33m[CHANNELS]\033[0m");
            }
            else if (r == member_rows + 3)
            {
                if (strcmp(current_room, "0000") == 0)
                    snprintf(side_text, sizeof(side_text), "\033[1;32m> # Public Lobby\033[0m");
                else
                    snprintf(side_text, sizeof(side_text), "\033[37m  # Public Lobby\033[0m");
            }
            else if (r <= member_rows + 3 + joined_party_count)
            {
                int p_idx = r - (member_rows + 4);
                if (p_idx >= 0 && p_idx < joined_party_count)
                {
                    if (strcmp(current_room, joined_parties[p_idx]) == 0)
                        snprintf(side_text, sizeof(side_text), "\033[1;32m> # Party %s\033[0m", joined_parties[p_idx]);
                    else
                        snprintf(side_text, sizeof(side_text), "\033[37m  # Party %s\033[0m", joined_parties[p_idx]);
                }
            }
            else if (r == member_rows + 4 + joined_party_count)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;36m[+ Join/Create]\033[0m");
            }
            else if (r == member_rows + 5 + joined_party_count)
            {
                snprintf(side_text, sizeof(side_text), "\033[38;5;240m──────────────────────\033[0m");
            }
            else if (r == member_rows + 6 + joined_party_count)
            {
                snprintf(side_text, sizeof(side_text), "\033[1;35m[PERSONAL MESSAGES]\033[0m");
            }
            else if (r > member_rows + 6 + joined_party_count)
            {
                int pm_idx = r - (member_rows + 7 + joined_party_count);
                if (pm_idx < pm_contact_count)
                {
                    char pm_tag[36];
                    snprintf(pm_tag, sizeof(pm_tag), "@%s", pm_contacts[pm_idx]);
                    int unread = pm_contact_unread[pm_idx];
                    if (unread > 0)
                        snprintf(side_text, sizeof(side_text), "\033[1;33m> @%s (%d)\033[0m", pm_contacts[pm_idx], unread);
                    else if (strcmp(current_room, pm_tag) == 0)
                        snprintf(side_text, sizeof(side_text), "\033[1;32m> @%s\033[0m", pm_contacts[pm_idx]);
                    else
                        snprintf(side_text, sizeof(side_text), "\033[37m  @%s\033[0m", pm_contacts[pm_idx]);
                }
                else if (pm_contact_count == 0 && pm_idx == 0)
                {
                    snprintf(side_text, sizeof(side_text), "\033[38;5;240m  (No contacts yet)\033[0m");
                }
            }

            int side_plain_len = visual_str_len(side_text);
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
    // STATUS / TYPING / PROGRESS DIVIDER
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
    else if (suggestion && strlen(suggestion) > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "Suggestions: %s", suggestion);
    }
    else if (scroll_offset > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "SCROLLBACK (+%d lines) - Press [PgDn] or type to return", scroll_offset);
    }
    else if (now - typing_peer_time < 3 && strlen(typing_peer) > 0)
    {
        snprintf(stat_content, sizeof(stat_content), "Typing: %s is typing a message...", typing_peer);
    }
    else
    {
        if (current_room[0] == '@')
            snprintf(stat_content, sizeof(stat_content), "Personal Chat with %s - Double Ratchet Active", current_room);
        else
            snprintf(stat_content, sizeof(stat_content), "Online - Room #%s - Double Ratchet Active", current_room);
    }

    int stat_len = visual_str_len(stat_content);
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
    // INPUT BOX TOP BORDER (WITH EMOJI BUTTON)
    // -------------------------------------------------------------
    char in_head[64];
    if (current_room[0] == '@')
        snprintf(in_head, sizeof(in_head), "╭─ Message %s ", current_room);
    else
        snprintf(in_head, sizeof(in_head), "╭─ Message #%s ", current_room);
    int in_head_len = visual_str_len(in_head);

    char emoji_btn[32] = " [ \xe2\x98\xba ] ─";
    int emoji_btn_len = visual_str_len(emoji_btn);

    int in_dash = cols - in_head_len - emoji_btn_len - 1;
    if (in_dash < 0) in_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m%s\033[0m", in_head);
    for (int d = 0; d < in_dash; ++d)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;33m [ \xe2\x98\xba ] \033[38;5;240m─\033[1;36m╮\033[0m\n");

    // -------------------------------------------------------------
    // INPUT TEXT LINE
    // -------------------------------------------------------------
    const char *in_text = input_buf ? input_buf : "";
    int in_len = visual_str_len(in_text);
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
    // INPUT BOX BOTTOM & HOTKEY GUIDE
    // -------------------------------------------------------------
    char in_foot[128] = "╰─ [Tab] Autocomplete  •  [PgUp/PgDn] Scroll  •  [/help] Catalog ";
    int in_foot_len = visual_str_len(in_foot);
    int in_foot_dash = cols - in_foot_len - 1;
    if (in_foot_dash < 0) in_foot_dash = 0;

    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m%s\033[0m", in_foot);
    for (int d = 0; d < in_foot_dash; ++d)
    {
        fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[38;5;240m─\033[0m");
    }
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[1;36m╯\033[0m");

    // Reposition cursor at input line
    int target_col = 5 + cursor_pos;
    if (target_col > cols - 2) target_col = cols - 2;
    fidx += snprintf(frame + fidx, FRAME_BUF_SIZE - fidx, "\033[%d;%dH\033[?25h", rows - 1, target_col);

    // Atomically write entire frame
    platform_write_console(frame, fidx);

    pthread_mutex_unlock(&ui_lock);
}

ui_action_result_t ui_handle_mouse_click(int col, int row)
{
    pthread_mutex_lock(&ui_lock);
    ui_action_result_t res;
    memset(&res, 0, sizeof(res));
    res.type = UI_ACTION_NONE;

    int cols = term_cols;
    int rows = term_rows;

    int show_banner = (rows >= 22 && cols >= 80);
    int banner_height = show_banner ? 5 : 2;
    int show_sidebar = (cols >= 80);
    int sidebar_width = show_sidebar ? 24 : 0;
    int chat_rows = rows - banner_height - 4;
    if (chat_rows < 3) chat_rows = 3;

    // Check click inside Active Modal
    if (active_modal != UI_MODAL_NONE)
    {
        int modal_w = 66;
        int modal_h = 13;
        if (active_modal == UI_MODAL_PM_INVITE) { modal_w = 54; modal_h = 8; }
        else if (active_modal == UI_MODAL_EDIT_NAME) { modal_w = 56; modal_h = 8; }
        else if (active_modal == UI_MODAL_CHANGE_PWD) { modal_w = 56; modal_h = 8; }
        else if (active_modal == UI_MODAL_PARTY_JOIN_CREATE) { modal_w = 58; modal_h = (party_modal_tab == 2 ? 9 : 8); }
        else if (active_modal == UI_MODAL_RBAC_USERS) { modal_w = 66; modal_h = 10; }
        else if (active_modal == UI_MODAL_ACCOUNT) { modal_w = 66; modal_h = 13; }

        if (modal_w > cols - 4) modal_w = cols - 4;
        if (modal_h > chat_rows) modal_h = chat_rows;

        int modal_r_start = banner_height + (chat_rows - modal_h) / 2;
        int modal_c_start = (cols - modal_w) / 2;

        if (row < modal_r_start || row >= modal_r_start + modal_h ||
            col < modal_c_start || col >= modal_c_start + modal_w)
        {
            active_modal = UI_MODAL_NONE;
            res.type = UI_ACTION_CLOSE_MODAL;
            pthread_mutex_unlock(&ui_lock);
            return res;
        }

        int m_line = row - modal_r_start;
        int rel_c = col - modal_c_start;

        if (active_modal == UI_MODAL_ACCOUNT)
        {
            if (m_line == 2 && rel_c >= 28 && rel_c <= 46) // [ ✎ Edit Name ]
            {
                active_modal = UI_MODAL_EDIT_NAME;
                modal_input_buf[0] = '\0';
                modal_input_len = 0;
                res.type = UI_ACTION_OPEN_MODAL;
                res.action_param = UI_MODAL_EDIT_NAME;
            }
            else if (m_line == 6) // Colors
            {
                if (rel_c >= 1 && rel_c <= 8) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "green"); }
                else if (rel_c >= 9 && rel_c <= 16) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "cyan"); }
                else if (rel_c >= 17 && rel_c <= 24) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "yellow"); }
                else if (rel_c >= 25 && rel_c <= 32) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "magenta"); }
                else if (rel_c >= 33 && rel_c <= 40) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "blue"); }
                else if (rel_c >= 41 && rel_c <= 48) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "red"); }
                else if (rel_c >= 49 && rel_c <= 57) { res.type = UI_ACTION_SET_COLOR; strcpy(res.action_buf, "white"); }
            }
            else if (m_line == 8) // Password or Logout
            {
                if (rel_c >= 1 && rel_c <= 22) // Change Password
                {
                    active_modal = UI_MODAL_CHANGE_PWD;
                    modal_input_buf[0] = '\0';
                    modal_input_len = 0;
                    res.type = UI_ACTION_OPEN_MODAL;
                    res.action_param = UI_MODAL_CHANGE_PWD;
                }
                else if (rel_c >= 28 && rel_c <= 42) // Log Out
                {
                    active_modal = UI_MODAL_NONE;
                    res.type = UI_ACTION_LOGOUT;
                }
            }
            else if (m_line == 9 && strcmp(my_username, "admin") == 0 && rel_c >= 1 && rel_c <= 36) // RBAC
            {
                active_modal = UI_MODAL_RBAC_USERS;
                res.type = UI_ACTION_OPEN_MODAL;
                res.action_param = UI_MODAL_RBAC_USERS;
            }
            else if (m_line == 10 && rel_c >= 15 && rel_c <= 38) // Close
            {
                active_modal = UI_MODAL_NONE;
                res.type = UI_ACTION_CLOSE_MODAL;
            }
        }
        else if (active_modal == UI_MODAL_EDIT_NAME)
        {
            if (m_line == 5)
            {
                if (rel_c >= 4 && rel_c <= 22) // Save
                {
                    res.type = UI_ACTION_EDIT_NAME;
                    strncpy(res.action_buf, modal_input_buf, sizeof(res.action_buf) - 1);
                    active_modal = UI_MODAL_NONE;
                }
                else if (rel_c >= 25 && rel_c <= 42) // Cancel
                {
                    active_modal = UI_MODAL_ACCOUNT;
                    res.type = UI_ACTION_OPEN_MODAL;
                    res.action_param = UI_MODAL_ACCOUNT;
                }
            }
        }
        else if (active_modal == UI_MODAL_CHANGE_PWD)
        {
            if (m_line == 5)
            {
                if (rel_c >= 4 && rel_c <= 24) // Update
                {
                    res.type = UI_ACTION_CHANGE_PWD;
                    strncpy(res.action_buf, modal_input_buf, sizeof(res.action_buf) - 1);
                    active_modal = UI_MODAL_NONE;
                }
                else if (rel_c >= 26 && rel_c <= 42) // Cancel
                {
                    active_modal = UI_MODAL_ACCOUNT;
                    res.type = UI_ACTION_OPEN_MODAL;
                    res.action_param = UI_MODAL_ACCOUNT;
                }
            }
        }
        else if (active_modal == UI_MODAL_PARTY_JOIN_CREATE)
        {
            if (party_modal_tab == 0)
            {
                if (m_line == 3)
                {
                    if (rel_c >= 3 && rel_c <= 22) // JOIN PARTY
                    {
                        party_modal_tab = 1;
                        modal_input_buf[0] = '\0';
                        modal_input_len = 0;
                    }
                    else if (rel_c >= 26 && rel_c <= 46) // CREATE PARTY
                    {
                        party_modal_tab = 2;
                        modal_input_buf[0] = '\0';
                        modal_input_len = 0;
                    }
                }
                else if (m_line == 5 && rel_c >= 15 && rel_c <= 35) // Cancel
                {
                    active_modal = UI_MODAL_NONE;
                    res.type = UI_ACTION_CLOSE_MODAL;
                }
            }
            else if (party_modal_tab == 1) // Join tab
            {
                if (m_line == 4)
                {
                    if (rel_c >= 4 && rel_c <= 22) // Join
                    {
                        res.type = UI_ACTION_PARTY_JOIN;
                        strncpy(res.action_buf, modal_input_buf, sizeof(res.action_buf) - 1);
                        active_modal = UI_MODAL_NONE;
                    }
                    else if (rel_c >= 25 && rel_c <= 42) // Back
                    {
                        party_modal_tab = 0;
                    }
                }
            }
            else if (party_modal_tab == 2) // Create tab
            {
                if (m_line == 3) // Privacy toggle
                {
                    party_create_privacy = !party_create_privacy;
                }
                else if (m_line == 5)
                {
                    if (rel_c >= 3 && rel_c <= 32) // Create & Launch
                    {
                        res.type = UI_ACTION_PARTY_CREATE;
                        snprintf(res.action_buf, sizeof(res.action_buf), "%s:%d",
                                 modal_input_buf[0] ? modal_input_buf : "auto", party_create_privacy);
                        active_modal = UI_MODAL_NONE;
                    }
                    else if (rel_c >= 35 && rel_c <= 50) // Back
                    {
                        party_modal_tab = 0;
                    }
                }
            }
        }
        else if (active_modal == UI_MODAL_RBAC_USERS)
        {
            if (m_line >= 2 && m_line <= 6)
            {
                int u_idx = m_line - 2;
                if (u_idx < sidebar_user_count)
                {
                    const char *target = sidebar_users[u_idx];
                    if (strcmp(target, my_username) != 0)
                    {
                        if (rel_c >= 11 && rel_c <= 18) // Kick
                        {
                            res.type = UI_ACTION_RBAC_KICK;
                            strncpy(res.action_buf, target, sizeof(res.action_buf) - 1);
                        }
                        else if (rel_c >= 19 && rel_c <= 26) // Ban
                        {
                            res.type = UI_ACTION_RBAC_BAN;
                            strncpy(res.action_buf, target, sizeof(res.action_buf) - 1);
                        }
                        else if (rel_c >= 27 && rel_c <= 39) // Mute 60s
                        {
                            res.type = UI_ACTION_RBAC_MUTE;
                            strncpy(res.action_buf, target, sizeof(res.action_buf) - 1);
                        }
                        else if (rel_c >= 40 && rel_c <= 50) // Unmute
                        {
                            res.type = UI_ACTION_RBAC_UNMUTE;
                            strncpy(res.action_buf, target, sizeof(res.action_buf) - 1);
                        }
                    }
                }
            }
            else if (m_line == 8 && rel_c >= 15 && rel_c <= 35) // Close
            {
                active_modal = UI_MODAL_NONE;
                res.type = UI_ACTION_CLOSE_MODAL;
            }
        }
        else if (active_modal == UI_MODAL_NOTIFS)
        {
            if (notif_count == 0)
            {
                if (m_line >= 3)
                {
                    active_modal = UI_MODAL_NONE;
                    res.type = UI_ACTION_CLOSE_MODAL;
                }
            }
            else
            {
                int n_idx = (m_line - 1) / 3;
                int n_sub = (m_line - 1) % 3;
                if (n_idx < notif_count && n_sub == 2)
                {
                    ui_notif_t *nt = &notifications[n_idx];
                    if (rel_c >= 8 && rel_c <= 20) // ACCEPT
                    {
                        if (nt->type == NOTIF_PM_INVITE)
                        {
                            res.type = UI_ACTION_ACCEPT_PM_INVITE;
                            strncpy(res.action_buf, nt->sender, sizeof(res.action_buf) - 1);
                        }
                        else if (nt->type == NOTIF_FILE_OFFER)
                        {
                            res.type = UI_ACTION_ACCEPT_FILE;
                            res.action_param = nt->action_param;
                        }
                        else if (nt->type == NOTIF_PARTY_JOIN_REQ)
                        {
                            res.type = UI_ACTION_ACCEPT_PARTY_REQ;
                            snprintf(res.action_buf, sizeof(res.action_buf), "%s:%s", nt->extra, nt->sender);
                        }
                        for (int j = n_idx; j < notif_count - 1; ++j) notifications[j] = notifications[j + 1];
                        notif_count--;
                        active_modal = UI_MODAL_NONE;
                    }
                    else if (rel_c >= 25 && rel_c <= 38) // DECLINE
                    {
                        if (nt->type == NOTIF_PM_INVITE)
                        {
                            res.type = UI_ACTION_DECLINE_PM_INVITE;
                            strncpy(res.action_buf, nt->sender, sizeof(res.action_buf) - 1);
                        }
                        else if (nt->type == NOTIF_FILE_OFFER)
                        {
                            res.type = UI_ACTION_DECLINE_FILE;
                            res.action_param = nt->action_param;
                        }
                        else if (nt->type == NOTIF_PARTY_JOIN_REQ)
                        {
                            res.type = UI_ACTION_DECLINE_PARTY_REQ;
                            snprintf(res.action_buf, sizeof(res.action_buf), "%s:%s", nt->extra, nt->sender);
                        }
                        for (int j = n_idx; j < notif_count - 1; ++j) notifications[j] = notifications[j + 1];
                        notif_count--;
                        active_modal = UI_MODAL_NONE;
                    }
                }
                else if (m_line == modal_h - 2)
                {
                    active_modal = UI_MODAL_NONE;
                    res.type = UI_ACTION_CLOSE_MODAL;
                }
            }
        }
        else if (active_modal == UI_MODAL_PM_INVITE)
        {
            if (m_line == 4)
            {
                if (rel_c >= 6 && rel_c <= 22) // SEND INVITE
                {
                    res.type = UI_ACTION_SEND_PM_INVITE;
                    strncpy(res.action_buf, modal_target_user, sizeof(res.action_buf) - 1);
                    active_modal = UI_MODAL_NONE;
                }
                else if (rel_c >= 28 && rel_c <= 42) // CANCEL
                {
                    active_modal = UI_MODAL_NONE;
                    res.type = UI_ACTION_CLOSE_MODAL;
                }
            }
        }
        else if (active_modal == UI_MODAL_EMOJI_PICKER)
        {
            if (m_line >= 2 && m_line <= 5)
            {
                int row_idx = m_line - 2;
                int c_idx = (rel_c - 2) / 10;
                if (c_idx >= 0 && c_idx < 5)
                {
                    int em_idx = emoji_picker_page * 20 + row_idx * 5 + c_idx + 1;
                    if (em_idx <= EMOJI_COUNT)
                    {
                        const char *em_str = get_emoji_by_index(em_idx);
                        if (em_str)
                        {
                            res.type = UI_ACTION_INSERT_TEXT;
                            strncpy(res.action_buf, em_str, sizeof(res.action_buf) - 1);
                            active_modal = UI_MODAL_NONE;
                        }
                    }
                }
            }
            else if (m_line == 6) // Pagination
            {
                if (rel_c >= 1 && rel_c <= 11)
                {
                    int total_pages = (EMOJI_COUNT + 19) / 20;
                    emoji_picker_page = (emoji_picker_page - 1 + total_pages) % total_pages;
                }
                else if (rel_c >= 45 && rel_c <= 57)
                {
                    int total_pages = (EMOJI_COUNT + 19) / 20;
                    emoji_picker_page = (emoji_picker_page + 1) % total_pages;
                }
            }
            else if (m_line == 7) // Close
            {
                active_modal = UI_MODAL_NONE;
                res.type = UI_ACTION_CLOSE_MODAL;
            }
        }

        pthread_mutex_unlock(&ui_lock);
        return res;
    }

    // Check Header / Banner clicks
    if (show_banner)
    {
        if (row == 2 && col >= cols - 16 && col <= cols - 2) // [ 👤 Account ]
        {
            active_modal = UI_MODAL_ACCOUNT;
            res.type = UI_ACTION_OPEN_MODAL;
            res.action_param = UI_MODAL_ACCOUNT;
            pthread_mutex_unlock(&ui_lock);
            return res;
        }
        else if (row == 3 && col >= cols - 19 && col <= cols - 2) // [ 🔔 Notifs (%d) ]
        {
            active_modal = UI_MODAL_NOTIFS;
            res.type = UI_ACTION_OPEN_MODAL;
            res.action_param = UI_MODAL_NOTIFS;
            pthread_mutex_unlock(&ui_lock);
            return res;
        }
    }
    else
    {
        // Compact top bar
        if (row == 1)
        {
            if (col >= 20 && col <= 36) // [ 👤 Account ]
            {
                active_modal = UI_MODAL_ACCOUNT;
                res.type = UI_ACTION_OPEN_MODAL;
                res.action_param = UI_MODAL_ACCOUNT;
                pthread_mutex_unlock(&ui_lock);
                return res;
            }
            else if (col >= 38 && col <= 56) // [ 🔔 Notifs (%d) ]
            {
                active_modal = UI_MODAL_NOTIFS;
                res.type = UI_ACTION_OPEN_MODAL;
                res.action_param = UI_MODAL_NOTIFS;
                pthread_mutex_unlock(&ui_lock);
                return res;
            }
        }
    }

    // Check Input Box [ ☺ ] button
    if (row == rows - 2)
    {
        if (col >= cols - 10 && col <= cols - 2)
        {
            active_modal = UI_MODAL_EMOJI_PICKER;
            res.type = UI_ACTION_OPEN_MODAL;
            res.action_param = UI_MODAL_EMOJI_PICKER;
            pthread_mutex_unlock(&ui_lock);
            return res;
        }
    }

    // Check Sidebar clicks
    if (show_sidebar && col >= cols - sidebar_width - 1 && col <= cols)
    {
        int r = row - banner_height - 1;
        int member_rows = sidebar_user_count;
        if (member_rows > 5) member_rows = 5;

        if (r >= 1 && r <= member_rows)
        {
            int u_idx = r - 1;
            if (u_idx < sidebar_user_count)
            {
                const char *target = sidebar_users[u_idx];
                if (strcmp(target, my_username) != 0)
                {
                    strncpy(modal_target_user, target, sizeof(modal_target_user) - 1);
                    active_modal = UI_MODAL_PM_INVITE;
                    res.type = UI_ACTION_OPEN_MODAL;
                    res.action_param = UI_MODAL_PM_INVITE;
                    pthread_mutex_unlock(&ui_lock);
                    return res;
                }
            }
        }
        else if (r == member_rows + 3) // # Public Lobby
        {
            res.type = UI_ACTION_SWITCH_ROOM;
            strcpy(res.action_buf, "0000");
            pthread_mutex_unlock(&ui_lock);
            return res;
        }
        else if (r >= member_rows + 4 && r <= member_rows + 3 + joined_party_count)
        {
            int p_idx = r - (member_rows + 4);
            if (p_idx >= 0 && p_idx < joined_party_count)
            {
                res.type = UI_ACTION_SWITCH_ROOM;
                strncpy(res.action_buf, joined_parties[p_idx], sizeof(res.action_buf) - 1);
                pthread_mutex_unlock(&ui_lock);
                return res;
            }
        }
        else if (r == member_rows + 4 + joined_party_count) // [+ Join/Create]
        {
            active_modal = UI_MODAL_PARTY_JOIN_CREATE;
            party_modal_tab = 0;
            res.type = UI_ACTION_OPEN_MODAL;
            res.action_param = UI_MODAL_PARTY_JOIN_CREATE;
            pthread_mutex_unlock(&ui_lock);
            return res;
        }
        else if (r > member_rows + 6 + joined_party_count)
        {
            int pm_idx = r - (member_rows + 7 + joined_party_count);
            if (pm_idx < pm_contact_count)
            {
                res.type = UI_ACTION_SWITCH_ROOM;
                snprintf(res.action_buf, sizeof(res.action_buf), "@%s", pm_contacts[pm_idx]);
                pm_contact_unread[pm_idx] = 0;
                pthread_mutex_unlock(&ui_lock);
                return res;
            }
        }
    }

    pthread_mutex_unlock(&ui_lock);
    return res;
}
