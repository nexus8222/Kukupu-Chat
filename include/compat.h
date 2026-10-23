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

        CONSOLE_FONT_INFOEX cfi;
        memset(&cfi, 0, sizeof(cfi));
        cfi.cbSize = sizeof(cfi);
        cfi.dwFontSize.Y = 16;
        cfi.FontWeight = FW_NORMAL;
        wcscpy(cfi.FaceName, L"Cascadia Code");
        if (!SetCurrentConsoleFontEx(hOut, FALSE, &cfi)) {
            wcscpy(cfi.FaceName, L"Consolas");
            SetCurrentConsoleFontEx(hOut, FALSE, &cfi);
        }

        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
    }

    static inline void net_cleanup(void) {
        WSACleanup();
    }

    static inline void platform_write_console(const void *buf, size_t len) {
        if (!buf || len == 0) return;
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char *)buf, (int)len, NULL, 0);
            if (wlen > 0) {
                wchar_t *wbuf = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
                if (wbuf) {
                    MultiByteToWideChar(CP_UTF8, 0, (const char *)buf, (int)len, wbuf, wlen);
                    DWORD written = 0;
                    WriteConsoleW(hOut, wbuf, (DWORD)wlen, &written, NULL);
                    free(wbuf);
                    return;
                }
            }
        }
        _write(1, buf, (unsigned int)len);
    }

    static inline void platform_enable_raw_mode(void) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD in_mode = 0;
        if (GetConsoleMode(hIn, &in_mode)) {
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
            in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE);
            in_mode |= (ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS);
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
            in_mode |= (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
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
        static unsigned char q[32];
        static int qhead = 0;
        static int qtail = 0;

        if (qhead < qtail) {
            *out = q[qhead++];
            if (qhead == qtail) {
                qhead = qtail = 0;
            }
            return 1;
        }

        wint_t wch = _getwch();
        if (wch == 0 || wch == 224 || wch == 0xFFFF) {
            wint_t code = _getwch();
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

        // Check for UTF-16 surrogate pairs (astral characters including emojis)
        if (wch >= 0xD800 && wch <= 0xDBFF) {
            wint_t wch2 = _getwch();
            wchar_t wstr[3] = { (wchar_t)wch, (wchar_t)wch2, 0 };
            char utf8[16] = {0};
            int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr, 2, utf8, sizeof(utf8), NULL, NULL);
            if (ulen > 0) {
                *out = (unsigned char)utf8[0];
                for (int i = 1; i < ulen; ++i) {
                    q[qtail++] = (unsigned char)utf8[i];
                }
                return 1;
            }
        } else if (wch > 127) {
            // Non-ASCII wide character
            wchar_t wstr[2] = { (wchar_t)wch, 0 };
            char utf8[16] = {0};
            int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr, 1, utf8, sizeof(utf8), NULL, NULL);
            if (ulen > 0) {
                *out = (unsigned char)utf8[0];
                for (int i = 1; i < ulen; ++i) {
                    q[qtail++] = (unsigned char)utf8[i];
                }
                return 1;
            }
        }

        *out = (unsigned char)wch;
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

    static inline void platform_write_console(const void *buf, size_t len) {
        if (!buf || len == 0) return;
        ssize_t w = write(STDOUT_FILENO, buf, len);
        (void)w;
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

// Sound Alert Types
enum {
    SOUND_NOTIF_BELL = 0,
    SOUND_NOTIF_ACCEPT,
    SOUND_NOTIF_DECLINE,
    SOUND_NOTIF_MSG,
    SOUND_SUCCESS = SOUND_NOTIF_ACCEPT,
    SOUND_ALERT = SOUND_NOTIF_BELL,
    SOUND_ERROR = SOUND_NOTIF_DECLINE,
    SOUND_NOTIFICATION = SOUND_NOTIF_MSG
};

#ifdef _WIN32
static inline void platform_play_sound(int sound_type) {
    switch (sound_type) {
        case SOUND_NOTIF_ACCEPT:
            Beep(880, 80);
            Beep(1760, 100);
            break;
        case SOUND_NOTIF_DECLINE:
            Beep(440, 100);
            Beep(330, 120);
            break;
        case SOUND_NOTIF_MSG:
            Beep(1175, 70);
            break;
        case SOUND_NOTIF_BELL:
        default:
            MessageBeep(MB_ICONASTERISK);
            break;
    }
}
#else
static inline void platform_play_sound(int sound_type) {
    (void)sound_type;
    ssize_t w = write(STDOUT_FILENO, "\a", 1);
    (void)w;
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
