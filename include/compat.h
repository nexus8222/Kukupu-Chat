#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #endif

    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <conio.h>
    #include <process.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    typedef SOCKET socket_t;
    #define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
    #define close(s) closesocket(s)
    #define usleep(us) Sleep((DWORD)((us) / 1000 > 0 ? (us) / 1000 : 1))
    #define sleep(s) Sleep((DWORD)((s) * 1000))
    #define mkdir(p, m) _mkdir(p)
    #define chmod(p, m) ((void)0)
    #define isatty _isatty
    #define write _write
    #define STDIN_FILENO 0
    #define STDOUT_FILENO 1
    #define STDERR_FILENO 2
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp

    #ifndef SHUT_RDWR
    #define SHUT_RDWR SD_BOTH
    #endif
    #ifndef SHUT_RD
    #define SHUT_RD SD_RECEIVE
    #endif
    #ifndef SHUT_WR
    #define SHUT_WR SD_SEND
    #endif

    static inline int net_init(void) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD out_mode = 0;
        if (GetConsoleMode(hOut, &out_mode)) {
            out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
            SetConsoleMode(hOut, out_mode);
        }

        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
    }

    static inline void net_cleanup(void) {
        WSACleanup();
    }

    static inline void platform_enable_raw_mode(void) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD in_mode = 0;
        if (GetConsoleMode(hIn, &in_mode)) {
            in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            SetConsoleMode(hIn, in_mode);
        }
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD out_mode = 0;
        if (GetConsoleMode(hOut, &out_mode)) {
            out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
            SetConsoleMode(hOut, out_mode);
        }
    }

    static inline void platform_disable_raw_mode(void) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD in_mode = 0;
        if (GetConsoleMode(hIn, &in_mode)) {
            in_mode |= (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            SetConsoleMode(hIn, in_mode);
        }
    }

    static inline int platform_wait_input(int timeout_ms) {
        int elapsed = 0;
        while (elapsed < timeout_ms) {
            if (_kbhit()) return 1;
            Sleep(10);
            elapsed += 10;
        }
        return _kbhit();
    }

    static inline int platform_read_char(unsigned char *out) {
        static unsigned char q[16];
        static int qhead = 0;
        static int qtail = 0;

        if (qhead < qtail) {
            *out = q[qhead++];
            if (qhead == qtail) {
                qhead = qtail = 0;
            }
            return 1;
        }

        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int code = _getch();
            *out = 27; // ESC
            q[qtail++] = '[';
            switch (code) {
                case 72: q[qtail++] = 'A'; break; // Up
                case 80: q[qtail++] = 'B'; break; // Down
                case 77: q[qtail++] = 'C'; break; // Right
                case 75: q[qtail++] = 'D'; break; // Left
                case 71: q[qtail++] = 'H'; break; // Home
                case 79: q[qtail++] = 'F'; break; // End
                case 73: q[qtail++] = '5'; q[qtail++] = '~'; break; // PageUp
                case 81: q[qtail++] = '6'; q[qtail++] = '~'; break; // PageDown
                case 83: q[qtail++] = '3'; q[qtail++] = '~'; break; // Delete
                default: break;
            }
            return 1;
        }
        *out = (unsigned char)ch;
        return 1;
    }

#else
    #define _GNU_SOURCE
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/select.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <strings.h>
    #include <termios.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    typedef int socket_t;
    #define IS_VALID_SOCKET(s) ((s) >= 0)

    static inline int net_init(void) {
        return 0;
    }

    static inline void net_cleanup(void) {
        // No-op on POSIX
    }

    static struct termios orig_termios_state;
    static int orig_termios_saved = 0;

    static inline void platform_disable_raw_mode(void) {
        if (orig_termios_saved) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios_state);
        }
    }

    static inline void platform_enable_raw_mode(void) {
        if (!orig_termios_saved) {
            tcgetattr(STDIN_FILENO, &orig_termios_state);
            orig_termios_saved = 1;
            atexit(platform_disable_raw_mode);
        }
        struct termios raw = orig_termios_state;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    static inline int platform_wait_input(int timeout_ms) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int sel = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        return (sel > 0 && FD_ISSET(STDIN_FILENO, &readfds));
    }

    static inline int platform_read_char(unsigned char *out) {
        ssize_t n = read(STDIN_FILENO, out, 1);
        return (n == 1) ? 1 : 0;
    }
#endif

// Safe cross-platform basename (handles both '/' and '\' without modifying input)
static inline const char *cross_basename(const char *path) {
    if (!path) return "";
    const char *p1 = strrchr(path, '/');
    const char *p2 = strrchr(path, '\\');
    const char *base = (p1 > p2) ? p1 : p2;
    return base ? (base + 1) : path;
}

#endif // COMPAT_H
