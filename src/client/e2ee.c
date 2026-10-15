#include "e2ee.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/rand.h>

e2ee_manager_t g_e2ee = {0};

int e2ee_init()
{
    memset(&g_e2ee, 0, sizeof(g_e2ee));
    if (x25519_generate_keypair(&g_e2ee.identity_key, g_e2ee.identity_pub) != 0)
    {
        return -1;
    }
    hex_encode(g_e2ee.identity_pub, X25519_KEY_LEN, g_e2ee.identity_pub_hex, sizeof(g_e2ee.identity_pub_hex));
    return 0;
}

void e2ee_cleanup()
{
    if (g_e2ee.identity_key)
    {
        EVP_PKEY_free(g_e2ee.identity_key);
        g_e2ee.identity_key = NULL;
    }
    for (int i = 0; i < g_e2ee.session_count; ++i)
    {
        if (g_e2ee.sessions[i].our_ratchet_dh)
        {
            EVP_PKEY_free(g_e2ee.sessions[i].our_ratchet_dh);
            g_e2ee.sessions[i].our_ratchet_dh = NULL;
        }
    }
    g_e2ee.session_count = 0;
}

ratchet_session_t* e2ee_find_session(const char *peer_username)
{
    if (!peer_username) return NULL;
    for (int i = 0; i < g_e2ee.session_count; ++i)
    {
        if (g_e2ee.sessions[i].active && strcmp(g_e2ee.sessions[i].peer, peer_username) == 0)
        {
            return &g_e2ee.sessions[i];
        }
    }
    return NULL;
}

ratchet_session_t* e2ee_get_or_create_session(const char *peer_username, const unsigned char peer_pubkey[X25519_KEY_LEN])
{
    ratchet_session_t *s = e2ee_find_session(peer_username);
    if (s) return s;

    if (g_e2ee.session_count >= MAX_E2EE_SESSIONS) return NULL;

    s = &g_e2ee.sessions[g_e2ee.session_count++];
    memset(s, 0, sizeof(ratchet_session_t));
    strncpy(s->peer, peer_username, USERNAME_LEN - 1);
    s->peer[USERNAME_LEN - 1] = '\0';
    s->active = 1;

    // Generate our initial ephemeral DH ratchet key
    if (x25519_generate_keypair(&s->our_ratchet_dh, s->our_ratchet_pub) != 0)
    {
        s->active = 0;
        return NULL;
    }

    if (peer_pubkey)
    {
        memcpy(s->peer_identity_pub, peer_pubkey, X25519_KEY_LEN);

        // Derive initial shared secret: (our_eph x peer_id) ^ (our_id x peer_id)
        unsigned char dh1[32], dh2[32], master_dh[32];
        if (x25519_derive_shared_secret(s->our_ratchet_dh, peer_pubkey, dh1) != 0 ||
            x25519_derive_shared_secret(g_e2ee.identity_key, peer_pubkey, dh2) != 0)
        {
            s->active = 0;
            return NULL;
        }
        for (int i = 0; i < 32; i++) master_dh[i] = dh1[i] ^ dh2[i];

        unsigned char kdf_out[64];
        unsigned char salt0[32] = {0};
        hkdf_sha256(salt0, 32, master_dh, 32, "DoubleRatchetMaster", kdf_out, 64);
        memcpy(s->root_key, kdf_out, 32);
        memcpy(s->send_chain_key, kdf_out + 32, 32);
    }

    return s;
}

static int symmetric_chain_step(unsigned char chain_key[32], unsigned char out_msg_key[32])
{
    unsigned char okm[64];
    unsigned char salt[32] = {0};
    if (hkdf_sha256(salt, 32, chain_key, 32, "ChainStep", okm, 64) != 0)
    {
        return -1;
    }
    memcpy(chain_key, okm, 32);
    memcpy(out_msg_key, okm + 32, 32);
    return 0;
}

int e2ee_encrypt_message(const char *peer_username, const char *plaintext, char *out_packet, size_t out_packet_max)
{
    if (!peer_username || !plaintext || !out_packet) return -1;

    ratchet_session_t *s = e2ee_find_session(peer_username);
    if (!s || !s->active) return -1;

    // Step symmetric send chain
    unsigned char msg_key[32];
    if (symmetric_chain_step(s->send_chain_key, msg_key) != 0)
    {
        return -1;
    }

    unsigned char iv[AES_IV_LEN];
    RAND_bytes(iv, sizeof(iv));

    size_t pt_len = strlen(plaintext);
    unsigned char ciphertext[BUFFER_SIZE];
    unsigned char tag[AES_TAG_LEN];

    char aad[64];
    snprintf(aad, sizeof(aad), "%s:%d", peer_username, s->send_count);

    int ct_len = aes_gcm_encrypt(msg_key, iv, (unsigned char *)aad, strlen(aad),
                                 (const unsigned char *)plaintext, pt_len,
                                 ciphertext, tag);
    if (ct_len < 0) return -1;

    char ik_hex[X25519_HEX_LEN];
    hex_encode(g_e2ee.identity_pub, X25519_KEY_LEN, ik_hex, sizeof(ik_hex));

    char ratchet_hex[X25519_HEX_LEN];
    hex_encode(s->our_ratchet_pub, X25519_KEY_LEN, ratchet_hex, sizeof(ratchet_hex));

    char iv_hex[AES_IV_LEN * 2 + 1];
    hex_encode(iv, AES_IV_LEN, iv_hex, sizeof(iv_hex));

    char tag_hex[AES_TAG_LEN * 2 + 1];
    hex_encode(tag, AES_TAG_LEN, tag_hex, sizeof(tag_hex));

    char ct_hex[BUFFER_SIZE * 2 + 1];
    hex_encode(ciphertext, (size_t)ct_len, ct_hex, sizeof(ct_hex));

    int written = snprintf(out_packet, out_packet_max,
                           "__E2EE__:%s:%s:%s:%d:%s:%s:%s",
                           peer_username, ik_hex, ratchet_hex, s->send_count, iv_hex, ct_hex, tag_hex);

    s->send_count++;
    return (written > 0 && (size_t)written < out_packet_max) ? 0 : -1;
}

int e2ee_decrypt_packet(const char *packet, char *out_from, char *out_plaintext, size_t out_max, int *out_seq)
{
    if (!packet || !out_from || !out_plaintext) return -1;

    if (strncmp(packet, "__E2EE__:", 9) != 0) return -1;

    char from[USERNAME_LEN], to[USERNAME_LEN];
    char ik_hex[X25519_HEX_LEN], ratchet_hex[X25519_HEX_LEN];
    char iv_hex[AES_IV_LEN * 2 + 1], tag_hex[AES_TAG_LEN * 2 + 1];
    char ct_hex[BUFFER_SIZE * 2 + 1];
    int seq = 0;

    int scanned = sscanf(packet, "__E2EE__:%31[^:]:%31[^:]:%64[^:]:%64[^:]:%d:%24[^:]:%4096[^:]:%32s",
                         from, to, ik_hex, ratchet_hex, &seq, iv_hex, ct_hex, tag_hex);
    if (scanned != 8) return -1;

    unsigned char remote_ik[X25519_KEY_LEN];
    unsigned char remote_ek[X25519_KEY_LEN];
    if (hex_decode(ik_hex, remote_ik, sizeof(remote_ik)) != X25519_KEY_LEN ||
        hex_decode(ratchet_hex, remote_ek, sizeof(remote_ek)) != X25519_KEY_LEN)
    {
        return -1;
    }

    ratchet_session_t *s = e2ee_find_session(from);
    if (!s)
    {
        s = e2ee_get_or_create_session(from, NULL);
        if (!s) return -1;

        memcpy(s->peer_identity_pub, remote_ik, X25519_KEY_LEN);
        memcpy(s->peer_ratchet_pub, remote_ek, X25519_KEY_LEN);
        s->has_peer_ratchet = 1;

        // Bob computes initial master DH: (bob_id x alice_ek) ^ (bob_id x alice_ik)
        unsigned char dh1[32], dh2[32], master_dh[32];
        if (x25519_derive_shared_secret(g_e2ee.identity_key, remote_ek, dh1) != 0 ||
            x25519_derive_shared_secret(g_e2ee.identity_key, remote_ik, dh2) != 0)
        {
            return -1;
        }
        for (int i = 0; i < 32; i++) master_dh[i] = dh1[i] ^ dh2[i];

        unsigned char kdf_out[64];
        unsigned char salt0[32] = {0};
        hkdf_sha256(salt0, 32, master_dh, 32, "DoubleRatchetMaster", kdf_out, 64);
        memcpy(s->root_key, kdf_out, 32);
        memcpy(s->recv_chain_key, kdf_out + 32, 32);

        // Derive Bob's sending chain key with Bob's ephemeral key and Alice's remote_ek
        unsigned char dh_bob_send[32];
        if (x25519_derive_shared_secret(s->our_ratchet_dh, remote_ek, dh_bob_send) == 0)
        {
            hkdf_sha256(s->root_key, 32, dh_bob_send, 32, "RatchetDH", kdf_out, 64);
            memcpy(s->root_key, kdf_out, 32);
            memcpy(s->send_chain_key, kdf_out + 32, 32);
        }
    }
    else if (!s->has_peer_ratchet || memcmp(s->peer_ratchet_pub, remote_ek, X25519_KEY_LEN) != 0)
    {
        // Remote DH key rotated: advance DH ratchet!
        unsigned char dh_secret[32];
        if (x25519_derive_shared_secret(s->our_ratchet_dh, remote_ek, dh_secret) != 0)
        {
            return -1;
        }
        unsigned char kdf_out[64];
        hkdf_sha256(s->root_key, 32, dh_secret, 32, "RatchetDH", kdf_out, 64);
        memcpy(s->root_key, kdf_out, 32);
        memcpy(s->recv_chain_key, kdf_out + 32, 32);

        memcpy(s->peer_ratchet_pub, remote_ek, X25519_KEY_LEN);
        s->has_peer_ratchet = 1;
        s->recv_count = 0;

        // Generate our new DH key pair for future replies
        EVP_PKEY_free(s->our_ratchet_dh);
        s->our_ratchet_dh = NULL;
        if (x25519_generate_keypair(&s->our_ratchet_dh, s->our_ratchet_pub) != 0)
        {
            return -1;
        }
        if (x25519_derive_shared_secret(s->our_ratchet_dh, remote_ek, dh_secret) == 0)
        {
            hkdf_sha256(s->root_key, 32, dh_secret, 32, "RatchetDH", kdf_out, 64);
            memcpy(s->root_key, kdf_out, 32);
            memcpy(s->send_chain_key, kdf_out + 32, 32);
            s->send_count = 0;
        }
    }

    // Step symmetric receive chain
    unsigned char msg_key[32];
    if (symmetric_chain_step(s->recv_chain_key, msg_key) != 0)
    {
        return -1;
    }

    unsigned char iv[AES_IV_LEN];
    if (hex_decode(iv_hex, iv, sizeof(iv)) != AES_IV_LEN) return -1;

    unsigned char tag[AES_TAG_LEN];
    if (hex_decode(tag_hex, tag, sizeof(tag)) != AES_TAG_LEN) return -1;

    unsigned char ct[BUFFER_SIZE];
    int ct_len = hex_decode(ct_hex, ct, sizeof(ct));
    if (ct_len < 0) return -1;

    char aad[64];
    snprintf(aad, sizeof(aad), "%s:%d", to, seq);

    unsigned char pt[BUFFER_SIZE];
    int pt_len = aes_gcm_decrypt(msg_key, iv, (unsigned char *)aad, strlen(aad),
                                 ct, (size_t)ct_len, tag, pt);
    if (pt_len < 0)
    {
        snprintf(aad, sizeof(aad), "%s:%d", from, seq);
        pt_len = aes_gcm_decrypt(msg_key, iv, (unsigned char *)aad, strlen(aad),
                                 ct, (size_t)ct_len, tag, pt);
        if (pt_len < 0) return -1;
    }

    pt[pt_len] = '\0';
    strncpy(out_from, from, USERNAME_LEN - 1);
    out_from[USERNAME_LEN - 1] = '\0';
    strncpy(out_plaintext, (char *)pt, out_max - 1);
    out_plaintext[out_max - 1] = '\0';
    if (out_seq) *out_seq = seq;

    s->recv_count++;
    return 0;
}
