#ifndef E2EE_H
#define E2EE_H

#include "common.h"
#include "crypto_utils.h"

#define MAX_E2EE_SESSIONS 32

typedef struct {
    char peer[USERNAME_LEN];
    int active;
    
    unsigned char peer_identity_pub[X25519_KEY_LEN];
    EVP_PKEY *our_ratchet_dh;
    unsigned char our_ratchet_pub[X25519_KEY_LEN];
    unsigned char peer_ratchet_pub[X25519_KEY_LEN];
    
    unsigned char root_key[32];
    unsigned char send_chain_key[32];
    unsigned char recv_chain_key[32];
    
    int send_count;
    int recv_count;
    int prev_send_count;
    int has_peer_ratchet;
} ratchet_session_t;

typedef struct {
    EVP_PKEY *identity_key;
    unsigned char identity_pub[X25519_KEY_LEN];
    char identity_pub_hex[X25519_HEX_LEN];
    ratchet_session_t sessions[MAX_E2EE_SESSIONS];
    int session_count;
} e2ee_manager_t;

// Global client E2EE state
extern e2ee_manager_t g_e2ee;

// Initialize client E2EE manager
int e2ee_init();
void e2ee_cleanup();

// Session management
ratchet_session_t* e2ee_get_or_create_session(const char *peer_username, const unsigned char peer_pubkey[X25519_KEY_LEN]);
ratchet_session_t* e2ee_find_session(const char *peer_username);

// Double Ratchet Encrypt / Decrypt
int e2ee_encrypt_message(const char *peer_username, const char *plaintext, char *out_packet, size_t out_packet_max);
int e2ee_decrypt_packet(const char *packet, char *out_from, char *out_plaintext, size_t out_max, int *out_seq);

#endif // E2EE_H
