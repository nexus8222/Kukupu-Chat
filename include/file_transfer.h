#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "common.h"
#include "crypto_utils.h"

#define MAX_ACTIVE_TRANSFERS 8
#define FILE_CHUNK_SIZE 1024

typedef enum {
    TRANSFER_INACTIVE = 0,
    TRANSFER_PENDING,
    TRANSFER_ACTIVE,
    TRANSFER_COMPLETED,
    TRANSFER_FAILED
} transfer_state_t;

typedef struct {
    int id;
    int is_sender;
    char peer[USERNAME_LEN];
    char filename[128];
    char filepath[256];
    size_t filesize;
    char expected_sha256[SHA256_HEX_LEN];
    
    FILE *fp;
    size_t bytes_transferred;
    int current_chunk;
    int total_chunks;
    transfer_state_t state;
    time_t last_activity;
} file_transfer_t;

void file_transfer_init();
void file_transfer_cleanup();

// Sender actions
int file_transfer_start_send(const char *peer_username, const char *filepath, int sockfd);
int file_transfer_handle_accept(int id, int sockfd);

// Receiver actions
int file_transfer_handle_request(int id, const char *from, const char *filename, size_t filesize, const char *sha256);
int file_transfer_accept(int id, int sockfd);
int file_transfer_decline(int id, int sockfd);
int file_transfer_receive_chunk(int id, int chunk_seq, int total_chunks, const char *b64_data);
int file_transfer_complete(int id);

// Background pump for sender chunks
void file_transfer_sender_tick(int sockfd);

typedef void (*file_progress_callback_t)(const char *filename, int current_chunk, int total_chunks);
void file_transfer_set_progress_callback(file_progress_callback_t cb);

#endif // FILE_TRANSFER_H
