#include "login_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <ctype.h>

static struct termios orig_termios;
static int raw_active = 0;

static void disable_raw(void)
{
    if (raw_active)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_active = 0;
        printf("\033[?25h"); // Show cursor
        fflush(stdout);
    }
}

static void enable_raw(void)
{
    if (!raw_active)
    {
        tcgetattr(STDIN_FILENO, &orig_termios);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_active = 1;
    }
}

static int read_key(void)
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c == 27) // Escape sequence
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27; // Bare ESC
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;

        if (seq[0] == '[')
        {
            if (seq[1] == 'A') return 1001; // UP
            if (seq[1] == 'B') return 1002; // DOWN
            if (seq[1] == 'C') return 1003; // RIGHT
            if (seq[1] == 'D') return 1004; // LEFT
        }
        return 27;
    }
    return (int)(unsigned char)c;
}

static int socket_recv_line(int sockfd, char *buf, size_t max_len)
{
    size_t i = 0;
    while (i < max_len - 1)
    {
        char c;
        int n = recv(sockfd, &c, 1, 0);
        if (n <= 0) return (i > 0) ? (int)i : -1;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

static int is_valid_user(const char *name)
{
    if (!name) return 0;
    size_t len = strlen(name);
    if (len < 2 || len > 10) return 0;
    for (size_t i = 0; i < len; ++i)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

// -------------------------------------------------------------
// TUI RENDERERS
// -------------------------------------------------------------

static void render_landing_menu(int selected)
{
    printf("\033[2J\033[H\033[?25l"); // Clear, home, hide cursor
    printf("\n");
    printf("  \033[1;36m┌───────────────────────────────────────────────────────────────┐\033[0m\n");
    printf("  \033[1;36m│\033[0m                   \033[1;37m* KUKUPU CHAT MESSENGER *\033[0m                   \033[1;36m│\033[0m\n");
    printf("  \033[1;36m│\033[0m            \033[2mNext-Gen Terminal Communication System\033[0m             \033[1;36m│\033[0m\n");
    printf("  \033[1;36m└───────────────────────────────────────────────────────────────┘\033[0m\n");
    printf("\n");
    printf("       \033[1;33mWelcome to Kukupu Chat.\033[0m Choose an option to enter:\n");
    printf("\n");
    printf("       \033[38;5;240m┌─────────────────────────────────────────────────────────┐\033[0m\n");
    printf("       \033[38;5;240m│                                                         │\033[0m\n");

    const char *opts[4] = {
        " [1] >> Quick Guest Login (Anonymous Join)             ",
        " [2] [#] Login to Registered Account                   ",
        " [3] [+] Register New Account (Persistent Identity)    ",
        " [4] [X] Exit Kukupu Chat                              "
    };

    const char *descs[4] = {
        "     Join public channel instantly without an account  ",
        "     Sign in securely with persistent username & pwd   ",
        "     Create a permanent account protected by PBKDF2    ",
        "     Close connection and return to terminal shell     "
    };

    for (int i = 0; i < 4; ++i)
    {
        if (i == selected)
        {
            printf("       \033[38;5;240m│\033[0m \033[1;46;30m%s\033[0m \033[38;5;240m│\033[0m\n", opts[i]);
            printf("       \033[38;5;240m│\033[0m \033[1;36m%s\033[0m \033[38;5;240m│\033[0m\n", descs[i]);
        }
        else
        {
            printf("       \033[38;5;240m│\033[0m \033[1;37m%s\033[0m \033[38;5;240m│\033[0m\n", opts[i]);
            printf("       \033[38;5;240m│\033[0m \033[2;37m%s\033[0m \033[38;5;240m│\033[0m\n", descs[i]);
        }
        if (i < 3)
            printf("       \033[38;5;240m│                                                         │\033[0m\n");
    }

    printf("       \033[38;5;240m│                                                         │\033[0m\n");
    printf("       \033[38;5;240m└─────────────────────────────────────────────────────────┘\033[0m\n");
    printf("\n");
    printf("     \033[2mUse [Up/Down/Tab] or [1-4] to select, [Enter] to confirm, [Ctrl+C] to quit\033[0m\n");
    fflush(stdout);
}

// -------------------------------------------------------------
// DIALOG: GUEST LOGIN
// -------------------------------------------------------------

static int dialog_guest_login(int sockfd, char *out_user, size_t ulen)
{
    char username[32] = "";
    char status[256] = "Enter a guest handle (2-10 chars: a-z, 0-9, _)";
    int status_err = 0;

    while (1)
    {
        printf("\033[2J\033[H\033[?25h"); // Clear, home, show cursor
        printf("\n");
        printf("  \033[1;36m┌───────────────────────────────────────────────────────────────┐\033[0m\n");
        printf("  \033[1;36m│\033[0m                 \033[1;37m>> QUICK GUEST LOGIN (ANONYMOUS)\033[0m              \033[1;36m│\033[0m\n");
        printf("  \033[1;36m└───────────────────────────────────────────────────────────────┘\033[0m\n");
        printf("\n");
        printf("       \033[38;5;240m┌─────────────────────────────────────────────────────────┐\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m│\033[0m   Guest Username: \033[1;32m%-38.38s\033[0m \033[38;5;240m│\033[0m\n", username);
        printf("       \033[38;5;240m│\033[0m   (Format: 2-10 alphanumeric characters or '_')         \033[38;5;240m│\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        if (status_err)
            printf("       \033[38;5;240m│\033[0m   \033[1;31m[!] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);
        else
            printf("       \033[38;5;240m│\033[0m   \033[1;33m[i] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m│\033[0m       \033[1;42;30m [ Enter ] Join Chat \033[0m        \033[1;41;37m [ Esc ] Back \033[0m       \033[38;5;240m│\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m└─────────────────────────────────────────────────────────┘\033[0m\n");
        fflush(stdout);

        int key = read_key();
        if (key == 27) return -1; // Cancel / Back
        if (key == '\n' || key == '\r')
        {
            if (!is_valid_user(username))
            {
                snprintf(status, sizeof(status), "Username must be 2-10 alphanumeric or _ chars");
                status_err = 1;
                continue;
            }

            // Send __GUEST__:<username>\n to server
            char pkt[128];
            snprintf(pkt, sizeof(pkt), "__GUEST__:%s\n", username);
            send(sockfd, pkt, strlen(pkt), 0);

            char resp[256];
            int n = socket_recv_line(sockfd, resp, sizeof(resp));
            if (n <= 0)
            {
                snprintf(status, sizeof(status), "Connection lost to server");
                status_err = 1;
                continue;
            }

            if (strncmp(resp, "__GUEST_OK__", 12) == 0)
            {
                snprintf(out_user, ulen, "%s", username);
                return 0; // Success!
            }
            else if (strncmp(resp, "__GUEST_ERR__:", 14) == 0)
            {
                snprintf(status, sizeof(status), "%.200s", resp + 14);
                status_err = 1;
                continue;
            }
            else
            {
                snprintf(status, sizeof(status), "Server: %.200s", resp);
                status_err = 1;
                continue;
            }
        }
        else if (key == 127 || key == 8) // Backspace
        {
            size_t len = strlen(username);
            if (len > 0) username[len - 1] = '\0';
        }
        else if (key >= 32 && key <= 126)
        {
            size_t len = strlen(username);
            if (len < 10 && ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == '_'))
            {
                username[len] = (char)key;
                username[len + 1] = '\0';
                status_err = 0;
            }
        }
    }
}

// -------------------------------------------------------------
// DIALOG: REGISTER NEW ACCOUNT
// -------------------------------------------------------------

static int dialog_register_account(int sockfd)
{
    char username[32] = "";
    char password[64] = "";
    char confirm[64] = "";
    int active_field = 0; // 0: username, 1: password, 2: confirm

    char status[256] = "Username: 2-10 chars (alphanumeric/_), Pwd >= 6";
    int status_err = 0;

    while (1)
    {
        printf("\033[2J\033[H\033[?25h");
        printf("\n");
        printf("  \033[1;36m┌───────────────────────────────────────────────────────────────┐\033[0m\n");
        printf("  \033[1;36m│\033[0m               \033[1;37m[+] CREATE NEW REGISTERED ACCOUNT\033[0m               \033[1;36m│\033[0m\n");
        printf("  \033[1;36m└───────────────────────────────────────────────────────────────┘\033[0m\n");
        printf("\n");
        printf("       \033[38;5;240m┌─────────────────────────────────────────────────────────┐\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");

        // Username row
        char u_field[64];
        snprintf(u_field, sizeof(u_field), "%-30s", username);
        if (active_field == 0)
            printf("       \033[38;5;240m│\033[0m   \033[1;36mDesired Username:\033[0m  \033[4;1;32m%-33.33s\033[0m  \033[38;5;240m│\033[0m\n", u_field);
        else
            printf("       \033[38;5;240m│\033[0m   Desired Username:  %-33.33s  \033[38;5;240m│\033[0m\n", u_field);

        // Password row (masked)
        char p_stars[64] = {0};
        for (size_t i = 0; i < strlen(password) && i < 30; ++i) p_stars[i] = '*';
        char p_field[64];
        snprintf(p_field, sizeof(p_field), "%-30s", p_stars);
        if (active_field == 1)
            printf("       \033[38;5;240m│\033[0m   \033[1;36mPassword        :\033[0m  \033[4;1;33m%-33.33s\033[0m  \033[38;5;240m│\033[0m\n", p_field);
        else
            printf("       \033[38;5;240m│\033[0m   Password        :  %-33.33s  \033[38;5;240m│\033[0m\n", p_field);

        // Confirm Password row (masked)
        char c_stars[64] = {0};
        for (size_t i = 0; i < strlen(confirm) && i < 30; ++i) c_stars[i] = '*';
        char c_field[64];
        snprintf(c_field, sizeof(c_field), "%-30s", c_stars);
        if (active_field == 2)
            printf("       \033[38;5;240m│\033[0m   \033[1;36mConfirm Password:\033[0m  \033[4;1;33m%-33.33s\033[0m  \033[38;5;240m│\033[0m\n", c_field);
        else
            printf("       \033[38;5;240m│\033[0m   Confirm Password:  %-33.33s  \033[38;5;240m│\033[0m\n", c_field);

        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        if (status_err)
            printf("       \033[38;5;240m│\033[0m   \033[1;31m[!] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);
        else
            printf("       \033[38;5;240m│\033[0m   \033[1;32m[i] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);

        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m│\033[0m  \033[1;42;30m [ Enter ] Register \033[0m  \033[1;46;30m [ Tab ] Next \033[0m   \033[1;41;37m [ Esc ] Back \033[0m  \033[38;5;240m│\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m└─────────────────────────────────────────────────────────┘\033[0m\n");
        fflush(stdout);

        int key = read_key();
        if (key == 27) return -1; // Cancel / Back
        if (key == '\t' || key == 1002) // Tab or DOWN
        {
            active_field = (active_field + 1) % 3;
            continue;
        }
        if (key == 1001) // UP
        {
            active_field = (active_field + 2) % 3;
            continue;
        }

        if (key == '\n' || key == '\r')
        {
            if (active_field < 2)
            {
                active_field++;
                continue;
            }

            // Submission validation
            if (!is_valid_user(username))
            {
                snprintf(status, sizeof(status), "Username must be 2-10 alphanumeric or _ chars");
                status_err = 1;
                active_field = 0;
                continue;
            }

            if (strlen(password) < 6)
            {
                snprintf(status, sizeof(status), "Password must be at least 6 characters");
                status_err = 1;
                active_field = 1;
                continue;
            }

            if (strcmp(password, confirm) != 0)
            {
                snprintf(status, sizeof(status), "Passwords do not match!");
                status_err = 1;
                active_field = 2;
                continue;
            }

            // Send __REGISTER__:<username>:<password>\n
            char pkt[256];
            snprintf(pkt, sizeof(pkt), "__REGISTER__:%s:%s\n", username, password);
            send(sockfd, pkt, strlen(pkt), 0);

            char resp[256];
            int n = socket_recv_line(sockfd, resp, sizeof(resp));
            if (n <= 0)
            {
                snprintf(status, sizeof(status), "Connection lost to server");
                status_err = 1;
                continue;
            }

            if (strncmp(resp, "__REG_OK__", 10) == 0)
            {
                printf("\033[2J\033[H\n");
                printf("  \033[1;32m┌───────────────────────────────────────────────────────────────┐\033[0m\n");
                printf("  \033[1;32m│\033[0m             \033[1;37m* ACCOUNT CREATED SUCCESSFULLY! *\033[0m                 \033[1;32m│\033[0m\n");
                printf("  \033[1;32m└───────────────────────────────────────────────────────────────┘\033[0m\n\n");
                printf("       Username '@\033[1;32m%s\033[0m' has been saved with salted PBKDF2.\n", username);
                printf("       Press \033[1;33m[Enter]\033[0m to return to the menu and login!\n\n");
                fflush(stdout);
                while (1)
                {
                    int k = read_key();
                    if (k == '\n' || k == '\r' || k == 27) break;
                }
                return 0;
            }
            else if (strncmp(resp, "__REG_ERR__:", 12) == 0)
            {
                snprintf(status, sizeof(status), "%.200s", resp + 12);
                status_err = 1;
                continue;
            }
            else
            {
                snprintf(status, sizeof(status), "Server: %.200s", resp);
                status_err = 1;
                continue;
            }
        }
        else if (key == 127 || key == 8) // Backspace
        {
            if (active_field == 0)
            {
                size_t len = strlen(username);
                if (len > 0) username[len - 1] = '\0';
            }
            else if (active_field == 1)
            {
                size_t len = strlen(password);
                if (len > 0) password[len - 1] = '\0';
            }
            else if (active_field == 2)
            {
                size_t len = strlen(confirm);
                if (len > 0) confirm[len - 1] = '\0';
            }
        }
        else if (key >= 32 && key <= 126)
        {
            if (active_field == 0)
            {
                size_t len = strlen(username);
                if (len < 10 && ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == '_'))
                {
                    username[len] = (char)key;
                    username[len + 1] = '\0';
                    status_err = 0;
                }
            }
            else if (active_field == 1)
            {
                size_t len = strlen(password);
                if (len < 60 && key != ':') // Colon is protocol separator
                {
                    password[len] = (char)key;
                    password[len + 1] = '\0';
                    status_err = 0;
                }
            }
            else if (active_field == 2)
            {
                size_t len = strlen(confirm);
                if (len < 60 && key != ':')
                {
                    confirm[len] = (char)key;
                    confirm[len + 1] = '\0';
                    status_err = 0;
                }
            }
        }
    }
}

// -------------------------------------------------------------
// DIALOG: LOGIN TO REGISTERED ACCOUNT
// -------------------------------------------------------------

static int dialog_registered_login(int sockfd, char *out_user, size_t ulen)
{
    char username[32] = "";
    char password[64] = "";
    int active_field = 0; // 0: username, 1: password

    char status[256] = "Enter your registered username and password";
    int status_err = 0;

    while (1)
    {
        printf("\033[2J\033[H\033[?25h");
        printf("\n");
        printf("  \033[1;36m┌───────────────────────────────────────────────────────────────┐\033[0m\n");
        printf("  \033[1;36m│\033[0m               \033[1;37m[#] SIGN IN TO REGISTERED ACCOUNT\033[0m               \033[1;36m│\033[0m\n");
        printf("  \033[1;36m└───────────────────────────────────────────────────────────────┘\033[0m\n");
        printf("\n");
        printf("       \033[38;5;240m┌─────────────────────────────────────────────────────────┐\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");

        // Username
        char u_field[64];
        snprintf(u_field, sizeof(u_field), "%-30s", username);
        if (active_field == 0)
            printf("       \033[38;5;240m│\033[0m   \033[1;36mUsername:\033[0m  \033[4;1;32m%-39.39s\033[0m    \033[38;5;240m│\033[0m\n", u_field);
        else
            printf("       \033[38;5;240m│\033[0m   Username:  %-39.39s    \033[38;5;240m│\033[0m\n", u_field);

        // Password
        char p_stars[64] = {0};
        for (size_t i = 0; i < strlen(password) && i < 30; ++i) p_stars[i] = '*';
        char p_field[64];
        snprintf(p_field, sizeof(p_field), "%-30s", p_stars);
        if (active_field == 1)
            printf("       \033[38;5;240m│\033[0m   \033[1;36mPassword:\033[0m  \033[4;1;33m%-39.39s\033[0m    \033[38;5;240m│\033[0m\n", p_field);
        else
            printf("       \033[38;5;240m│\033[0m   Password:  %-39.39s    \033[38;5;240m│\033[0m\n", p_field);

        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        if (status_err)
            printf("       \033[38;5;240m│\033[0m   \033[1;31m[!] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);
        else
            printf("       \033[38;5;240m│\033[0m   \033[1;33m[i] %-49.49s\033[0m \033[38;5;240m│\033[0m\n", status);

        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m│\033[0m   \033[1;42;30m [ Enter ] Sign In \033[0m  \033[1;46;30m [ Tab ] Next \033[0m   \033[1;41;37m [ Esc ] Back \033[0m  \033[38;5;240m│\033[0m\n");
        printf("       \033[38;5;240m│                                                         │\033[0m\n");
        printf("       \033[38;5;240m└─────────────────────────────────────────────────────────┘\033[0m\n");
        fflush(stdout);

        int key = read_key();
        if (key == 27) return -1; // Cancel / Back
        if (key == '\t' || key == 1002) // Tab / Down
        {
            active_field = (active_field + 1) % 2;
            continue;
        }
        if (key == 1001) // Up
        {
            active_field = (active_field + 1) % 2;
            continue;
        }

        if (key == '\n' || key == '\r')
        {
            if (active_field == 0)
            {
                active_field = 1;
                continue;
            }

            if (strlen(username) == 0 || strlen(password) == 0)
            {
                snprintf(status, sizeof(status), "Username and password cannot be empty");
                status_err = 1;
                continue;
            }

            // Send __LOGIN__:<username>:<password>\n
            char pkt[256];
            snprintf(pkt, sizeof(pkt), "__LOGIN__:%s:%s\n", username, password);
            send(sockfd, pkt, strlen(pkt), 0);

            char resp[256];
            int n = socket_recv_line(sockfd, resp, sizeof(resp));
            if (n <= 0)
            {
                snprintf(status, sizeof(status), "Connection lost to server");
                status_err = 1;
                continue;
            }

            if (strncmp(resp, "__LOGIN_OK__", 12) == 0)
            {
                snprintf(out_user, ulen, "%s", username);
                return 0; // Verified and Logged in!
            }
            else if (strncmp(resp, "__LOGIN_ERR__:", 14) == 0)
            {
                snprintf(status, sizeof(status), "%.200s", resp + 14);
                status_err = 1;
                continue;
            }
            else
            {
                snprintf(status, sizeof(status), "Server response: %.200s", resp);
                status_err = 1;
                continue;
            }
        }
        else if (key == 127 || key == 8) // Backspace
        {
            if (active_field == 0)
            {
                size_t len = strlen(username);
                if (len > 0) username[len - 1] = '\0';
            }
            else
            {
                size_t len = strlen(password);
                if (len > 0) password[len - 1] = '\0';
            }
        }
        else if (key >= 32 && key <= 126)
        {
            if (active_field == 0)
            {
                size_t len = strlen(username);
                if (len < 10 && ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == '_'))
                {
                    username[len] = (char)key;
                    username[len + 1] = '\0';
                    status_err = 0;
                }
            }
            else
            {
                size_t len = strlen(password);
                if (len < 60 && key != ':')
                {
                    password[len] = (char)key;
                    password[len + 1] = '\0';
                    status_err = 0;
                }
            }
        }
    }
}

// -------------------------------------------------------------
// MAIN ENTRY: RUN LOGIN TUI
// -------------------------------------------------------------

int run_login_tui(int sockfd, char *out_username, size_t ulen, int *out_is_registered)
{
    if (!out_username || ulen == 0 || !out_is_registered) return -1;

    enable_raw();

    int selected = 0;

    while (1)
    {
        render_landing_menu(selected);

        int key = read_key();
        if (key == 27 || key == 3) // ESC or Ctrl+C
        {
            disable_raw();
            return -1;
        }

        if (key == '1') { selected = 0; key = '\n'; }
        else if (key == '2') { selected = 1; key = '\n'; }
        else if (key == '3') { selected = 2; key = '\n'; }
        else if (key == '4') { selected = 3; key = '\n'; }

        if (key == 1001) // UP
        {
            selected = (selected + 3) % 4;
            continue;
        }
        if (key == 1002 || key == '\t') // DOWN / TAB
        {
            selected = (selected + 1) % 4;
            continue;
        }

        if (key == '\n' || key == '\r')
        {
            if (selected == 0) // Quick Guest Login
            {
                if (dialog_guest_login(sockfd, out_username, ulen) == 0)
                {
                    *out_is_registered = 0;
                    disable_raw();
                    return 0;
                }
            }
            else if (selected == 1) // Registered Account Login
            {
                if (dialog_registered_login(sockfd, out_username, ulen) == 0)
                {
                    *out_is_registered = 1;
                    disable_raw();
                    return 0;
                }
            }
            else if (selected == 2) // Register New Account
            {
                dialog_register_account(sockfd);
                // After registration, returns to landing menu so user can choose Login!
            }
            else if (selected == 3) // Exit
            {
                disable_raw();
                return -1;
            }
        }
    }
}
