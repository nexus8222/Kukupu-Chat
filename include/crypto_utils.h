#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>
#include <openssl/evp.h>

#define SHA256_HEX_LEN 65
#define X25519_KEY_LEN 32
#define X25519_HEX_LEN 65
#define AES_KEY_LEN 32
#define AES_IV_LEN 12
#define AES_TAG_LEN 16
#define SALT_LEN 16
#define PBKDF2_ITER 10000

// Hex encoding / decoding
void hex_encode(const unsigned char *src, size_t len, char *dest, size_t dest_max);
int hex_decode(const char *src, unsigned char *dest, size_t dest_max);

// Base64 encoding / decoding
int base64_encode(const unsigned char *src, size_t len, char *dest, size_t dest_max);
int base64_decode(const char *src, unsigned char *dest, size_t *out_len);

// SHA-256 hashes
int compute_sha256_buffer(const unsigned char *data, size_t len, char hex_out[SHA256_HEX_LEN]);
int compute_sha256_file(const char *filepath, char hex_out[SHA256_HEX_LEN]);

// PBKDF2 password hashing & verification
int hash_password_pbkdf2(const char *password, const unsigned char *salt, size_t salt_len, unsigned char out_hash[32]);
int verify_password_pbkdf2(const char *password, const unsigned char *salt, size_t salt_len, const unsigned char expected_hash[32]);

// X25519 Key Exchange
int x25519_generate_keypair(EVP_PKEY **out_pkey, unsigned char out_raw_pub[X25519_KEY_LEN]);
int x25519_derive_shared_secret(EVP_PKEY *our_priv_key, const unsigned char peer_raw_pub[X25519_KEY_LEN], unsigned char out_secret[32]);

// HKDF-SHA256 Key Derivation
int hkdf_sha256(const unsigned char *salt, size_t salt_len,
                const unsigned char *ikm, size_t ikm_len,
                const char *info,
                unsigned char *okm, size_t okm_len);

// AES-256-GCM Authenticated Encryption & Decryption
int aes_gcm_encrypt(const unsigned char key[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *aad, size_t aad_len,
                    const unsigned char *plaintext, size_t pt_len,
                    unsigned char *ciphertext,
                    unsigned char tag[AES_TAG_LEN]);

int aes_gcm_decrypt(const unsigned char key[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *aad, size_t aad_len,
                    const unsigned char *ciphertext, size_t ct_len,
                    const unsigned char tag[AES_TAG_LEN],
                    unsigned char *plaintext);

#endif // CRYPTO_UTILS_H
