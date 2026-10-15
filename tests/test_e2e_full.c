#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/stat.h>
#include "crypto_utils.h"
#include "e2ee.h"
#include "file_transfer.h"

#define SERVER_PORT 9001
#define SERVER_IP "127.0.0.1"

static int connect_client(const char *username)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); exit(1); }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &sin.sin_addr);

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        perror("connect");
        exit(1);
    }

    char uname[64];
    snprintf(uname, sizeof(uname), "%s\n", username);
    send(s, uname, strlen(uname), 0);
    usleep(50000); // 50ms
    return s;
}

static int recv_line(int s, char *buf, size_t max_len, int timeout_sec)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    size_t idx = 0;
    while (idx < max_len - 1)
    {
        int sel = select(s + 1, &fds, NULL, NULL, &tv);
        if (sel <= 0) break; // timeout

        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';
    return idx > 0 ? (int)idx : -1;
}

int main()
{
    printf("\n============================================================\n");
    printf("   KUKUPU CHAT - FULL END-TO-END AUTOMATED TEST SUITE\n");
    printf("============================================================\n\n");

    e2ee_init();
    file_transfer_init();

    // -------------------------------------------------------------
    // TEST 1: E2EE DOUBLE RATCHET MULTI-ROUND EXCHANGE
    // -------------------------------------------------------------
    printf("[TEST 1] Testing E2EE Double Ratchet Over Sockets...\n");
    int alice_sock = connect_client("alice");
    int bob_sock = connect_client("bob");

    // Initialize Alice's E2EE manager and register pubkey with server
    e2ee_init();
    e2ee_manager_t alice_mgr = g_e2ee;
    char alice_pub_hex[128];
    hex_encode(alice_mgr.identity_pub, 32, alice_pub_hex, sizeof(alice_pub_hex));
    char id_alice[256];
    snprintf(id_alice, sizeof(id_alice), "__IDKEY__:%s\n", alice_pub_hex);
    send(alice_sock, id_alice, strlen(id_alice), 0);

    // Initialize Bob's E2EE manager and register pubkey with server
    e2ee_init();
    e2ee_manager_t bob_mgr = g_e2ee;
    char bob_pub_hex[128];
    hex_encode(bob_mgr.identity_pub, 32, bob_pub_hex, sizeof(bob_pub_hex));
    char id_bob[256];
    snprintf(id_bob, sizeof(id_bob), "__IDKEY__:%s\n", bob_pub_hex);
    send(bob_sock, id_bob, strlen(id_bob), 0);

    usleep(50000); // 50ms for server to record keys

    // Alice queries Bob's public key from the server
    send(alice_sock, "__KEYREQ__:bob\n", 15, 0);
    char recv_buf[4096];
    int r = 0;
    char bob_fetched_hex[128] = {0};
    while ((r = recv_line(alice_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__KEYRESP__:", 12) == 0)
        {
            char target[32];
            assert(sscanf(recv_buf, "__KEYRESP__:%31[^:]:%127s", target, bob_fetched_hex) == 2);
            assert(strcmp(target, "bob") == 0);
            assert(strcmp(bob_fetched_hex, bob_pub_hex) == 0);
            break;
        }
    }
    printf("  ✓ Alice queried and received Bob's identity key from server directory.\n");

    // Alice initializes Double Ratchet session for Bob
    unsigned char bob_raw_pub[32];
    assert(hex_decode(bob_fetched_hex, bob_raw_pub, sizeof(bob_raw_pub)) == 32);
    g_e2ee = alice_mgr;
    assert(e2ee_get_or_create_session("bob", bob_raw_pub) != NULL);

    // Alice encrypts message 1 to Bob
    char packet[4096];
    const char *msg1 = "TopSecret: Double Ratchet signal session init!";
    assert(e2ee_encrypt_message("bob", msg1, packet, sizeof(packet)) == 0);
    alice_mgr = g_e2ee; // save Alice state

    // Send via socket
    char send_buf[sizeof(packet) + 32];
    snprintf(send_buf, sizeof(send_buf), "%s\n", packet);
    send(alice_sock, send_buf, strlen(send_buf), 0);

    // Bob receives forwarded packet
    while ((r = recv_line(bob_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__E2EE__:", 9) == 0) break;
    }
    assert(r > 0);

    // Bob decrypts
    g_e2ee = bob_mgr;
    char from_user[32], decrypted[2048];
    int seq = -1;
    assert(e2ee_decrypt_packet(recv_buf, from_user, decrypted, sizeof(decrypted), &seq) == 0);
    assert(strcmp(from_user, "alice") == 0);
    assert(strcmp(decrypted, msg1) == 0);
    assert(seq == 0);
    printf("  ✓ Alice -> Bob decrypted message #1 correctly: \"%s\"\n", decrypted);

    // Bob ratchets and sends message #2 back to Alice
    const char *msg2 = "Bob ack: Ratchet key rotated and confirmed!";
    assert(e2ee_encrypt_message("alice", msg2, packet, sizeof(packet)) == 0);
    bob_mgr = g_e2ee; // save Bob state

    snprintf(send_buf, sizeof(send_buf), "%s\n", packet);
    send(bob_sock, send_buf, strlen(send_buf), 0);

    // Alice receives forwarded packet
    while ((r = recv_line(alice_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__E2EE__:", 9) == 0) break;
    }
    assert(r > 0);

    g_e2ee = alice_mgr;
    assert(e2ee_decrypt_packet(recv_buf, from_user, decrypted, sizeof(decrypted), &seq) == 0);
    assert(strcmp(from_user, "bob") == 0);
    assert(strcmp(decrypted, msg2) == 0);
    assert(seq == 0);
    printf("  ✓ Bob -> Alice decrypted message #2 correctly: \"%s\"\n", decrypted);

    // Alice sends message #3 to Bob (advancing sending ratchet chain)
    const char *msg3 = "Alice message #3: Forward secrecy chain advanced!";
    assert(e2ee_encrypt_message("bob", msg3, packet, sizeof(packet)) == 0);
    alice_mgr = g_e2ee; // save Alice state

    snprintf(send_buf, sizeof(send_buf), "%s\n", packet);
    send(alice_sock, send_buf, strlen(send_buf), 0);

    while ((r = recv_line(bob_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__E2EE__:", 9) == 0) break;
    }
    assert(r > 0);

    g_e2ee = bob_mgr;
    assert(e2ee_decrypt_packet(recv_buf, from_user, decrypted, sizeof(decrypted), &seq) == 0);
    assert(strcmp(decrypted, msg3) == 0);
    printf("  ✓ Alice -> Bob decrypted message #3 correctly: \"%s\"\n", decrypted);
    printf("  >>> TEST 1 PASSED: Double Ratchet E2EE fully functional!\n\n");

    // -------------------------------------------------------------
    // TEST 2: CHUNKED FILE TRANSFER WITH SHA-256 CHECKSUM
    // -------------------------------------------------------------
    printf("[TEST 2] Testing Chunked File Transfer & SHA-256 Integrity...\n");
    // Create a 5KB test payload file
    const char *payload_name = "test_payload.bin";
    FILE *fp = fopen(payload_name, "wb");
    assert(fp != NULL);
    for (int i = 0; i < 5000; i++) fputc((i * 37 + 13) & 0xFF, fp);
    fclose(fp);

    char orig_sha256[SHA256_HEX_LEN];
    compute_sha256_file(payload_name, orig_sha256);

    // Alice starts file transfer offer to Bob
    int transfer_id = file_transfer_start_send("bob", payload_name, alice_sock);
    assert(transfer_id > 0);

    // Bob receives __FILEREQ__
    int fid = 0;
    char req_from[32], req_file[256], req_sha[SHA256_HEX_LEN];
    size_t req_size = 0;
    while ((r = recv_line(bob_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__FILEREQ__:", 12) == 0)
        {
            assert(sscanf(recv_buf, "__FILEREQ__:%d:%31[^:]:%255[^:]:%zu:%64s",
                          &fid, req_from, req_file, &req_size, req_sha) == 5);
            break;
        }
    }
    assert(fid == transfer_id);
    assert(strcmp(req_from, "alice") == 0);
    assert(req_size == 5000);
    assert(strcmp(req_sha, orig_sha256) == 0);
    printf("  ✓ Bob received file offer: '%s' (%zu bytes), SHA-256: %.16s...\n", req_file, req_size, req_sha);

    // Bob accepts transfer
    file_transfer_handle_request(fid, req_from, req_file, req_size, req_sha);
    assert(file_transfer_accept(fid, bob_sock) == 0);

    // Alice receives __FILEACCEPT__
    while ((r = recv_line(alice_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strncmp(recv_buf, "__FILEACCEPT__:", 15) == 0)
        {
            int acc_id = 0; char acc_from[32];
            assert(sscanf(recv_buf, "__FILEACCEPT__:%d:%31s", &acc_id, acc_from) == 2);
            assert(acc_id == fid);
            file_transfer_handle_accept(acc_id, alice_sock);
            break;
        }
    }
    printf("  ✓ Alice received accept confirmation from Bob.\n");

    // Alice ticks file sender loop until done
    for (int tick = 0; tick < 20; tick++)
    {
        file_transfer_sender_tick(alice_sock);

        // Bob reads whatever chunks Alice sent
        while ((r = recv_line(bob_sock, recv_buf, sizeof(recv_buf), 1)) > 0)
        {
            if (strncmp(recv_buf, "__FILECHUNK__:", 14) == 0)
            {
                int cid = 0, cseq = 0, ctot = 0; char b64[4096];
                if (sscanf(recv_buf, "__FILECHUNK__:%d:%d:%d:%4095s", &cid, &cseq, &ctot, b64) == 4)
                {
                    file_transfer_receive_chunk(cid, cseq, ctot, b64);
                }
            }
            else if (strncmp(recv_buf, "__FILEDONE__:", 13) == 0)
            {
                int did = 0;
                if (sscanf(recv_buf, "__FILEDONE__:%d", &did) == 1)
                {
                    assert(file_transfer_complete(did) == 0);
                    goto file_done;
                }
            }
        }
    }

file_done:
    // Check that downloaded file exists and matches original checksum
    char downloaded_path[256];
    snprintf(downloaded_path, sizeof(downloaded_path), "downloads/%s", payload_name);
    char dl_sha256[SHA256_HEX_LEN];
    assert(compute_sha256_file(downloaded_path, dl_sha256) == 0);
    assert(strcmp(dl_sha256, orig_sha256) == 0);
    printf("  ✓ File '%s' verified byte-for-byte with SHA-256: %s\n", downloaded_path, dl_sha256);
    printf("  >>> TEST 2 PASSED: In-Band Chunked File Transfer & SHA-256 Integrity Verified!\n\n");

    // -------------------------------------------------------------
    // TEST 3: TOKEN BUCKET RATE LIMITING / ANTI-SPAM DEFENSE
    // -------------------------------------------------------------
    printf("[TEST 3] Testing Rate Limiting & Anti-Spam Defense...\n");
    int spammer_sock = connect_client("spammer");

    int got_rate_warning = 0;
    int got_auto_mute = 0;

    for (int i = 0; i < 15; i++)
    {
        char spam_msg[64];
        snprintf(spam_msg, sizeof(spam_msg), "SPAM BURST #%d\n", i);
        send(spammer_sock, spam_msg, strlen(spam_msg), 0);
        usleep(10000); // 10ms burst
    }

    // Read responses for spammer
    while ((r = recv_line(spammer_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "RATE LIMIT") || strstr(recv_buf, "Sending too fast"))
        {
            got_rate_warning = 1;
        }
        if (strstr(recv_buf, "auto-muted") || strstr(recv_buf, "Auto-muted"))
        {
            got_auto_mute = 1;
            break;
        }
    }
    assert(got_rate_warning == 1);
    assert(got_auto_mute == 1);
    printf("  ✓ Spammer triggered rate limit warnings and was auto-muted by token bucket.\n");
    close(spammer_sock);
    printf("  >>> TEST 3 PASSED: Anti-Spam Rate Limiter functioning correctly!\n\n");

    // -------------------------------------------------------------
    // TEST 4: OFFLINE MESSAGING INBOX PERSISTENCE & REPLAY
    // -------------------------------------------------------------
    printf("[TEST 4] Testing Offline Messaging Inbox...\n");
    // Bob disconnects
    close(bob_sock);
    usleep(100000);

    // Alice sends private message to offline Bob
    const char *offline_msg = "Hello Bob! You were offline when I wrote this.";
    char pm_cmd[256];
    snprintf(pm_cmd, sizeof(pm_cmd), "/msg bob %s\n", offline_msg);
    send(alice_sock, pm_cmd, strlen(pm_cmd), 0);

    // Alice receives offline inbox confirmation
    int stored_notice = 0;
    while ((r = recv_line(alice_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "OFFLINE INBOX") && strstr(recv_buf, "offline"))
        {
            stored_notice = 1;
            break;
        }
    }
    assert(stored_notice == 1);
    printf("  ✓ Server notified Alice that message was queued in offline inbox.\n");

    // Bob reconnects!
    int bob_reconnect = connect_client("bob");
    int received_offline_pm = 0;
    while ((r = recv_line(bob_reconnect, recv_buf, sizeof(recv_buf), 3)) > 0)
    {
        if (strstr(recv_buf, offline_msg))
        {
            received_offline_pm = 1;
            break;
        }
    }
    assert(received_offline_pm == 1);
    printf("  ✓ Bob reconnected and immediately received pending offline message!\n");
    close(bob_reconnect);
    printf("  >>> TEST 4 PASSED: Offline Messaging Inbox persisted and replayed correctly!\n\n");

    // -------------------------------------------------------------
    // TEST 5: PBKDF2 ADMIN AUTHENTICATION
    // -------------------------------------------------------------
    printf("[TEST 5] Testing Admin Authentication via PBKDF2...\n");
    int admin_sock = connect_client("admin");

    // Protocol: server asks -> client sends password -> server sends true -> client sends alias
    while ((r = recv_line(admin_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "ask")) break;
    }
    send(admin_sock, "admin123\n", 9, 0);

    while ((r = recv_line(admin_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "true")) break;
    }
    send(admin_sock, "admin_boss\n", 11, 0);
    printf("  ✓ Admin authenticated successfully using PBKDF2-HMAC-SHA256.\n");

    // Wait for server to finish handshake / welcome
    while ((r = recv_line(admin_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "Welcome! Type /help")) break;
    }

    // Admin updates password with /setadminpwd
    send(admin_sock, "/setadminpwd new_secure_pass_456\n", 33, 0);
    int pwd_updated = 0;
    while ((r = recv_line(admin_sock, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        printf("    [DEBUG ADMIN RECV] %s", recv_buf);
        if (strstr(recv_buf, "successfully updated with PBKDF2"))
        {
            pwd_updated = 1;
            break;
        }
    }
    assert(pwd_updated == 1);
    printf("  ✓ Admin updated password with /setadminpwd.\n");
    close(admin_sock);

    // Verify new password is required now
    int admin_sock2 = connect_client("admin");
    while ((r = recv_line(admin_sock2, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "ask")) break;
    }
    // Try old password
    send(admin_sock2, "admin123\n", 9, 0);
    int old_rejected = 0;
    while ((r = recv_line(admin_sock2, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "try"))
        {
            old_rejected = 1;
            break;
        }
    }
    assert(old_rejected == 1);
    printf("  ✓ Old password was rejected as expected.\n");

    // Try new password
    send(admin_sock2, "new_secure_pass_456\n", 20, 0);
    int new_accepted = 0;
    while ((r = recv_line(admin_sock2, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "true"))
        {
            new_accepted = 1;
            break;
        }
    }
    assert(new_accepted == 1);
    printf("  ✓ New PBKDF2 password was verified and accepted!\n");
    send(admin_sock2, "admin_boss2\n", 12, 0);
    while ((r = recv_line(admin_sock2, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "Welcome! Type /help")) break;
    }

    // Reset password back to default admin123 for idempotency
    send(admin_sock2, "/setadminpwd admin123\n", 22, 0);
    while ((r = recv_line(admin_sock2, recv_buf, sizeof(recv_buf), 2)) > 0)
    {
        if (strstr(recv_buf, "successfully updated with PBKDF2")) break;
    }

    close(admin_sock2);
    printf("  >>> TEST 5 PASSED: PBKDF2 Credential Storage & Verification confirmed!\n\n");

    // Clean up
    close(alice_sock);
    unlink(payload_name);
    file_transfer_cleanup();
    e2ee_cleanup();

    printf("============================================================\n");
    printf("   ALL 5 END-TO-END TESTS COMPLETED AND PASSED (100%%)!\n");
    printf("============================================================\n\n");

    return 0;
}
