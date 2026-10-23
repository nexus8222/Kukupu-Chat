#include "db.h"
#include "crypto_utils.h"
#include "compat.h"
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
    chmod(DB_CRED_FILE, 0600);

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

    pthread_mutex_lock(&db_lock);

    // Enforce quota per user to prevent storage exhaustion DoS (CWE-400)
    FILE *chk = fopen(DB_INBOX_FILE, "rb");
    if (chk) {
        offline_msg_t temp;
        int user_count = 0;
        while (fread(&temp, sizeof(offline_msg_t), 1, chk) == 1) {
            if (strcmp(temp.recipient, recipient) == 0) user_count++;
        }
        fclose(chk);
        if (user_count >= MAX_OFFLINE_PER_USER) {
            pthread_mutex_unlock(&db_lock);
            return 0; // Quota exceeded
        }
    }

    offline_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.timestamp = time(NULL);
    strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
    strncpy(msg.recipient, recipient, sizeof(msg.recipient) - 1);
    strncpy(msg.content, content, sizeof(msg.content) - 1);

    FILE *f = fopen(DB_INBOX_FILE, "ab");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    size_t written = fwrite(&msg, sizeof(offline_msg_t), 1, f);
    fclose(f);
    chmod(DB_INBOX_FILE, 0600);
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
    chmod(DB_INBOX_FILE, 0600);

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
    chmod(DB_KEYS_FILE, 0600);

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

// Internal helper: checks if username is already registered (assumes db_lock held)
static int db_user_exists_locked(const char *username) {
    if (!username || strlen(username) == 0) return 0;
    FILE *f = fopen(DB_ACCOUNTS_FILE, "r");
    if (!f) return 0;

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char u[64] = {0};
        if (sscanf(line, "%63[^:]", u) == 1) {
            if (strcasecmp(u, username) == 0) {
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

int db_user_exists(const char *username) {
    if (!username) return 0;
    pthread_mutex_lock(&db_lock);
    int exists = db_user_exists_locked(username);
    pthread_mutex_unlock(&db_lock);
    return exists;
}

int db_register_user(const char *username, const char *password, char *err_msg, size_t err_len) {
    if (!username || !password) {
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Missing username or password");
        return 0;
    }

    size_t ulen = strlen(username);
    if (ulen < 2 || ulen > 10) {
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Username must be between 2 and 10 characters");
        return 0;
    }

    // Only alphanumeric and underscore
    for (size_t i = 0; i < ulen; ++i) {
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Username can only contain letters, numbers, and '_'");
            return 0;
        }
    }

    // Block reserved names
    if (strcasecmp(username, "admin") == 0 ||
        strcasecmp(username, "server") == 0 ||
        strcasecmp(username, "system") == 0 ||
        strcasecmp(username, "all") == 0 ||
        strcasecmp(username, "root") == 0 ||
        strcasecmp(username, "public") == 0) {
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Username '%s' is a reserved system name", username);
        return 0;
    }

    size_t plen = strlen(password);
    if (plen < 6 || plen > 64) {
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Password must be between 6 and 64 characters");
        return 0;
    }

    pthread_mutex_lock(&db_lock);

    if (db_user_exists_locked(username)) {
        pthread_mutex_unlock(&db_lock);
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Username '%s' is already registered", username);
        return 0;
    }

    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        pthread_mutex_unlock(&db_lock);
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Cryptographic random generation failed");
        return 0;
    }

    unsigned char hash[32];
    if (hash_password_pbkdf2(password, salt, sizeof(salt), hash) != 0) {
        pthread_mutex_unlock(&db_lock);
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Password hashing failed");
        return 0;
    }

    char salt_hex[SALT_LEN * 2 + 1];
    char hash_hex[32 * 2 + 1];
    hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    hex_encode(hash, sizeof(hash), hash_hex, sizeof(hash_hex));

    FILE *f = fopen(DB_ACCOUNTS_FILE, "a");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        if (err_msg && err_len > 0) snprintf(err_msg, err_len, "Could not open user accounts store");
        return 0;
    }

    time_t now = time(NULL);
    fprintf(f, "%s:%s:%s:%ld\n", username, salt_hex, hash_hex, (long)now);
    fclose(f);
    chmod(DB_ACCOUNTS_FILE, 0600);

    pthread_mutex_unlock(&db_lock);
    return 1;
}

int db_verify_user(const char *username, const char *password) {
    if (!username || !password) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_ACCOUNTS_FILE, "r");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    char line[256];
    int verified = 0;
    while (fgets(line, sizeof(line), f)) {
        char u[64] = {0}, s_hex[64] = {0}, h_hex[128] = {0};
        long t = 0;
        if (sscanf(line, "%63[^:]:%63[^:]:%127[^:]:%ld", u, s_hex, h_hex, &t) >= 3) {
            if (strcasecmp(u, username) == 0) {
                unsigned char salt[SALT_LEN];
                unsigned char expected_hash[32];
                if (hex_decode(s_hex, salt, sizeof(salt)) == (int)sizeof(salt) &&
                    hex_decode(h_hex, expected_hash, sizeof(expected_hash)) == (int)sizeof(expected_hash)) {
                    if (verify_password_pbkdf2(password, salt, sizeof(salt), expected_hash)) {
                        verified = 1;
                    }
                }
                break;
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&db_lock);
    return verified;
}

int db_update_user_password(const char *username, const char *new_password) {
    if (!username || !new_password || strlen(new_password) < 6) return 0;

    pthread_mutex_lock(&db_lock);
    FILE *f = fopen(DB_ACCOUNTS_FILE, "r");
    if (!f) {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    FILE *tmp = fopen(DB_ACCOUNTS_FILE ".tmp", "w");
    if (!tmp) {
        fclose(f);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        fclose(f);
        fclose(tmp);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    unsigned char hash[32];
    if (hash_password_pbkdf2(new_password, salt, sizeof(salt), hash) != 0) {
        fclose(f);
        fclose(tmp);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    char salt_hex[SALT_LEN * 2 + 1];
    char hash_hex[32 * 2 + 1];
    hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    hex_encode(hash, sizeof(hash), hash_hex, sizeof(hash_hex));

    char line[256];
    int updated = 0;
    time_t now = time(NULL);

    while (fgets(line, sizeof(line), f)) {
        char u[64] = {0};
        if (sscanf(line, "%63[^:]", u) == 1) {
            if (strcasecmp(u, username) == 0) {
                fprintf(tmp, "%s:%s:%s:%ld\n", username, salt_hex, hash_hex, (long)now);
                updated = 1;
            } else {
                fputs(line, tmp);
            }
        }
    }

    fclose(f);
    fclose(tmp);

    if (updated) {
        remove(DB_ACCOUNTS_FILE);
        rename(DB_ACCOUNTS_FILE ".tmp", DB_ACCOUNTS_FILE);
        chmod(DB_ACCOUNTS_FILE, 0600);
    } else {
        remove(DB_ACCOUNTS_FILE ".tmp");
    }

    pthread_mutex_unlock(&db_lock);
    return updated;
}

