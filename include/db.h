#ifndef DB_H
#define DB_H

#include <time.h>

#define MAX_OFFLINE_PER_USER 50
#define DB_CRED_FILE "admin.cred"
#define DB_INBOX_FILE "offline_inbox.dat"

typedef struct {
    time_t timestamp;
    char sender[32];
    char recipient[32];
    char content[2048];
} offline_msg_t;

// Admin Authentication via PBKDF2
int db_init_admin(const char *default_pwd);
int db_verify_admin_password(const char *pwd);
int db_set_admin_password(const char *new_pwd);

// Offline Messaging Inbox
int db_store_offline_msg(const char *sender, const char *recipient, const char *content);
int db_fetch_and_clear_offline_msgs(const char *recipient, offline_msg_t *out_msgs, int max_count);

// Public Key Directory (Signal Prekeys / Identity Keys)
#define DB_KEYS_FILE "user_keys.dat"
int db_save_user_key(const char *username, const char *pubkey_hex);
int db_get_user_key(const char *username, char *out_pubkey_hex, size_t max_len);

#endif // DB_H
