#include "db.h"
#include "crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <openssl/rand.h>

static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;

int db_set_admin_password(const char *new_pwd) {
    if (!new_pwd || strlen(new_pwd) == 0) return 0;

    pthread_mutex_lock(&db_lock);

    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    unsigned char hash[32];
    if (hash_password_pbkdf2(new_pwd, salt, sizeof(salt), hash) != 0) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    char salt_hex[SALT_LEN * 2 + 1];
    char hash_hex[32 * 2 + 1];
    hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    hex_encode(hash, sizeof(hash), hash_hex, sizeof(hash_hex));

    FILE *f = fopen(DB_CRED_FILE, "w");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    fprintf(f, "%s:%s\n", salt_hex, hash_hex);
    fclose(f);

    pthread_mutex_unlock(&db_lock);
    return 1;
}

int db_init_admin(const char *default_pwd) {
    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_CRED_FILE, "r");
    if (f) {
        fclose(f);
        pthread_mutex_unlock(&db_lock);
        return 1; // Already initialized
    }
    pthread_mutex_unlock(&db_lock);

    return db_set_admin_password(default_pwd ? default_pwd : "admin123");
}

int db_verify_admin_password(const char *pwd) {
    if (!pwd) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_CRED_FILE, "r");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }
    fclose(f);
    pthread_mutex_unlock(&db_lock);

    char salt_hex[64] = {0};
    char hash_hex[128] = {0};
    if (sscanf(line, "%63[^:]:%127s", salt_hex, hash_hex) != 2) {
        return 0;
    }

    unsigned char salt[SALT_LEN];
    unsigned char expected_hash[32];
    if (hex_decode(salt_hex, salt, sizeof(salt)) != SALT_LEN) return 0;
    if (hex_decode(hash_hex, expected_hash, sizeof(expected_hash)) != 32) return 0;

    return verify_password_pbkdf2(pwd, salt, sizeof(salt), expected_hash);
}

int db_store_offline_msg(const char *sender, const char *recipient, const char *content) {
    if (!sender || !recipient || !content) return 0;

    offline_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.timestamp = time(NULL);
    strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
    strncpy(msg.recipient, recipient, sizeof(msg.recipient) - 1);
    strncpy(msg.content, content, sizeof(msg.content) - 1);

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_INBOX_FILE, "ab");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    size_t written = fwrite(&msg, sizeof(offline_msg_t), 1, f);
    fclose(f);
    pthread_mutex_unlock(&db_lock);

    return written == 1 ? 1 : 0;
}

int db_fetch_and_clear_offline_msgs(const char *recipient, offline_msg_t *out_msgs, int max_count) {
    if (!recipient || !out_msgs || max_count <= 0) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_INBOX_FILE, "rb");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    FILE *tmp = fopen(DB_INBOX_FILE ".tmp", "wb");
    if (!tmp) {
        fclose(f);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    offline_msg_t item;
    int count = 0;
    while (fread(&item, sizeof(offline_msg_t), 1, f) == 1) {
        if (strcmp(item.recipient, recipient) == 0 && count < max_count) {
            out_msgs[count++] = item;
        } else {
            fwrite(&item, sizeof(offline_msg_t), 1, tmp);
        }
    }

    fclose(f);
    fclose(tmp);

    remove(DB_INBOX_FILE);
    rename(DB_INBOX_FILE ".tmp", DB_INBOX_FILE);

    pthread_mutex_unlock(&db_lock);
    return count;
}

int db_save_user_key(const char *username, const char *pubkey_hex) {
    if (!username || !pubkey_hex) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_KEYS_FILE, "r");
    FILE *tmp = fopen(DB_KEYS_FILE ".tmp", "w");
    if (!tmp) {
        if (f) fclose(f);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    int updated = 0;
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char u[64] = {0}, k[128] = {0};
            if (sscanf(line, "%63[^:]:%127s", u, k) == 2) {
                if (strcmp(u, username) == 0) {
                    fprintf(tmp, "%s:%s\n", username, pubkey_hex);
                    updated = 1;
                } else {
                    fprintf(tmp, "%s:%s\n", u, k);
                }
            }
        }
        fclose(f);
    }

    if (!updated) {
        fprintf(tmp, "%s:%s\n", username, pubkey_hex);
    }

    fclose(tmp);
    remove(DB_KEYS_FILE);
    rename(DB_KEYS_FILE ".tmp", DB_KEYS_FILE);

    pthread_mutex_unlock(&db_lock);
    return 1;
}

int db_get_user_key(const char *username, char *out_pubkey_hex, size_t max_len) {
    if (!username || !out_pubkey_hex || max_len == 0) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_KEYS_FILE, "r");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char u[64] = {0}, k[128] = {0};
        if (sscanf(line, "%63[^:]:%127s", u, k) == 2) {
            if (strcmp(u, username) == 0) {
                snprintf(out_pubkey_hex, max_len, "%s", k);
                found = 1;
                break;
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&db_lock);
    return found;
}

