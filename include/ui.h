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

// Terminal life-cycle
void ui_init(const char *username);
void ui_shutdown(void);
void ui_resize(void);

// Chat messages
void ui_add_message(ui_msg_type_t type, const char *sender, const char *text, int msg_id);
void ui_edit_message(int msg_id, const char *new_text);
void ui_delete_message(int msg_id);
void ui_clear_messages(void);

// Channel & user state
void ui_set_room(const char *room);
void ui_set_typing(const char *username);
void ui_set_e2ee(int active);
void ui_set_file_progress(const char *filename, int cur, int tot);
void ui_clear_file_progress(void);

// Sidebar online users
void ui_add_user(const char *username);
void ui_remove_user(const char *username);
void ui_clear_users(void);
void ui_set_users_from_string(const char *raw_list);

// Navigation
void ui_scroll_up(int lines);
void ui_scroll_down(int lines);
void ui_scroll_bottom(void);

// Render loop
void ui_render(const char *input_buf, int cursor_pos, const char *suggestion);

#endif
