#include "crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>

void hex_encode(const unsigned char *src, size_t len, char *dest, size_t dest_max)
{
    if (!src || !dest || dest_max < (len * 2 + 1)) return;
    for (size_t i = 0; i < len; ++i)
    {
        sprintf(dest + (i * 2), "%02x", src[i]);
    }
    dest[len * 2] = '\0';
}

int hex_decode(const char *src, unsigned char *dest, size_t dest_max)
{
    if (!src || !dest) return -1;
    size_t len = strlen(src);
    if (len % 2 != 0 || (len / 2) > dest_max) return -1;

    for (size_t i = 0; i < len / 2; ++i)
    {
        unsigned int val = 0;
        if (sscanf(src + (i * 2), "%02x", &val) != 1) return -1;
        dest[i] = (unsigned char)val;
    }
    return (int)(len / 2);
}

int base64_encode(const unsigned char *src, size_t len, char *dest, size_t dest_max)
{
    if (!src || !dest) return -1;
    size_t expected_len = 4 * ((len + 2) / 3);
    if (dest_max < expected_len + 1) return -1;

    int encoded_len = EVP_EncodeBlock((unsigned char *)dest, src, (int)len);
    dest[encoded_len] = '\0';
    return encoded_len;
}

int base64_decode(const char *src, unsigned char *dest, size_t *out_len)
{
    if (!src || !dest || !out_len) return -1;
    int src_len = (int)strlen(src);
    int decoded_len = EVP_DecodeBlock(dest, (const unsigned char *)src, src_len);
    if (decoded_len < 0) return -1;

    // Adjust for padding '=' characters
    int padding = 0;
    if (src_len > 0 && src[src_len - 1] == '=') padding++;
    if (src_len > 1 && src[src_len - 2] == '=') padding++;

    *out_len = (size_t)(decoded_len - padding);
    return 0;
}

int compute_sha256_buffer(const unsigned char *data, size_t len, char hex_out[SHA256_HEX_LEN])
{
    if (!data || !hex_out) return -1;
    unsigned char hash[32];
    unsigned int hash_len = 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    EVP_MD_CTX_free(ctx);

    hex_encode(hash, hash_len, hex_out, SHA256_HEX_LEN);
    return 0;
}

int compute_sha256_file(const char *filepath, char hex_out[SHA256_HEX_LEN])
{
    if (!filepath || !hex_out) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { fclose(fp); return -1; }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
    {
        EVP_MD_CTX_free(ctx);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[4096];
    size_t bytes = 0;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        EVP_DigestUpdate(ctx, buffer, bytes);
    }

    unsigned char hash[32];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);

    EVP_MD_CTX_free(ctx);
    fclose(fp);

    hex_encode(hash, hash_len, hex_out, SHA256_HEX_LEN);
    return 0;
}

int hash_password_pbkdf2(const char *password, const unsigned char *salt, size_t salt_len, unsigned char out_hash[32])
{
    if (!password || !salt || !out_hash) return -1;
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                          salt, (int)salt_len,
                          PBKDF2_ITER, EVP_sha256(),
                          32, out_hash) != 1)
    {
        return -1;
    }
    return 0;
}

int verify_password_pbkdf2(const char *password, const unsigned char *salt, size_t salt_len, const unsigned char expected_hash[32])
{
    unsigned char actual_hash[32];
    if (hash_password_pbkdf2(password, salt, salt_len, actual_hash) != 0)
        return 0;
    return (CRYPTO_memcmp(actual_hash, expected_hash, 32) == 0);
}

int x25519_generate_keypair(EVP_PKEY **out_pkey, unsigned char out_raw_pub[X25519_KEY_LEN])
{
    if (!out_pkey || !out_raw_pub) return -1;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!pctx) return -1;

    if (EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_keygen(pctx, out_pkey) <= 0)
    {
        EVP_PKEY_CTX_free(pctx);
        return -1;
    }
    EVP_PKEY_CTX_free(pctx);

    size_t pub_len = X25519_KEY_LEN;
    if (EVP_PKEY_get_raw_public_key(*out_pkey, out_raw_pub, &pub_len) <= 0 || pub_len != X25519_KEY_LEN)
    {
        EVP_PKEY_free(*out_pkey);
        *out_pkey = NULL;
        return -1;
    }
    return 0;
}

int x25519_derive_shared_secret(EVP_PKEY *our_priv_key, const unsigned char peer_raw_pub[X25519_KEY_LEN], unsigned char out_secret[32])
{
    if (!our_priv_key || !peer_raw_pub || !out_secret) return -1;

    EVP_PKEY *peer_pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_raw_pub, X25519_KEY_LEN);
    if (!peer_pkey) return -1;

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(our_priv_key, NULL);
    if (!ctx)
    {
        EVP_PKEY_free(peer_pkey);
        return -1;
    }

    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx, peer_pkey) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_pkey);
        return -1;
    }

    size_t secret_len = 32;
    if (EVP_PKEY_derive(ctx, out_secret, &secret_len) <= 0 || secret_len != 32)
    {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_pkey);
        return -1;
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer_pkey);
    return 0;
}

int hkdf_sha256(const unsigned char *salt, size_t salt_len,
                const unsigned char *ikm, size_t ikm_len,
                const char *info,
                unsigned char *okm, size_t okm_len)
{
    if (!ikm || !okm) return -1;
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return -1;

    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return -1;

    OSSL_PARAM params[5];
    char digest[] = "SHA256";
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, sizeof(digest));
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)ikm, ikm_len);
    params[2] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)salt, salt_len);
    params[3] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info, info ? strlen(info) : 0);
    params[4] = OSSL_PARAM_construct_end();

    int res = EVP_KDF_derive(kctx, okm, okm_len, params);
    EVP_KDF_CTX_free(kctx);
    return (res > 0) ? 0 : -1;
}

int aes_gcm_encrypt(const unsigned char key[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *aad, size_t aad_len,
                    const unsigned char *plaintext, size_t pt_len,
                    unsigned char *ciphertext,
                    unsigned char tag[AES_TAG_LEN])
{
    if (!key || !iv || !plaintext || !ciphertext || !tag) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len = 0;
    int ciphertext_len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (aad && aad_len > 0)
    {
        if (EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return -1;
        }
    }

    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, (int)pt_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_LEN, tag) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}

int aes_gcm_decrypt(const unsigned char key[AES_KEY_LEN],
                    const unsigned char iv[AES_IV_LEN],
                    const unsigned char *aad, size_t aad_len,
                    const unsigned char *ciphertext, size_t ct_len,
                    const unsigned char tag[AES_TAG_LEN],
                    unsigned char *plaintext)
{
    if (!key || !iv || !ciphertext || !tag || !plaintext) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (aad && aad_len > 0)
    {
        if (EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return -1;
        }
    }

    if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ct_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_LEN, (void *)tag) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1; // Authentication verification failed (tag mismatch or corrupted)
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return plaintext_len;
}
