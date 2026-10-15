#include "file_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <time.h>

static file_transfer_t transfers[MAX_ACTIVE_TRANSFERS];
static int next_transfer_id = 1000;

void file_transfer_init()
{
    memset(transfers, 0, sizeof(transfers));
    mkdir("downloads", 0755);
}

void file_transfer_cleanup()
{
    for (int i = 0; i < MAX_ACTIVE_TRANSFERS; ++i)
    {
        if (transfers[i].fp)
        {
            fclose(transfers[i].fp);
            transfers[i].fp = NULL;
        }
    }
}

static file_transfer_t *find_transfer(int id, int is_sender)
{
    for (int i = 0; i < MAX_ACTIVE_TRANSFERS; ++i)
    {
        if (transfers[i].id == id && transfers[i].state != TRANSFER_INACTIVE && transfers[i].is_sender == is_sender)
            return &transfers[i];
    }
    return NULL;
}

static file_transfer_t *find_free_transfer()
{
    for (int i = 0; i < MAX_ACTIVE_TRANSFERS; ++i)
    {
        if (transfers[i].state == TRANSFER_INACTIVE ||
            transfers[i].state == TRANSFER_COMPLETED ||
            transfers[i].state == TRANSFER_FAILED)
        {
            if (transfers[i].fp)
            {
                fclose(transfers[i].fp);
                transfers[i].fp = NULL;
            }
            memset(&transfers[i], 0, sizeof(file_transfer_t));
            return &transfers[i];
        }
    }
    return NULL;
}

static file_progress_callback_t g_progress_cb = NULL;

void file_transfer_set_progress_callback(file_progress_callback_t cb)
{
    g_progress_cb = cb;
}

static void print_progress(const char *filename, int current, int total)
{
    if (g_progress_cb)
    {
        g_progress_cb(filename, current, total);
        return;
    }

    int percent = (total > 0) ? (current * 100 / total) : 0;
    if (percent > 100) percent = 100;
    int bar_width = 25;
    int pos = bar_width * percent / 100;

    printf("\r\033[K\033[1;36m[FILE: %s] [", filename);
    for (int i = 0; i < bar_width; ++i)
    {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% (%d/%d chunks)\033[0m\n", percent, current, total);
    fflush(stdout);
}

int file_transfer_start_send(const char *peer_username, const char *filepath, int sockfd)
{
    if (!peer_username || !filepath || sockfd < 0) return -1;

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        printf("\033[1;31m[FILE] Cannot open file '%s' for reading.\033[0m\n", filepath);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0)
    {
        printf("\033[1;31m[FILE] File '%s' is empty or invalid.\033[0m\n", filepath);
        fclose(fp);
        return -1;
    }

    file_transfer_t *t = find_free_transfer();
    if (!t)
    {
        printf("\033[1;31m[FILE] Max concurrent transfers reached.\033[0m\n");
        fclose(fp);
        return -1;
    }

    t->id = next_transfer_id++;
    t->is_sender = 1;
    strncpy(t->peer, peer_username, USERNAME_LEN - 1);
    strncpy(t->filepath, filepath, sizeof(t->filepath) - 1);
    char *base = basename((char *)filepath);
    strncpy(t->filename, base, sizeof(t->filename) - 1);
    t->filesize = (size_t)size;
    t->total_chunks = (int)((t->filesize + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
    t->current_chunk = 0;
    t->bytes_transferred = 0;
    t->state = TRANSFER_PENDING;
    t->fp = fp;
    t->last_activity = time(NULL);

    // Compute SHA-256
    compute_sha256_file(filepath, t->expected_sha256);

    // Send request to server: __FILEREQ__:<id>:<to>:<filename>:<filesize>:<sha256>
    char req[512];
    snprintf(req, sizeof(req), "__FILEREQ__:%d:%s:%s:%zu:%s\n",
             t->id, peer_username, t->filename, t->filesize, t->expected_sha256);
    send(sockfd, req, strlen(req), 0);

    printf("\033[1;33m[FILE] Sent transfer offer for '%s' (%zu bytes) to %s. Awaiting acceptance...\033[0m\n",
           t->filename, t->filesize, peer_username);
    return t->id;
}

int file_transfer_handle_request(int id, const char *from, const char *filename, size_t filesize, const char *sha256)
{
    file_transfer_t *t = find_free_transfer();
    if (!t) return -1;

    t->id = id;
    t->is_sender = 0;
    strncpy(t->peer, from, USERNAME_LEN - 1);
    strncpy(t->filename, filename, sizeof(t->filename) - 1);
    snprintf(t->filepath, sizeof(t->filepath), "downloads/%s", filename);
    t->filesize = filesize;
    t->total_chunks = (int)((filesize + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
    t->current_chunk = 0;
    t->bytes_transferred = 0;
    strncpy(t->expected_sha256, sha256, SHA256_HEX_LEN - 1);
    t->state = TRANSFER_PENDING;
    t->last_activity = time(NULL);

    printf("\n\033[1;35m========================================================\033[0m\n");
    printf("\033[1;35m[INCOMING FILE TRANSFER]\033[0m\n");
    printf(" From:     \033[1;32m%s\033[0m\n", from);
    printf(" File:     \033[1;36m%s\033[0m (%zu bytes, %d chunks)\n", filename, filesize, t->total_chunks);
    printf(" SHA-256:  %.16s...\n", sha256);
    printf(" Accept:   \033[1;32m/fileaccept %d\033[0m\n", id);
    printf(" Decline:  \033[1;31m/filedecline %d\033[0m\n", id);
    printf("\033[1;35m========================================================\033[0m\n");
    return 0;
}

int file_transfer_accept(int id, int sockfd)
{
    file_transfer_t *t = find_transfer(id, 0);
    if (!t || t->is_sender || t->state != TRANSFER_PENDING)
    {
        printf("[FILE] Invalid or expired transfer ID %d.\n", id);
        return -1;
    }

    t->fp = fopen(t->filepath, "wb");
    if (!t->fp)
    {
        printf("[FILE] Cannot open '%s' for writing.\n", t->filepath);
        t->state = TRANSFER_FAILED;
        return -1;
    }

    t->state = TRANSFER_ACTIVE;
    char accept_msg[128];
    snprintf(accept_msg, sizeof(accept_msg), "__FILEACCEPT__:%d:%s\n", id, t->peer);
    send(sockfd, accept_msg, strlen(accept_msg), 0);

    printf("\033[1;32m[FILE] Accepted file transfer #%d. Downloading to '%s'...\033[0m\n", id, t->filepath);
    return 0;
}

int file_transfer_decline(int id, int sockfd)
{
    file_transfer_t *t = find_transfer(id, 0);
    if (!t || t->is_sender)
    {
        printf("[FILE] Invalid transfer ID %d.\n", id);
        return -1;
    }

    t->state = TRANSFER_FAILED;
    char decline_msg[128];
    snprintf(decline_msg, sizeof(decline_msg), "__FILEDECLINE__:%d:%s\n", id, t->peer);
    send(sockfd, decline_msg, strlen(decline_msg), 0);

    printf("\033[1;33m[FILE] Declined file transfer #%d.\033[0m\n", id);
    return 0;
}

int file_transfer_handle_accept(int id, int sockfd)
{
    (void)sockfd;
    file_transfer_t *t = find_transfer(id, 1);
    if (!t || !t->is_sender) return -1;

    t->state = TRANSFER_ACTIVE;
    printf("\033[1;32m[FILE] %s accepted file '%s'. Beginning transmission...\033[0m\n", t->peer, t->filename);
    return 0;
}

void file_transfer_sender_tick(int sockfd)
{
    for (int i = 0; i < MAX_ACTIVE_TRANSFERS; ++i)
    {
        file_transfer_t *t = &transfers[i];
        if (t->state == TRANSFER_ACTIVE && t->is_sender && t->fp)
        {
            unsigned char chunk[FILE_CHUNK_SIZE];
            size_t bytes = fread(chunk, 1, sizeof(chunk), t->fp);
            if (bytes > 0)
            {
                char b64[FILE_CHUNK_SIZE * 2];
                base64_encode(chunk, bytes, b64, sizeof(b64));

                char packet[FILE_CHUNK_SIZE * 2 + 128];
                snprintf(packet, sizeof(packet), "__FILECHUNK__:%d:%s:%d:%d:%s\n",
                         t->id, t->peer, t->current_chunk, t->total_chunks, b64);
                send(sockfd, packet, strlen(packet), 0);

                t->current_chunk++;
                t->bytes_transferred += bytes;
                print_progress(t->filename, t->current_chunk, t->total_chunks);
                usleep(1000); // 1ms pacing
            }
            else
            {
                // Finished sending chunks
                char done_msg[128];
                snprintf(done_msg, sizeof(done_msg), "__FILEDONE__:%d:%s\n", t->id, t->peer);
                send(sockfd, done_msg, strlen(done_msg), 0);

                fclose(t->fp);
                t->fp = NULL;
                t->state = TRANSFER_COMPLETED;
                printf("\033[1;32m[FILE] File '%s' transfer completed successfully!\033[0m\n", t->filename);
            }
        }
    }
}

int file_transfer_receive_chunk(int id, int chunk_seq, int total_chunks, const char *b64_data)
{
    file_transfer_t *t = find_transfer(id, 0);
    if (!t || t->is_sender || t->state != TRANSFER_ACTIVE || !t->fp) return -1;

    unsigned char raw[FILE_CHUNK_SIZE * 2];
    size_t raw_len = 0;
    if (base64_decode(b64_data, raw, &raw_len) != 0 || raw_len == 0) return -1;

    fwrite(raw, 1, raw_len, t->fp);
    t->bytes_transferred += raw_len;
    t->current_chunk = chunk_seq + 1;
    t->total_chunks = total_chunks;

    print_progress(t->filename, t->current_chunk, t->total_chunks);
    return 0;
}

int file_transfer_complete(int id)
{
    file_transfer_t *t = find_transfer(id, 0);
    if (!t || t->is_sender || !t->fp) return -1;

    fclose(t->fp);
    t->fp = NULL;

    // Verify SHA-256
    char actual_sha256[SHA256_HEX_LEN];
    compute_sha256_file(t->filepath, actual_sha256);

    if (strcmp(actual_sha256, t->expected_sha256) == 0)
    {
        t->state = TRANSFER_COMPLETED;
        printf("\n\033[1;32m[FILE] Download complete: '%s' (%zu bytes)\033[0m\n", t->filepath, t->bytes_transferred);
        printf("\033[1;32m[FILE] Checksum verified: SHA-256 %s\033[0m\n", actual_sha256);
        return 0;
    }
    else
    {
        t->state = TRANSFER_FAILED;
        printf("\n\033[1;31m[FILE] CHECKSUM MISMATCH on '%s'!\033[0m\n", t->filepath);
        printf(" Expected: %s\n Actual:   %s\n", t->expected_sha256, actual_sha256);
        return -1;
    }
}
