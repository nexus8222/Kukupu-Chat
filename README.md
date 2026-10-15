# Kukupu Chat
### Production-Grade Concurrent Terminal Chat Ecosystem in Pure C

[![Language](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.cppreference.com/w/c)
[![Security](https://img.shields.io/badge/Security-OpenSSL%203.0-green.svg)](https://www.openssl.org/)
[![Protocol](https://img.shields.io/badge/Cryptography-Signal%20Double%20Ratchet-blueviolet.svg)](https://signal.org/docs/specifications/doubleratchet/)
[![Interface](https://img.shields.io/badge/Interface-Full--Screen%20TUI-cyan.svg)]()
[![Build](https://img.shields.io/badge/Build-Passing%20(0%20warnings)-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)]()

A high-performance, multithreaded terminal chat server and client written in pure C using POSIX sockets, `pthread` concurrency, and OpenSSL 3.0. Features a **full-screen terminal chat interface (TUI)** with zero-flicker double buffering and split-pane layout, **Signal-style Double Ratchet End-to-End Encryption (E2EE)**, **in-band chunked file streaming with SHA-256 integrity verification**, **Token Bucket anti-spam rate limiting**, **PBKDF2-HMAC-SHA256 administrative authentication**, **ephemeral vanishing messages**, **multi-room parties**, and **raw-mode terminal controls** (history recall, cursor editing, Tab autocompletion, PageUp/PageDown scrollback, and live typing indicators).

---

## Table of Contents
- [Key Features](#key-features)
- [Terminal User Interface (TUI) Layout](#terminal-user-interface-tui-layout)
- [Architecture & Cryptographic Design](#architecture--cryptographic-design)
- [Project Structure](#project-structure)
- [Prerequisites & Dependencies](#prerequisites--dependencies)
- [Build & Quick Start](#build--quick-start)
- [Docker Deployment](#docker-deployment)
- [Command Reference](#command-reference)
- [Keyboard Controls & Shortcuts](#keyboard-controls--shortcuts)
- [Internal Wire Protocol](#internal-wire-protocol)
- [Automated Verification Suite](#automated-verification-suite)
- [Developer & License](#developer--license)

---

## Key Features

### [1] Signal-Style Double Ratchet E2EE
- **End-to-End Encryption**: Zero server-side plaintext visibility.
- **Diffie-Hellman Ratchet**: Curve25519 (`X25519`) dynamic key exchange on alternating rounds.
- **Symmetric Chain Ratchet**: HKDF-SHA256 message and chain key derivations (`WhisperRatchetRoot`, `WhisperRatchetChain`, `WhisperMessageKeys`).
- **Authenticated Encryption**: AES-256-GCM AEAD with 12-byte initialization vectors and 16-byte authentication tags.
- **Forward Secrecy & Break-in Recovery**: Old keys cannot decrypt future messages; past keys cannot be derived from a compromised ephemeral state.
- **Public Key Directory**: Automatic identity key broadcast and directory querying via `__IDKEY__` and `__KEYREQ__`.

### [2] Chunked In-Band File Transfer
- **In-Band Slicing**: Files are split into 1024-byte chunks and streamed via Base64 wire packets.
- **Negotiation Protocol**: Senders issue transfer offers; receivers interactively accept (`/fileaccept <id>`) or decline (`/filedecline <id>`).
- **Integrity Verification**: Automatic SHA-256 checksum comparison on the receiving client before saving into `downloads/`.
- **Live Terminal Progress Bar**: Dynamic ASCII progress bar (`[FILE XFER] report.pdf [=============>   ] 75%`) rendered directly in the TUI status pane.

### [3] Anti-Spam Rate Limiting & DoS Defense
- **Token Bucket Algorithm**: 5.0 token capacity with continuous 1.5 tokens/sec replenishment per client.
- **Automated Mute Enforcement**: Warns users when tokens deplete; automatically applies a 15-second mute upon 3 consecutive burst violations.
- **Per-IP Connection Throttling**: Restricts maximum concurrent TCP connections per IP address to mitigate socket-exhaustion DoS attacks.
- **Protocol Fast-Path**: Internal control messages (`__FILECHUNK__`, `__TYPING__`, `__E2EE__`) bypass human rate limits to prevent file stalls.

### [4] Persistent User Accounts & Secure PBKDF2 Storage
- **Salted PBKDF2-HMAC-SHA256**: 100,000 iterations with 16-byte cryptographically secure random salt generated via OpenSSL `RAND_bytes()`.
- **Strict Username Policy**: 2-10 characters, strictly alphanumeric and underscore (`_`).
- **Account Isolation**: Guest users cannot claim or impersonate registered usernames. Concurrent logins of active registered users are blocked.
- **Dedicated Credential Vault**: Stored in `accounts.dat` with `chmod 0600` access controls and thread-safe file locking.
- **Persistent Offline Store**: Private messages to offline users are queued in `offline_inbox.dat` and replayed immediately upon reconnection with original timestamps.

### [5] Interactive TUI Landing Screen & Modals
- **Interactive Menu**: Launch screen featuring keyboard navigation (`Up/Down/Tab`, `1-4`, `Enter`, `Esc`).
- **Guest vs. Member**: Quick Guest Login for anonymous instant access or Member Login with masked password dialogs (`*`).
- **In-App Account Registration**: Modal dialog for account creation with validation and confirmation checks.
- **Non-Interactive Fallback**: Uses `isatty()` to support automated testing and scripted socket piping seamlessly.

### [6] Multi-Room Party System
- **Isolated Channels**: Create, join, or leave isolated chat rooms (`/createparty`, `/joinparty`, `/leaveparty`).
- **Room Access Control**: Lock rooms with `/lockparty` or invite specific users with `/inviteparty`.

### [7] Vanishing Messages & Mini-Games
- **Self-Destructing Messages**: Send messages that automatically vanish after a specified TTL (`/vanish <sec> <msg>`).
- **Interactive Mini-Games**: Multiplayer number-guessing game with turn cycling and scoreboards (`/game`).

### [8] Professional Full-Screen Terminal UI (TUI)
- **Alternate Screen Buffer**: Uses `\033[?1049h` and `\033[?1049l` to preserve standard shell scrollback.
- **Flicker-Free Double Buffering**: Pre-renders entire screen frame into an off-screen buffer before a single atomic flush.
- **Split-Pane Architecture**: Top header bar, main chat viewport, collapsible members sidebar, dedicated status bar, and pinned input box.
- **Dynamic Resize Support**: Automatically adapts to window geometry changes via `SIGWINCH` and `TIOCGWINSZ`.
- **Zero Emojis Policy**: Clean, high-contrast ASCII and UTF-8 box characters (`┌`, `─`, `┐`, `│`, `└`, `┘`, `╭`, `╰`, `╮`, `╯`, `├`, `┤`, `*`, `>`).

---

## Terminal User Interface (TUI) Layout

```text
┌── [KUKUPU CHAT] ── Room: #0000 ── [E2EE: ACTIVE] ── User: @alice ──────────────────┐
│ [14:20] --- [SYS] Connected to Kukupu Chat.                     │ [MEMBERS (2)]    │
│ [14:20] --- [SYS] Signal Double Ratchet E2EE active.            │ * alice (you)    │
│ [14:21] <bob> Hey Alice, how is the project going?              │ * bob            │
│ [14:21] [YOU] > Working on the terminal user interface!         │ ─────────────────│
│ [14:22] [E2EE:bob] > Double Ratchet forward secrecy is verified!│ [CHANNEL #0000]  │
│ [14:22] [FILE:bob] Offer: 'report.pdf' (120 KB) -> Type /fileaccept 1000           │
│                                                                 │ Mode: Public     │
├─ [STATUS] Typing: bob is typing a message... ──────────────────────────────────────┤
╭─ Message #0000 ────────────────────────────────────────────────────────────────────╮
│ >                                                                                  │
╰─ [Tab] Autocomplete  •  [PgUp/PgDn] Scroll  •  [/exit] Quit ───────────────────────╯
```

### UI Panes & Components:
1. **Header Bar**: Displays application brand, current channel (`Room: #0000`), E2EE status badge, and username handle.
2. **Main Viewport**: Formatted message log with timestamp tags (`[HH:MM]`), clean badges (`<bob>`, `[YOU]`, `[E2EE:bob]`, `--- [SYS] ---`, `[FILE:bob]`), and automatic word-wrapping.
3. **Members Sidebar**: 22-column right panel displaying online user list, self indicator (`* user (you)`), and room modes. Automatically collapses on compact screens (< 80 columns).
4. **Status / Typing Divider**: Dedicated status bar for peer typing indicators (`Typing: bob is typing...`) and active file transfer progress bars.
5. **Pinned Input Container**: Clean rounded container (`╭───╮ │ > ... │ ╰───╯`) with full cursor navigation and autocomplete preview hints.

---

## Architecture & Cryptographic Design

```
+-----------------------------------------------------------------------------+
|                           CLIENT TERMINAL (Alice)                           |
|  [TUI Engine (ui.c)]  <--->  [Double Ratchet Engine]  <--->  [File Streamer] |
+-----------------------------------------------------------------------------+
                                      |
                     TCP / POSIX Sockets (Port 9001)
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                             CHAT SERVER DAEMON                              |
|  - Token Bucket Rate Limiter          - PBKDF2 Credential Engine            |
|  - Per-IP DoS Connection Guard        - Public Identity Key Directory       |
|  - Multi-Room Party Router            - Offline Message Inbox Store         |
|  - Moderation Arsenal (Kick/Ban/Mute) - Internal Protocol Dispatcher        |
+-----------------------------------------------------------------------------+
                                      |
                     TCP / POSIX Sockets (Port 9001)
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                            CLIENT TERMINAL (Bob)                            |
|  [TUI Engine (ui.c)]  <--->  [Double Ratchet Engine]  <--->  [File Receiver] |
+-----------------------------------------------------------------------------+
```

---

## Project Structure

```text
kukupu-chat/
├── Makefile                # Unified build system with modular target compilation
├── Dockerfile              # Containerized build & isolated runtime definition
├── docker-compose.yml      # Orchestration definition for single-command deploy
├── README.md               # Comprehensive architectural documentation & guides
├── checklist               # Development tracking roadmap
│
├── include/                # Header definitions & public APIs
│   ├── admin.h             # Moderation control declarations
│   ├── banlist.h           # IP ban list manager definitions
│   ├── client.h            # Client session & connection definitions
│   ├── commands.h          # Command dispatch declarations
│   ├── common.h            # Protocol constants, buffer limits & color palette
│   ├── crypto_utils.h      # OpenSSL 3.0 X25519, HKDF, AES-256-GCM, PBKDF2 APIs
│   ├── db.h                # PBKDF2 credential storage, offline inbox & key store
│   ├── e2ee.h              # Signal Double Ratchet protocol declarations
│   ├── emoji.h             # 80-item categorized ASCII/Unicode catalog
│   ├── file_transfer.h     # Chunked file streaming & SHA-256 integrity APIs
│   ├── game.h              # Number guessing mini-game engine
│   ├── lastseen.h          # Binary serialized logout tracker definitions
│   ├── log.h               # Server event and audit logging APIs
│   ├── login_ui.h          # TUI Landing Page & authentication modal definitions
│   ├── motd.h              # Message of the Day definitions
│   ├── mute.h              # User muting logic and mutelist definitions
│   ├── party.h             # Multi-room channel management declarations
│   ├── pwdgen.h            # Administrative credential helper functions
│   ├── ratelimit.h         # Token bucket & IP rate limiting headers
│   ├── ui.h                # Terminal user interface (TUI) engine definitions
│   ├── utils.h             # Server broadcast and client search utilities
│   └── vanish.h            # Ephemeral message self-destruct definitions
│
├── src/                    # Modular source code implementations
│   ├── common/             # Shared cryptographic & catalog logic
│   │   ├── crypto_utils.c  # OpenSSL 3.0 wrappers & primitives
│   │   └── emoji.c         # Emoji and symbol catalog definitions
│   ├── client/             # Client application & terminal interface
│   │   ├── client.c        # Event loop, socket multiplexing & tab completion
│   │   ├── ui.c            # Full-screen TUI rendering engine
│   │   ├── login_ui.c      # Interactive TUI landing page, guest & login dialogs
│   │   ├── e2ee.c          # Double Ratchet state machine & key management
│   │   └── file_transfer.c # Base64 chunk streaming & verification engine
│   └── server/             # Server daemon & moderation subsystems
│       ├── server.c        # Main socket listener & worker threads
│       ├── admin.c         # Administrative moderation handlers
│       ├── commands.c      # Slash command routing table
│       ├── banlist.c       # Persistent IP ban management
│       ├── db.c            # Secure credential database & inbox storage
│       ├── game.c          # Turn-based guessing game
│       ├── lastseen.c      # Last-seen persistence engine
│       ├── log.c           # Real-time event logging
│       ├── motd.c          # MOTD loader & live updater
│       ├── mute.c          # Mute list enforcement
│       ├── party.c         # Room and channel isolation logic
│       ├── pwdgen.c        # Credential hashing & generation
│       ├── ratelimit.c     # Token bucket anti-spam algorithm
│       ├── utils.c         # Message broadcasting & user lookups
│       └── vanish.c        # Ephemeral message decay thread
│
└── tests/                  # Automated verification test suites
    ├── test_e2e_full.c     # Socket-level integration suite (E2EE, files, inbox, auth)
    ├── test_all_commands.py# Full coverage test suite for all /help commands
    └── test_auth_and_features.py # Automated suite for PBKDF2 accounts & /clear
```

---

## Prerequisites & Dependencies

- **Operating System**: Linux (Ubuntu 20.04+, Debian, Arch, Fedora) or POSIX-compliant environment.
- **Compiler**: GCC 9.0+ or Clang 10.0+ with C11 support.
- **Libraries**:
  - `pthread` (POSIX Threads)
  - `libssl` & `libcrypto` (OpenSSL 3.0+)

### Installing Dependencies on Ubuntu / Debian:
```bash
sudo apt-get update
sudo apt-get install -y build-essential libssl-dev
```

---

## Build & Quick Start

### 1. Compile Everything
```bash
make clean && make
```
*Compiles both `server` and `client` binaries with zero warnings under `-Wall -Wextra -pthread -O2`.*

### 2. Start the Server Daemon
```bash
./server
```
- Binds to `0.0.0.0:9001`.
- Initializes PBKDF2 admin credentials in `admin.cred` (default: `admin123`).

### 3. Connect Interactive Clients
Open a new terminal window for each client:

```bash
# Launch interactive client
./client 127.0.0.1
```
The interactive TUI landing page launches immediately:
- Select **`[1] >> Quick Guest Login`** to join public chat instantly as a guest handle.
- Select **`[2] [#] Login to Registered Account`** to enter your username & password.
- Select **`[3] [+] Register New Account`** to create a persistent account with salted PBKDF2.

### 4. Connect as Administrator
```bash
./client 127.0.0.1
Enter your username: admin
Enter Admin Password: admin123
Enter Joining Alias: root_admin
```

---

## Docker Deployment

Run the chat server effortlessly using Docker:

```bash
# Build and run the server container
docker compose up --build -d

# View server logs
docker compose logs -f

# Stop the server container
docker compose down
```

---

## Command Reference

### User & Communication Commands
| Command | Syntax | Description |
|:---|:---|:---|
| `/help` | `/help` | Displays available commands. |
| `/msg` | `/msg <user> <message>` | Sends a private message to a specific user. |
| `/e2ee` | `/e2ee <user> <message>` | Sends a Signal Double Ratchet end-to-end encrypted message. |
| `/sendfile` | `/sendfile <user> <filepath>` | Sends a file via chunked streaming with SHA-256 verification. |
| `/fileaccept` | `/fileaccept <transfer_id>` | Accepts an incoming file transfer offer. |
| `/filedecline` | `/filedecline <transfer_id>` | Declines an incoming file transfer offer. |
| `/vanish` | `/vanish <sec> <message>` | Sends an ephemeral message that self-destructs after `<sec>` seconds. |
| `/users` | `/users` | Lists all currently connected online users. |
| `/whoami` | `/whoami` | Displays your current username, IP, and room code. |
| `/lastseen` | `/lastseen <user>` | Displays the last logout timestamp for a user. |
| `/emoji` | `/emoji [id]` | Displays the emoji palette or sends a selected emoji. |
| `/setcolor` | `/setcolor <color>` | Sets custom ANSI chat color (`red`, `green`, `yellow`, `blue`, `magenta`, `cyan`). |
| `/game` | `/game [guess]` | Starts or plays the turn-based number guessing game. |
| `/time` | `/time` | Prints the server's local date and time. |
| `/uptime` | `/uptime` | Displays server operational uptime. |
| `/clear` | `/clear` | Clears the terminal screen buffer. |
| `/exit` | `/exit` | Gracefully disconnects from the chat server. |

### Multi-Room Party Commands
| Command | Syntax | Description |
|:---|:---|:---|
| `/createparty` | `/createparty` | Creates a new isolated chat room with a random 4-digit code. |
| `/joinparty` | `/joinparty <code>` | Joins an existing party room. |
| `/leaveparty` | `/leaveparty` | Leaves the current party and returns to the public lounge (`0000`). |
| `/partyusers` | `/partyusers` | Lists members inside your current party room. |
| `/lockparty` | `/lockparty` | Locks the party room so only invited members can join. |
| `/inviteparty` | `/inviteparty <user>` | Invites a user into your locked party room. |

### Administrator Commands
| Command | Syntax | Description |
|:---|:---|:---|
| `/kick` | `/kick <user>` | Forcibly disconnects a user's TCP connection. |
| `/ban` | `/ban <user>` / `/ban <ip>` | Bans a user or IP address; persisted in `banned.txt`. |
| `/unban` | `/unban <ip>` | Removes an IP from the ban list. |
| `/banlist` | `/banlist` | Dumps all currently banned IP addresses. |
| `/mute` | `/mute <user>` | Suppresses a user from sending public or private messages. |
| `/unmute` | `/unmute <user>` | Restores a muted user's messaging privileges. |
| `/mutelist` | `/mutelist` | Lists all currently muted operators. |
| `/whois` | `/whois <user>` | Displays user metadata (IP address, party, admin status, session duration). |
| `/broadcast` | `/broadcast <message>` | Sends an administrative broadcast across all rooms. |
| `/pin` | `/pin <message>` | Sets a sticky banner shown to all connecting clients. |
| `/setmotd` | `/setmotd <message>` | Updates the Message of the Day; persisted in `motd.txt`. |
| `/setadminpwd` | `/setadminpwd <newpwd>` | Dynamically updates the server PBKDF2 administrative password. |
| `/log` | `/log` | Streams the contents of `server.log` to the admin terminal. |
| `/shutdown` | `/shutdown` | Gracefully shuts down the server daemon. |

---

## Keyboard Controls & Shortcuts

| Key / Shortcut | Action | Description |
|:---|:---|:---|
| `PageUp` | Scroll Up | Scrolls viewport upwards into message history (up to 600 buffered lines). |
| `PageDown` | Scroll Down | Scrolls viewport downwards toward latest messages. |
| `Tab` | Autocomplete | Auto-completes slash commands (`/msg`, `/e2ee`, `/sendfile`, etc.). |
| `Up Arrow` | History Previous | Recalls previously sent commands and messages into the input line. |
| `Down Arrow` | History Next | Cycles forward through recent input command history. |
| `Left Arrow` | Cursor Left | Shifts editing cursor leftwards within the current input string. |
| `Right Arrow` | Cursor Right | Shifts editing cursor rightwards within the current input string. |
| `Home` / `Ctrl+A` | Line Start | Moves cursor to the start of the input prompt. |
| `End` / `Ctrl+E` | Line End | Moves cursor to the end of the input prompt. |
| `Ctrl+L` | Redraw Screen | Forces a complete recalculation and double-buffered frame redraw. |
| `Ctrl+C` | Graceful Exit | Exits the client, restores terminal raw mode and resets screen buffer. |

---

## Internal Wire Protocol

The server uses an internal protocol router (`handle_internal_protocol`) to differentiate between human chat inputs and subsystem control packets:

| Header | Format | Purpose |
|:---|:---|:---|
| `__IDKEY__` | `__IDKEY__:<pubkey_hex>` | Registers client's Curve25519 identity key on connect. |
| `__KEYREQ__` | `__KEYREQ__:<username>` | Requests a user's public identity key from the directory. |
| `__KEYRESP__` | `__KEYRESP__:<username>:<pubkey_hex>` | Server response containing the requested public identity key. |
| `__E2EE__` | `__E2EE__:<from>:<to>:<ik>:<ratchet>:<seq>:<iv>:<ct>:<tag>` | Double Ratchet encrypted envelope (AES-256-GCM). |
| `__FILEREQ__` | `__FILEREQ__:<id>:<to>:<filename>:<size>:<sha256>` | Proposes an in-band file transfer offer. |
| `__FILEACCEPT__` | `__FILEACCEPT__:<id>:<from>` | Accepts a pending file transfer session. |
| `__FILEDECLINE__` | `__FILEDECLINE__:<id>:<from>` | Declines a pending file transfer session. |
| `__FILECHUNK__` | `__FILECHUNK__:<id>:<target>:<seq>:<total>:<base64>` | Streams a 1024-byte Base64-encoded chunk. |
| `__FILEDONE__` | `__FILEDONE__:<id>:<target>` | Signals completion of transmission. |
| `__TYPING__` | `__TYPING__:<username>` | Broadcasts typing status indicator within the room. |

---

## Automated Verification Suite

The repository includes a comprehensive, multi-client socket-level integration test suite in [`test_e2e_full.c`](test_e2e_full.c). It tests concurrent clients connected over live TCP sockets against the server.

### Run the Test Suites:
```bash
# 1. Run the full C integration test suite (automatically starts & stops test server):
make test

# 2. Run the /help command coverage suite (tests all 44 commands over live sockets):
make test-commands

# 3. Run the persistent accounts, PBKDF2 hashing & guest isolation test suite:
make test-auth

# 4. Run all test suites together:
make test-all
```

### Verification Coverage:
1. **Double Ratchet E2EE**: Key directory discovery, multi-round message encryption/decryption, DH ratchet advances, forward secrecy.
2. **Chunked File Transfer**: Offer, interactive accept, Base64 chunk streaming, dynamic progress tracking, and byte-for-byte SHA-256 verification.
3. **Anti-Spam Token Bucket**: Burst traffic detection, exhaustion warnings, and automated 15-second mute enforcement.
4. **Offline Inbox**: Message queueing when recipient is disconnected, and automatic delivery replay upon reconnect.
5. **PBKDF2 Authentication**: Challenge-response handshake, dynamic password update (`/setadminpwd`), old password rejection, and new hash verification.
6. **Persistent User Accounts**: Unique alphanumeric handles, PBKDF2 salted hash creation, guest impersonation defense, and `/clear` terminal reset.

```text
============================================================
   KUKUPU CHAT - FULL END-TO-END AUTOMATED TEST SUITE
============================================================
[TEST 1] Testing E2EE Double Ratchet Over Sockets...              PASSED [OK]
[TEST 2] Testing Chunked File Transfer & SHA-256 Integrity...     PASSED [OK]
[TEST 3] Testing Rate Limiting & Anti-Spam Defense...             PASSED [OK]
[TEST 4] Testing Offline Messaging Inbox...                       PASSED [OK]
[TEST 5] Testing Admin Authentication via PBKDF2...               PASSED [OK]
============================================================
   ALL 5 END-TO-END TESTS COMPLETED AND PASSED (100%)!
============================================================
```

---

## Developer & License

Developed with passion by **[NEXUS8222](https://github.com/nexus8222)**  
*Systems Developer, Network Engineer & Cybersecurity Enthusiast.*

This project is licensed under the **MIT License**.
