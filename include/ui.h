#ifndef UI_H
#define UI_H

#include <stddef.h>
#include <time.h>

typedef enum {
    MSG_NORMAL = 0,
    MSG_SELF,
    MSG_E2EE,
    MSG_PRIVATE,
    MSG_SYSTEM,
    MSG_FILE,
    MSG_VANISH,
    MSG_ERROR
} ui_msg_type_t;

typedef enum {
    UI_MODAL_NONE = 0,
    UI_MODAL_ACCOUNT,
    UI_MODAL_NOTIFS,
    UI_MODAL_PM_INVITE,
    UI_MODAL_EMOJI_PICKER,
    UI_MODAL_PARTY_JOIN_CREATE,
    UI_MODAL_RBAC_USERS,
    UI_MODAL_CHANGE_PWD,
    UI_MODAL_EDIT_NAME
} ui_modal_t;

typedef enum {
    NOTIF_PM_INVITE = 0,
    NOTIF_FILE_OFFER,
    NOTIF_PARTY_JOIN_REQ,
    NOTIF_SYSTEM_ANNOUNCE
} notif_type_t;

typedef struct {
    int id;
    notif_type_t type;
    char sender[32];
    char title[64];
    char details[128];
    int action_param;
    char extra[64];
    time_t created_at;
} ui_notif_t;

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_SEND_PM_INVITE,    // target in action_buf
    UI_ACTION_ACCEPT_PM_INVITE,  // target in action_buf
    UI_ACTION_DECLINE_PM_INVITE, // target in action_buf
    UI_ACTION_ACCEPT_FILE,       // action_param = fid
    UI_ACTION_DECLINE_FILE,      // action_param = fid
    UI_ACTION_ACCEPT_PARTY_REQ,  // party:user in action_buf
    UI_ACTION_DECLINE_PARTY_REQ, // party:user in action_buf
    UI_ACTION_SWITCH_ROOM,       // room in action_buf
    UI_ACTION_INSERT_TEXT,       // text to insert in action_buf
    UI_ACTION_TOGGLE_E2EE,
    UI_ACTION_SET_COLOR,         // color in action_buf
    UI_ACTION_PARTY_JOIN,        // party_code in action_buf
    UI_ACTION_PARTY_CREATE,      // party_code:privacy in action_buf
    UI_ACTION_EDIT_NAME,         // new_name in action_buf
    UI_ACTION_CHANGE_PWD,        // new_pwd in action_buf
    UI_ACTION_LOGOUT,
    UI_ACTION_RBAC_KICK,         // user in action_buf
    UI_ACTION_RBAC_BAN,          // user in action_buf
    UI_ACTION_RBAC_MUTE,         // user in action_buf
    UI_ACTION_RBAC_UNMUTE,       // user in action_buf
    UI_ACTION_OPEN_MODAL,        // action_param = modal_type
    UI_ACTION_CLOSE_MODAL
} ui_action_type_t;

typedef struct {
    ui_action_type_t type;
    char action_buf[256];
    int action_param;
} ui_action_result_t;

// Terminal life-cycle
void ui_init(const char *username);
void ui_shutdown(void);
void ui_resize(void);

// Profile & State
void ui_set_profile_name(const char *name);
const char *ui_get_profile_name(void);

// Chat messages
void ui_add_message(ui_msg_type_t type, const char *sender, const char *text, int msg_id);
void ui_add_message_with_color(ui_msg_type_t type, const char *sender, const char *text, int msg_id, const char *color);
void ui_add_message_to_room(ui_msg_type_t type, const char *sender, const char *text, int msg_id, const char *color, const char *room);
void ui_set_user_color(const char *color_code);
const char *ui_get_user_color(void);
void ui_set_registered(int is_registered);
int ui_is_registered(void);
void ui_edit_message(int msg_id, const char *new_text);
void ui_delete_message(int msg_id);
void ui_edit_last_message(const char *sender, const char *new_text);
void ui_delete_last_message(const char *sender);
void ui_clear_messages(void);

// Personal Messages (PM Contacts)
void ui_add_pm_contact(const char *username);
void ui_remove_pm_contact(const char *username);
int ui_has_pm_contact(const char *username);
int ui_get_pm_contact_count(void);
const char *ui_get_pm_contact(int idx);
void ui_inc_pm_unread(const char *username);
int ui_get_pm_unread(const char *username);
void ui_clear_pm_unread(const char *username);

// Floating Toasts
void ui_trigger_toast(const char *text, int sound_type);

// Channel & user state
void ui_set_room(const char *room);
const char *ui_get_room(void);
void ui_set_typing(const char *username);
void ui_set_e2ee(int active);
int ui_get_e2ee(void);
void ui_set_file_progress(const char *filename, int cur, int tot);
void ui_clear_file_progress(void);

// Channels in sidebar
void ui_add_party(const char *party_code);
void ui_remove_party(const char *party_code);

// Sidebar online users
void ui_add_user(const char *username);
void ui_remove_user(const char *username);
void ui_clear_users(void);
void ui_set_users_from_string(const char *raw_list);

// Notifications portal
int ui_add_notification(notif_type_t type, const char *sender, const char *title, const char *details, int action_param);
void ui_remove_notification(int notif_id);
int ui_get_notification_count(void);
ui_notif_t *ui_get_notification_by_index(int idx);

// Navigation
void ui_scroll_up(int lines);
void ui_scroll_down(int lines);
void ui_scroll_bottom(void);

// Modals
void ui_open_modal(ui_modal_t modal);
void ui_close_modal(void);
ui_modal_t ui_get_active_modal(void);
void ui_set_modal_target_user(const char *user);
const char *ui_get_modal_target_user(void);
void ui_emoji_picker_next_page(void);
void ui_emoji_picker_prev_page(void);
void ui_modal_input_char(char ch);
void ui_modal_input_backspace(void);
const char *ui_modal_get_input(void);
void ui_modal_clear_input(void);
int ui_modal_is_input_active(void);
void ui_party_modal_toggle_privacy(void);
int ui_party_modal_get_privacy(void);
void ui_set_party_modal_tab(int tab);
int ui_get_party_modal_tab(void);

// Interactive Mouse Handling
ui_action_result_t ui_handle_mouse_click(int col, int row);

// Render loop
void ui_render(const char *input_buf, int cursor_pos, const char *suggestion);

#endif
