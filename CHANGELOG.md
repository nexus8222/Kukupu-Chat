# Changelog

All notable changes to the **Kukupu Chat** project are documented in this file.

---

## [1.2.0] - 2026-10-23

### Added
- **End-to-End Encrypted File Streaming (`src/client/file_transfer.c`)**:
  - Encrypted file transfer payloads via AES-256-GCM AEAD using shared secrets derived via X25519 Elliptic-Curve Diffie-Hellman and HKDF-SHA256 (`KukupuFileTransferE2EE`).
  - Chunks carry encrypted bytes with unique 12-byte initialization vectors and 16-byte authentication tags inside Base64 wire envelopes.
- **Universal Transparent E2EE for Private Messages (`src/client/client.c`)**:
  - Upgraded `/msg <target> <text>` to automatically discover identity keys via `__KEYREQ__` and encrypt using the Double Ratchet session without requiring manual `/e2ee` toggles.
- **In-Place Message Editing and Deletion (`src/client/ui.c`, `include/ui.h`)**:
  - Implemented `ui_edit_last_message()` and `ui_delete_last_message()` in the client TUI engine.
  - Server broadcast notifications (`[EDITED]` and `[DELETED]`) are intercepted to directly mutate or remove the message in-place in the chat scrollback buffer rather than echoing duplicate lines.

### Fixed
- **Windows Console Emoji Rendering (`include/compat.h`, `src/client/ui.c`, `src/client/client.c`)**:
  - Added `platform_write_console()` utilizing `MultiByteToWideChar(CP_UTF8, ...)` and `WriteConsoleW()` to trigger Windows console wide-character rendering and native emoji font fallback (`Segoe UI Emoji`), resolving emoji corruption (`??`) on Windows.
  - Corrected `wrap_message()` in `ui.c` to never sever multi-byte UTF-8 continuation bytes across line boundaries.
  - Updated `visual_str_len()` to accurately calculate terminal column widths for multi-byte Unicode characters.
  - Enabled non-ASCII byte inputs in client terminal input loops to allow typing and pasting UTF-8 text.
- **File Transfer TCP Coalescing and Integrity Verification (`src/server/server.c`, `src/client/client.c`, `src/client/file_transfer.c`)**:
  - Implemented dynamic per-connection streaming line accumulators (`rx_buf`, `rx_len`) in both server and client to parse every newline-delimited command across fragmented or coalesced TCP packets, eliminating chunk drops during bulk transfers.
  - Added explicit seek alignment (`fseek(fp, chunk_seq * FILE_CHUNK_SIZE, SEEK_SET)`) before chunk writes.
  - Added transmission rate pacing (`usleep(5000)`) to maintain TCP stream stability.
  - Fixed false-positive verification reporting: client now checks `file_transfer_complete()` return status on `__FILEDONE__` and removes corrupted files upon checksum failure.

---

## [1.1.0] - 2026-10-21

### Added
- **Multi-OS Architecture (`include/compat.h`)**:
  - Unified cross-platform socket and console abstraction layer supporting both Linux (POSIX) and Windows (Winsock2).
  - Abstracted socket data types (`socket_t`, `INVALID_SOCKET_VAL`, `CLOSE_SOCKET`).
  - Cross-platform console raw mode and canonical mode switching.
  - Windows Console Virtual Terminal Processing (VT100 ANSI escape sequences) and UTF-8 console code page enablement (`CP_UTF8`).
  - Cross-platform filesystem operations (`cross_mkdir()`, `cross_basename()`, path separator macros).
  - Cross-platform sleep helpers (`platform_sleep_ms()`).
- **Windows Compilation & Build Automation**:
  - Added `make windows` Makefile target using MinGW-w64 (`x86_64-w64-mingw32-gcc`).
  - Fully static linking against OpenSSL (`-static -lssl -lcrypto -lws2_32 -lmswsock -lcrypt32 -lssp -pthread`) ensuring zero external runtime DLL dependencies.
  - Cross-compilation support on Linux for immediate Windows `.exe` production.
- **Pre-Built Standalone Binaries (`release/`)**:
  - `release/client.exe`: Statically linked, standalone Windows 64-bit client binary.
  - `release/server.exe`: Statically linked, standalone Windows 64-bit server binary.
  - `release/client`: Standalone Linux 64-bit client binary.
  - `release/server`: Standalone Linux 64-bit server daemon binary.
- **DNS Hostname Resolution & Interactive Connection**:
  - Enhanced client socket initialization using `getaddrinfo()` to resolve domain names (e.g. `tcp.pinggy.io`, `ngrok.io`).
  - Added support for both `<host> [port]` and `<host:port>` command-line arguments.
  - Added interactive prompt for Server IP/Host and Port when the client is double-clicked or launched without arguments.
- **Remote TCP Tunneling Support**:
  - Documented zero-cost, no-cloud hosting guides using Pinggy (`ssh -p 443 -R0:localhost:9001 tcp@a.pinggy.io`) and Ngrok TCP tunnels.

### Fixed
- **Windows Terminal UI Violent Screen Shaking**:
  - Fixed rapid screen-clearing busy loop caused by non-blocking `platform_read_char()` in `login_ui.c`.
  - Removed `!_kbhit()` check in Windows blocking read mode so `_getch()` waits for actual user input, eliminating 100% CPU spinning and screen flickering.
- **Windows Font and Border Scattering**:
  - Set active console input and output code pages to `CP_UTF8` (`SetConsoleOutputCP(CP_UTF8)`, `SetConsoleCP(CP_UTF8)`), correctly rendering Unicode border glyphs (Box Drawing characters).
  - Implemented UTF-8 multi-byte sequence length calculation (`visual_str_len()`) to ensure table borders never exceed the terminal width.
- **Terminal Auto-Scroll Jitter**:
  - Added terminal autowrap disable mode (`\033[?7l`) on startup and restored (`\033[?7h`) on exit.
  - Reserved a 1-row safety margin at the bottom of the Windows console buffer to prevent the OS console from auto-scrolling when writing to the bottom-right cell.
  - Hid cursor during full-screen redraw passes (`\033[?25l` / `\033[?25h`) to eliminate rendering artifacts.

### Security & Compatibility
- Maintained 100% backwards compatibility with Linux environments.
- Kept zero-warning compilation on all source modules under `-Wall -Wextra -O2`.
- Confirmed full test suite pass rate (`tests/test_e2e_full.c`) for Double Ratchet E2EE, chunked file transfers, PBKDF2 authentication, offline messaging, and token-bucket rate limiting.
