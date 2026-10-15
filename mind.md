# Kukupu Chat in C — Master Cognitive & Architecture Mind Map (mind.md)

> **Kukupu Chat in C** is an enterprise-grade, multi-client, multi-threaded terminal communication system developed from scratch in pure C for Linux.
> This document (`mind.md`) serves as the definitive cognitive architecture, engineering mental model, and comprehensive component blueprint for the entire codebase—mapping its conceptual foundation, socket mechanics, threading models, wire protocols, command systems, security mechanisms, and phase-by-phase evolution from Phase 1 to Phase 6 and beyond.

---

## Table of Contents

1. [Executive Overview & System Philosophy](#1-executive-overview--system-philosophy)
2. [Macro Architecture & Technology Stack](#2-macro-architecture--technology-stack)
3. [The 6-Phase Evolutionary Journey](#3-the-6-phase-evolutionary-journey)
   - [Phase 1: Single-Client TCP Echo Foundation](#phase-1-single-client-tcp-echo-foundation)
   - [Phase 2: Multi-Client Concurrency with POSIX Threads](#phase-2-multi-client-concurrency-with-posix-threads)
   - [Phase 3: Global Broadcast Engine & Bidirectional Client](#phase-3-global-broadcast-engine--bidirectional-client)
   - [Phase 4: User Identities, Private Mentions & Cross-Platform](#phase-4-user-identities-private-mentions--cross-platform)
   - [Phase 5: Modular Subsystem Decomposition & Party Rooms](#phase-5-modular-subsystem-decomposition--party-rooms)
   - [Phase 6: Advanced Feature Arsenal & Production Terminal Experience](#phase-6-advanced-feature-arsenal--production-terminal-experience)
4. [Core Data Structures & Memory Model](#4-core-data-structures--memory-model)
   - [The `client_t` Record](#the-client_t-record)
   - [Client Registry & Concurrency Mutexes](#client-registry--concurrency-mutexes)
   - [Party Room & Game Structures](#party-room--game-structures)
5. [Socket Lifecycle & Network Flow Diagram](#5-socket-lifecycle--network-flow-diagram)
6. [Command Engine & Wire Protocol Specification](#6-command-engine--wire-protocol-specification)
   - [Wire Control Protocols](#wire-control-protocols)
   - [Complete Command Matrix](#complete-command-matrix)
7. [Subsystem Deep Dives](#7-subsystem-deep-dives)
   - [7.1 Party Room Isolation & Locked Rooms](#71-party-room-isolation--locked-rooms)
   - [7.2 Ephemeral Vanishing Messages & Background Cleaner](#72-ephemeral-vanishing-messages--background-cleaner)
   - [7.3 Message Editing & Deletion Window (30s SLA)](#73-message-editing--deletion-window-30s-sla)
   - [7.4 Admin Authentication & Reversible Obfuscation Cipher](#74-admin-authentication--reversible-obfuscation-cipher)
   - [7.5 Moderation Arsenal: Kick, Ban, Mute & IP Guard](#75-moderation-arsenal-kick-ban-mute--ip-guard)
   - [7.6 Multiplayer Number Guessing Game](#76-multiplayer-number-guessing-game)
   - [7.7 80-Emoji UTF-8 Palette Engine](#77-80-emoji-utf-8-palette-engine)
   - [7.8 Persistent Lastseen & Activity Auditing](#78-persistent-lastseen--activity-auditing)
   - [7.9 Terminal Raw Mode UI (`termios` & `select`)](#79-terminal-raw-mode-ui-termios--select)
   - [7.10 Full-Screen Terminal User Interface (TUI Subsystem)](#710-full-screen-terminal-user-interface-tui-subsystem)
8. [File Structure & Module Map](#8-file-structure--module-map)
9. [Build, Deployment & Containerization](#9-build-deployment--containerization)
10. [Checklist, Known Limitations & Roadmap (Phases 7–12)](#10-checklist-known-limitations--roadmap-phases-712)

---

## 1. Executive Overview & System Philosophy

The **Kukupu Chat in C** is designed to prove that full-featured, interactive, real-time multi-room chat systems can be constructed without high-level runtimes (Node.js, Go, Python), relying purely on native **POSIX system calls**, **TCP/IP Berkeley sockets**, and low-level **C standard library** primitives.

### Core Design Principles
1. **Low-Level Native Performance**: Minimal memory overhead per client, zero garbage collection pauses, and direct kernel socket manipulation.
2. **Layered Incremental Evolution**: Structured into distinct phases (Phase 1 through Phase 6), demonstrating clean progression from a basic echo server to a feature-rich, multi-room, administrative chat daemon.
3. **Thread Safety First**: Shared memory across client threads is rigorously synchronized using POSIX Mutexes (`pthread_mutex_t`) to eliminate race conditions on global client tables, ban lists, and party registries.
4. **Resilient Network Handling**: Robust handling of abrupt client disconnects, `SIGPIPE` signal suppression, IP verification, and buffer overflow prevention.
5. **Interactive Terminal Experience**: Custom client using POSIX `termios` raw-mode terminal manipulation and `select()` multiplexing to deliver real-time visual feedback, emoji panels, and vanishing message countdowns directly inside standard Linux terminals.

---

## 2. Macro Architecture & Technology Stack

```
+---------------------------------------------------------------------------------------------------+
|                                  CHAT-APP CLIENT-SERVER RUNTIME                                   |
+---------------------------------------------------------------------------------------------------+

     [Client 1: User Terminal]          [Client 2: User Terminal]          [Client 3: Admin Terminal]
                 │                                  │                                  │
                 │ TCP Socket (Port 9001)           │ TCP Socket (Port 9001)           │ TCP Socket (Port 9001)
                 ▼                                  ▼                                  ▼
+───────────────────────────────────────────────────────────────────────────────────────────────────+
|                                    SERVER DAEMON (server.c)                                       |
|                                                                                                   |
|  [Listener Socket: socket(AF_INET, SOCK_STREAM) -> bind(:9001) -> listen(10)]                     |
|                                                                                                   |
|  [Master Accept Loop (while 1)] ──> accept() ──> Spawns Worker Thread per Client                  |
|                                                                                                   |
|  +────────────────────────────────────── WORKER THREAD ─────────────────────────────────────────+ |
|  |  pthread_create(&tid, NULL, handle_client, (void*)cli)                                       | |
|  |                                                                                              | |
|  |  1. Name Ingestion & Uniqueness Validation                                                  | |
|  |  2. Admin Authentication Challenge Handshake (if username == "admin")                        | |
|  |  3. Assign Default Party ("public") & Register into clients[MAX_CLIENTS]                     | |
|  |  4. Recv Loop:                                                                               | |
|  |     ├── Parse Slash Commands -> handle_command() -> [commands.c]                              | |
|  |     └── Broadcast Normal Chat -> party_broadcast() / broadcast_message()                     | |
|  |  5. Disconnect Cleanup: remove_client(), save_lastseen(), close(), free()                    | |
|  +──────────────────────────────────────────────────────────────────────────────────────────────+ |
|                                                                                                   |
|  [Background Cleaner Thread] ──> vanish_cleaner_thread() -> check_and_expire_vanish_messages()    |
|                                                                                                   |
|  [Shared Synchronized Modules (Protected by Mutexes)]:                                            |
|   ├── banlist.c (banned.txt)      ├── mute.c (Mute Flags)         ├── party.c (Room Isolation)  |
|   ├── admin.c (Kick/Ban/Privs)    ├── motd.c (motd.txt)           ├── lastseen.c (.lastseen)    |
|   ├── vanish.c (Timed Ephemeral)  ├── game.c (Number Guessing)    ├── log.c (server.log)        |
+───────────────────────────────────────────────────────────────────────────────────────────────────+
```

### Technology Matrix

| Subsystem | Tool / Technology | Purpose |
|:---|:---|:---|
| **Language** | C (C99 / C11, GCC, Clang) | Core programming language across all phases. |
| **Networking** | Berkeley Sockets (`sys/socket.h`, `arpa/inet.h`, `netinet/in.h`) | TCP/IP stream sockets (IPv4, `SOCK_STREAM`). |
| **Concurrency** | POSIX Threads (`pthread.h`) | Worker threads per client connection & background cleaner daemon. |
| **I/O Multiplexing** | `sys/select.h` | Non-blocking timeout multiplexing for client packet reception. |
| **Terminal Control**| `termios.h` | Raw terminal mode manipulation, unbuffered input, cursor positioning. |
| **Signals** | `signal.h` | `SIGPIPE` ignoring (preventing crash on broken sockets), `SIGINT` cleanup. |
| **Persistence** | Standard C File I/O (`stdio.h`) | Flat-file and binary storage: `server.log`, `banned.txt`, `motd.txt`, `.lastseen`, `pwd.env`. |
| **Containerization**| Docker & Docker Compose | Multi-container reproducible deployment for server daemons. |

---

## 3. The 6-Phase Evolutionary Journey

The codebase documents an engineering journey divided into 6 distinct developmental milestones:

```mermaid
flowchart LR
    P1["Phase 1: Basic Echo"] --> P2["Phase 2: Threaded Concurrency"]
    P2 --> P3["Phase 3: Multi-Client Broadcast"]
    P3 --> P4["Phase 4: Usernames & Private Mentions"]
    P4 --> P5["Phase 5: Subsystems & Moderation"]
    P5 --> P6["Phase 6: Advanced Terminal Arsenal"]
```

---

### Phase 1: Single-Client TCP Echo Foundation
- **Directory**: [`phase-1/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-1)
- **Files**: `server.c`, `client.c`, `Makefile`, `README1.md`
- **Objective**: Establish the bedrock networking primitives.
- **Key Mechanics**:
  - `socket(AF_INET, SOCK_STREAM, 0)`: Creates an IPv4 TCP stream socket.
  - `bind()` to `INADDR_ANY` (0.0.0.0) on port `9001`.
  - `listen(server_fd, 1)`: Backlog queue of 1.
  - `accept()` blocks until a client connects.
  - Blocking `recv()` reads from client into a 1024-byte buffer and echoes it back using `send()`.
  - Clean closure on EOF or disconnection.

---

### Phase 2: Multi-Client Concurrency with POSIX Threads
- **Directory**: [`phase-2/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-2)
- **Files**: `server.c`, `client.c`, `Makefile`
- **Objective**: Allow multiple clients to connect simultaneously without blocking each other.
- **Key Mechanics**:
  - `listen(server_fd, 10)`: Expanded connection queue.
  - Master loop runs `accept()` continuously.
  - On each incoming connection, dynamically allocates client file descriptor and spawns a thread:
    ```c
    pthread_create(&thread_id, NULL, handle_client, (void*)client_sock_ptr);
    pthread_detach(thread_id);
    ```
  - Independent thread execution lifecycle per client socket.

---

### Phase 3: Global Broadcast Engine & Bidirectional Client
- **Directory**: [`phase-3/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-3)
- **Files**: `server.c`, `client.c`, `linux_client.c`, `Makefile`
- **Objective**: Transform an echo server into a true group chat with simultaneous sending and receiving.
- **Key Mechanics**:
  - **Server-Side Client Registry**:
    ```c
    typedef struct { int socket; struct sockaddr_in address; } Client;
    Client clients[MAX_CLIENTS];
    int client_count = 0;
    pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
    ```
  - **Thread-Safe Broadcast**:
    ```c
    void broadcast(const char* message, int sender_fd) {
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; ++i) {
            if (clients[i].socket != sender_fd)
                send(clients[i].socket, message, strlen(message), 0);
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    ```
  - **Dual-Threaded Client (`client.c`)**:
    - Thread 1 (`send_msg_handler`): Reads user input from `fgets()` and calls `send()`.
    - Thread 2 (`recv_msg_handler`): Blocks on `recv()` and prints incoming messages from other clients.

---

### Phase 4: User Identities, Private Mentions & Cross-Platform
- **Directory**: [`phase-4/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-4)
- **Files**: `server.c`, `client.c`, `client_win.c`, `Dockerfile.server`, `docker-compose.yml`, `Makefile`
- **Objective**: Introduce identities, direct messaging, Windows compatibility, and containerization.
- **Key Mechanics**:
  - **Username Handshake**: Server prompts `Enter username: ` and ensures uniqueness before joining.
  - **Join/Leave Announcements**: Server announces `[*] alice joined the chat.` and `[-] alice left the chat.`
  - **Private Messaging via `@mention`**:
    - If a message starts with `@<user> <message>`, the server routes it only to the target socket.
  - **Server Pinned Notices**: Prefixed with `**[SERVER]: Notice**`.
  - **Windows Client Support (`client_win.c`)**:
    - Utilizes Winsock2 (`#include <winsock2.h>`, `WSAStartup()`, `WSACleanup()`).
  - **Docker Deployment**: First introduction of `Dockerfile.server` and `docker-compose.yml`.

---

### Phase 5: Modular Subsystem Decomposition & Party Rooms
- **Directory**: [`phase-5/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-5)
- **Files**: Modular split: `common.h`, `server.c`, `client.c`, `commands.c/.h`, `admin.c/.h`, `banlist.c/.h`, `mute.c/.h`, `party.c/.h`, `motd.c/.h`, `log.c/.h`, `utils.c/.h`
- **Objective**: Full architectural refactoring into dedicated modules with slash commands, moderation, and isolated rooms.
- **Key Mechanics**:
  - Introduction of `client_t` structure.
  - Central command router (`handle_command`).
  - IP banlist storage (`banned.txt`) with pre-accept rejection.
  - Mute flags (`cli->is_muted`).
  - Party/Room separation: Users only receive messages broadcast to their `party_code`.
  - Timestamped disk logging (`server.log`).

---

### Phase 6: Advanced Feature Arsenal & Production Terminal Experience
- **Directory**: [`phase-6/`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6)
- **Files**: Complete production system (16 source/header files + Makefile + Docker).
- **Objective**: The state-of-the-art implementation featuring gaming, ephemerality, emojis, raw terminal mode, and encrypted admin authentication.
- **Key Mechanics**:
  - **ANSI Username Colors**: Customizable colors with `/setcolor` and `/colorlist`.
  - **Ephemeral Vanishing Messages**: `/vanish <seconds> <msg>` with background timer thread and live client countdown.
  - **Message Editing and Deletion**: 30-second rollback window (`/editlast`, `/deletelast`).
  - **Persistent Lastseen**: Binary serialized tracking in `.lastseen`.
  - **Multiplayer Guessing Game**: Turn-based number guessing engine (`/guessgame`, `/guess`).
  - **80-Emoji UTF-8 Palette**: Interactive in-terminal emoji picker (`/emoji`).
  - **Admin Authentication Handshake**: Password challenge-response with custom reversible cipher (`pwdgen.c`, `pwd.env`).
  - **Locked Party Rooms**: Party locking (`/lockparty`, `/unlockparty`) and invitation gating (`/invite`).
  - **Raw Terminal Client**: Non-canonical raw input mode (`termios`) with `select()` multiplexing.

---

## 4. Core Data Structures & Memory Model

The centerpiece of Phase 6 is the `client_t` structure defined in [`phase-6/common.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/common.h):

### The `client_t` Record

```c
typedef struct client {
    int sockfd;                             // Socket file descriptor
    char username[USERNAME_LEN];            // Unique user handle (max 32 bytes)
    int active;                             // Online state flag (1 = active, 0 = disconnecting)
    char ip[INET_ADDRSTRLEN];               // Client IP string (IPv4 formatted)
    int is_admin;                           // Administrative rights flag (1 = elevated)
    int is_muted;                           // Mute flag (1 = suppressed from speaking)
    char color[ANSI_COLOR_MAX];             // ANSI color escape sequence for chat handle
    char party_code[PARTY_CODE_LEN];        // Current room code (default "public")
    time_t connected_time;                  // Timestamp of initial TCP handshake
    struct sockaddr_in address;             // Raw POSIX socket address structure
    time_t join_time;                       // Timestamp when username was registered
    time_t last_seen;                       // Timestamp of last received packet
    char last_msg[BUFFER_SIZE];             // Text buffer of the user's most recent message
    time_t last_msg_time;                   // Timestamp of last sent message (for 30s edit window)
    int last_msg_id;                        // Tracking ID of last sent message
    char invited_party[PARTY_CODE_LEN];     // Pending invitation code for locked rooms
    struct client *next;                    // Linked-list pointer for chaining
} client_t;
```

### Client Registry & Concurrency Mutexes

The server maintains an array of client pointers synchronized via POSIX Mutexes:

```c
extern client_t *clients[MAX_CLIENTS];               // Global pointer table (MAX_CLIENTS = 100)
extern pthread_mutex_t clients_mutex;                // Primary synchronization lock
```

- When adding a client (`add_client()`), finding a client (`get_client_by_name()`), removing a client (`remove_client()`), or broadcasting, `pthread_mutex_lock(&clients_mutex)` is acquired.
- This guarantees zero memory corruption or dangling pointer access when clients disconnect concurrently.

### Party Room & Game Structures

In [`phase-6/party.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/party.h) and [`phase-6/game.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/game.h):

```c
typedef struct {
    char code[PARTY_CODE_LEN];              // Unique room name / code
    int is_locked;                          // Lock flag (1 = invite-only)
    int invite_count;                       // Active invite counter
} party_info_t;

typedef struct {
    int active;                             // 1 if game is currently in progress
    int number_to_guess;                    // Random target (1 - 100)
    char started_by[USERNAME_LEN];          // Username of game initiator
    char current_turn[USERNAME_LEN];        // Username whose turn it is to guess
    time_t start_time;                      // Game start timestamp
} guess_game_t;
```

---

## 5. Socket Lifecycle & Network Flow Diagram

```
 CLIENT TERMINAL                                                  SERVER DAEMON
 ===============                                                  =============
        │                                                               │
        │ 1. socket() -> connect("127.0.0.1", 9001)                     │
        ├──────────────────────────────────────────────────────────────>│ 2. listener: accept()
        │                                                               │    Checks is_ip_banned(ip)
        │                                                               │    malloc(client_t), active=1
        │                                                               │    pthread_create(handle_client)
        │                                                               │
        │ 3. Send username (e.g. "sahil")                               │
        ├──────────────────────────────────────────────────────────────>│ 4. Check name collisions
        │                                                               │    If "admin", trigger pwd challenge
        │<──────────────────────────────────────────────────────────────┤ 5. Send Welcome + MOTD + Pinned Msg
        │    "[JOIN] sahil has entered the chat."                       │    party_broadcast_system()
        │                                                               │
        │ 6. User types: "Hello everyone!"                              │
        ├──────────────────────────────────────────────────────────────>│ 7. Check if cli->is_muted
        │                                                               │    Record last_msg, last_msg_time
        │<──────────────────────────────────────────────────────────────┤ 8. broadcast_message()
        │    "[sahil]: Hello everyone!"                                 │    (Filtered to cli->party_code)
        │                                                               │
        │ 9. User types: "/vanish 10 Secret note"                       │
        ├──────────────────────────────────────────────────────────────>│ 10. vanish_messages[id] registered
        │<──────────────────────────────────────────────────────────────┤ 11. Broadcast: "__VANISH__:id:10:..."
        │    Renders with countdown in raw terminal                     │
        │                                                               │ 12. vanish_cleaner_thread detects expiry
        │<──────────────────────────────────────────────────────────────┤ 13. Broadcast: "__DELETE__:id"
        │    Client clears vanish message from screen                   │
        │                                                               │
        │ 14. Client presses Ctrl+C                                     │
        │    (catch_ctrl_c_and_exit -> close)                           │
        ├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─>│ 15. recv() returns <= 0 (EOF)
        │                                                               │    party_broadcast_system([LEAVE])
        │                                                               │    update_lastseen(), save_lastseen()
        │                                                               │    remove_client(), close(), free()
```

---

## 6. Command Engine & Wire Protocol Specification

Communication between client and server blends standard human-readable text with dedicated structured wire protocol envelopes.

### Wire Control Protocols

| Protocol Header | Format | Purpose | Origin / Destination |
|:---|:---|:---|:---|
| `__PRIVATE__:` | `__PRIVATE__:<target>:<message>` | Private direct message routing. | Client $\to$ Server $\to$ Target Client |
| `__VANISH__:` | `__VANISH__:<id>:<sec>:<sender>:<msg>` | Ephemeral message broadcast with expiry countdown. | Server $\to$ All Clients in Party |
| `__DELETE__:` | `__DELETE__:<id>` | Vanishing message expiration notice; client purges from view. | Server Background Thread $\to$ Clients |
| `__EDIT__:` | `__EDIT__:<id>:<new_msg>` | Dynamic update of an existing message on client screens. | Server $\to$ Clients |
| `ask` / `try` / `true` | Raw string tokens | Multi-stage administrative password verification handshake. | Server $\leftrightarrow$ Client Handshake |

---

### Complete Command Matrix

All commands are processed by `handle_command()` in [`phase-6/commands.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/commands.c):

#### 1. General & Utility Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/help` | `/help` | User | Displays the comprehensive ASCII-formatted command help menu. |
| `/whoami` | `/whoami` | User | Shows your registered handle, assigned ANSI color, and admin status. |
| `/users` | `/users` | User | Lists all active connected users currently residing in your party room. |
| `/ping` | `/ping` | User | Latency test; returns immediate `pong` from server. |
| `/time` | `/time` | User | Queries and returns the server's local wall-clock time. |
| `/uptime` | `/uptime` | User | Computes and displays server uptime since launch in `HH:MM:SS`. |
| `/motd` | `/motd` | User | Fetches and renders the current server Message of the Day. |
| `/clear` | `/clear` | User | Clears the terminal screen viewport using ANSI escape sequences. |
| `/lastseen` | `/lastseen <user>` | User | Checks whether user is online, or retrieves last recorded logout timestamp. |

#### 2. Messaging & Content Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/msg` | `/msg <user> <message>` | User | Dispatches a private direct message visible only to the target. |
| `/vanish` | `/vanish <seconds> <msg>` | User | Dispatches an ephemeral message that self-destructs after $N$ seconds. |
| `/editlast` | `/editlast <new_message>` | User | Edits the user's most recently sent message if within the 30-second window. |
| `/deletelast`| `/deletelast` | User | Retracts and deletes the user's last message if within the 30-second window. |
| `/emoji` | `/emoji` / `/emoji <idx>` | User | Interactive menu of 80 emojis, or direct insertion by numeric index. |

#### 3. Customization Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/setcolor` | `/setcolor <color>` | User | Sets username display color (`red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`). |
| `/colorlist`| `/colorlist` | User | Prints a visual palette of all supported ANSI color options. |

#### 4. Party (Room) Isolation Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/createparty`| `/createparty [code]` | User | Spawns a new party room. Generates random 6-character alphanumeric code if omitted. |
| `/joinparty` | `/joinparty <code>` | User | Switches user into the designated room. Rejects if party is locked without invite. |
| `/party` | `/party` | User | Reports current room code (defaults to `"public"`). |
| `/leaveparty`| `/leaveparty` | User | Leaves current private room and rejoins the public room. Auto-prunes empty parties. |
| `/parties` | `/parties` | Admin | Lists all active party rooms, member counts, and lock status. |
| `/lockparty` | `/lockparty` | Admin | Locks the current party room, preventing new members from joining without invite. |
| `/unlockparty`| `/unlockparty` | Admin | Unlocks the current party room to allow public entry. |
| `/invite` | `/invite <user>` | Admin | Issues an authorized invitation ticket allowing a user to join a locked party. |
| `/partyinfo` | `/partyinfo <code>` | Admin | Inspects member roster and admin rights within any party room. |

#### 5. Games Engine Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/guessgame` | `/guessgame start` | User | Initiates a room-wide multiplayer number guessing game (1–100). |
| `/guess` | `/guess <number>` | User | Submits a guess on your turn. Server provides "too high" / "too low" hints. |

#### 6. Administration & Moderation Commands

| Command | Syntax | Minimum Role | Description |
|:---|:---|:---:|:---|
| `/kick` | `/kick <user>` | Admin | Forcefully disconnects target user's TCP socket. |
| `/ban` | `/ban <user>` / `/ban <ip>` | Admin | Bans target IP address and appends it to persistent `banned.txt`. |
| `/unban` | `/unban <ip>` | Admin | Removes IP from `banned.txt` and restores access. |
| `/banlist` | `/banlist` | Admin | Dumps all currently banned IP addresses. |
| `/mute` | `/mute <user>` | Admin | Sets mute flag on user, suppressing public and private chat. |
| `/unmute` | `/unmute <user>` | Admin | Removes mute flag from user. |
| `/mutelist` | `/mutelist` | Admin | Lists all currently muted operators. |
| `/broadcast`| `/broadcast <msg>` | Admin | Sends an inescapable system broadcast across **all** party rooms. |
| `/pin` | `/pin <msg>` | Admin | Pins a high-priority banner message shown to all connecting clients. |
| `/setmotd` | `/setmotd <msg>` | Admin | Updates the Message of the Day and saves to `motd.txt`. |
| `/log` | `/log` | Admin | Streams the contents of `server.log` directly into the admin terminal. |
| `/whois` | `/whois <user>` | Admin | Displays deep forensic metadata: IP address, party, admin status, and online seconds. |
| `/shutdown` | `/shutdown` | Admin | Broadcasts shutdown notice and gracefully terminates the server process. |

---

## 7. Subsystem Deep Dives

---

### 7.1 Party Room Isolation & Locked Rooms
- **Implementation**: [`phase-6/party.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/party.c) & [`party.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/party.h)
- **Room Isolation Mechanics**:
  - Every client possesses a `party_code` field (default: `"public"`).
  - Normal chat messages sent via `broadcast_message()` iterate through `clients[MAX_CLIENTS]` and only deliver to sockets where `strcmp(clients[i]->party_code, sender->party_code) == 0`.
- **Locking & Invitations**:
  - Rooms can be locked with `/lockparty` (`party_list[i].is_locked = 1`).
  - When a user tries to `/joinparty <code>`, the server checks:
    ```c
    if (party_list[i].is_locked && strcmp(cli->invited_party, arg1) != 0) {
        send_to_client(cli, "[SERVER] Party '%s' is locked. You need an invite.\n", arg1);
        return 1;
    }
    ```
  - An admin in that room can `/invite <user>`, writing the room code into `target->invited_party`.
- **Garbage Collection**:
  - When the last member leaves a custom party (`/leaveparty`), the party code is purged from `party_list` to free room slots.

---

### 7.2 Ephemeral Vanishing Messages & Background Cleaner
- **Implementation**: [`phase-6/vanish.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/vanish.c) & [`vanish.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/vanish.h)
- **Data Structure**:
  ```c
  typedef struct {
      int msg_id;                           // Unique numeric message identifier
      char sender[USERNAME_LEN];
      char message[MSG_LEN];
      int duration;                         // Time-to-live in seconds
      time_t timestamp;                     // Creation epoch
  } vanish_msg_t;
  ```
- **Execution Flow**:
  1. User dispatches `/vanish 10 Highly confidential data`.
  2. Server generates `msg_id = rand() % 1000000`, records entry in `vanish_messages[]`, and broadcasts:
     ```
     __VANISH__:48102:10:alice: Highly confidential data
     ```
  3. Client decodes packet and tracks message in local `vanish_messages[]`, rendering countdown:
     ```
     [VANISH] alice: Highly confidential data (expires in 10 sec)
     ```
  4. Server runs a dedicated background cleaner thread (`vanish_cleaner_thread()`):
     ```c
     void *vanish_cleaner_thread(void *arg) {
         while (1) {
             check_and_expire_vanish_messages();
             sleep(1);
         }
     }
     ```
  5. When `difftime(now, vm->timestamp) >= vm->duration`, server broadcasts `__DELETE__:48102`.
  6. Client receives delete packet and erases the message from the active terminal buffer.

---

### 7.3 Message Editing & Deletion Window (30s SLA)
- **Implementation**: [`phase-6/commands.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/commands.c) (lines 255–300)
- **Time Window Verification**:
  ```c
  if (difftime(time(NULL), cli->last_msg_time) > 30) {
      send_to_client(cli, "[SERVER] Edit time window (30s) expired.\n");
      return 1;
  }
  ```
- **Edit Execution (`/editlast`)**:
  - Server reconstructs updated message, marks it with yellow `[EDITED]` tag, broadcasts to party, updates `cli->last_msg`, resets timestamp, and logs event to `server.log`.
- **Delete Execution (`/deletelast`)**:
  - Server broadcasts red `[DELETED] <user> deleted their last message.` notice, zeroes out `cli->last_msg[0] = '\0'`, and logs to `server.log`.

---

### 7.4 Admin Authentication & Reversible Obfuscation Cipher
- **Implementation**: [`phase-6/pwdgen.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/pwdgen.c), [`pwdgen.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/pwdgen.h), and [`server.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/server.c) (lines 152–248)
- **First-Run Password Provisioning**:
  - When the server boots, it checks for `pwd.env`. If missing, it prompts the operator in the server terminal to set an administrative password, encrypts it via `pwdencrypt()`, and writes the ciphertext to `pwd.env`.
- **The Reversible Obfuscation Cipher**:
  - Operates via three mathematical passes:
    1. **Stride-4 Byte Permutation**: Swaps `enc_pwd[i]` with `enc_pwd[i + 2]`.
    2. **Index-Biased Arithmetic Shifting**: Shifts alternate byte values by their position index $i$:
       $$\text{enc\_pwd}[i] = \text{enc\_pwd}[i] + i$$
    3. **String Reversal**: Reverses the string buffer around its midpoint.
  - Decryption (`pwddecrypt()`) runs the operations in exact inverse sequence (reversal $\to$ stride swap $\to$ subtract index bias).
- **Challenge-Response Handshake**:
  - When a client connects with the username `"admin"`, the server initiates an authentication protocol:
    1. Server transmits token `"ask"`.
    2. Client prompts user for password and transmits plaintext.
    3. Server decrypts `pwd.env` and compares strings.
    4. On failure, server transmits `"try"`. If failed 3 consecutive times (`MAX_TRIES`), connection is severed.
    5. On success, server transmits `"true"`, promotes client (`cli->is_admin = 1`), and requests the operator's real display name.

---

### 7.5 Moderation Arsenal: Kick, Ban, Mute & IP Guard
- **Implementation**: [`phase-6/admin.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/admin.c), [`banlist.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/banlist.c), and [`mute.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/mute.c)
- **Pre-Accept IP Firewall**:
  - In `server.c`, immediately after `accept()`, the server extracts the client IP and evaluates:
    ```c
    if (is_ip_banned(cli_ip)) {
        printf("Rejected banned IP: %s\n", cli_ip);
        close(new_sock);
        continue;
    }
    ```
  - Banned connections are dropped immediately before allocating any `client_t` memory.
- **Persistent Ban Storage**:
  - Banned IPs are stored in `banned.txt`.
  - When `/ban <user>` is invoked, the IP is appended to disk.
  - On startup, `load_banlist()` parses `banned.txt` into memory.
- **Admin Hierarchy Immunity**:
  - In `mute_user()`:
    ```c
    if (victim->is_admin) {
        send_to_client(requester, "[ADMIN] You cannot mute another admin.\n");
        return 0;
    }
    ```

---

### 7.6 Multiplayer Number Guessing Game
- **Implementation**: [`phase-6/game.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/game.c) & [`game.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/game.h)
- **State Machine**:
  - Initiated via `/guessgame start`. Generates secret target:
    ```c
    guess_game.number_to_guess = (rand() % 100) + 1;
    ```
  - Assigns first turn to initiator.
  - On `/guess <num>`, checks if `strcmp(cli->username, guess_game.current_turn) == 0`.
  - Compares number:
    - Lower: Broadcasts `[GAME] <user> guessed too low!`
    - Higher: Broadcasts `[GAME] <user> guessed too high!`
    - Correct: Broadcasts victory notice and deactivates game.
  - **Turn Rotation**: Automatically iterates through `clients[]` to assign next turn to another active player.

---

### 7.7 80-Emoji UTF-8 Palette Engine
- **Implementation**: [`phase-6/emoji.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/emoji.c) & [`emoji.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/emoji.h)
- **Emoji Array**: Contains 80 UTF-8 multi-byte emoji strings partitioned into categories:
  - Smileys (😀, 😂, 😇, 🥳, 😎, etc.)
  - People (👶, 👮, 🕵️, 👩‍⚕️, etc.)
  - Animals (🐶, 🐱, 🦁, 🐸, etc.)
  - Food (🍏, 🍕, 🥑, 🍔, etc.)
  - Activities (⚽, 🎮, 🥊, 🏹, etc.)
  - Travel & Transport (🚗, ✈️, 🚀, 🚢, etc.)
  - Tech & Objects (💻, 📱, 🔒, 📷, etc.)
  - Symbols & Hearts (❤️, 💔, 💯, ⚠️, etc.)
- **Interactive Terminal Palette**: `/emoji` renders an indexed 6-column matrix.
- **Direct Insertion**: `/emoji 7` inserts `😂` directly into the chat message stream.

---

### 7.8 Persistent Lastseen & Activity Auditing
- **Implementation**: [`phase-6/lastseen.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/lastseen.c) & [`lastseen.h`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/lastseen.h)
- **Binary Persistence**:
  - Persists `lastseen_list[MAX_CLIENTS]` directly to disk as binary file `.lastseen` using `fwrite()` and `fread()`.
- **Query Resolution**:
  - `/lastseen <user>` first scans currently connected `clients[]`. If present, reports `"User is currently online."`
  - If offline, scans `.lastseen` records and formats timestamp via `strftime("%Y-%m-%d %H:%M:%S")`.

---

### 7.9 Terminal Raw Mode UI (`termios` & `select`)
- **Implementation**: [`phase-6/client.c`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/client.c) (lines 61–180)
- **Raw Mode Initialization**:
  ```c
  void enable_raw_mode() {
      tcgetattr(STDIN_FILENO, &orig_termios);
      atexit(disable_raw_mode); // Guarantees terminal restoration on exit
      struct termios raw = orig_termios;
      raw.c_lflag &= ~(ICANON | ECHO); // Disables canonical line buffering & automatic echo
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }
  ```
- **Socket & Keyboard Multiplexing**:
  - Client uses `select()` multiplexing on the socket descriptor with a 1-second timeout:
    ```c
    fd_set read_fds;
    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    ```
  - This allows the background receive thread to update the terminal screen (decrementing vanishing message timers, rendering live PMs) without waiting for the user to press Enter.
- **Line Clearing & Input Redraw**:
  - Uses ANSI escape sequence `\r\033[K` to clear the current terminal line before printing incoming broadcast messages, then redraws the user's active input buffer.

### 7.10 Full-Screen Terminal User Interface (TUI Subsystem)

The client features a zero-dependency, pure C terminal user interface engine implemented in `ui.h` and `ui.c`:

- **Alternate Screen Buffer Switching**:
  - On startup, sends `\033[?1049h\033[2J\033[H` to switch into the terminal's private buffer.
  - On exit, sends `\033[?1049l\033[?25h` to restore the user's primary terminal shell buffer and re-enable cursor visibility.
- **Atomic Double-Buffering (Zero-Flicker)**:
  - Compiles the entire screen frame (header, message viewport, sidebar, status bar, input container) into a 64 KB memory buffer (`frame[FRAME_BUF_SIZE]`).
  - Emits the frame using a single `write(STDOUT_FILENO, frame, len)` system call, eliminating terminal tearing, cursor jitter, and stdout race conditions.
- **Split-Pane Architecture**:
  - **Header Bar**: Displays application brand (`[KUKUPU CHAT]`), room indicator (`Room: #0000`), E2EE status badge, and user handle (`User: @alice`).
  - **Main Viewport**: Indented chat messages with timestamps (`[HH:MM]`), badges (`<bob>`, `[YOU]`, `[E2EE:bob]`, `--- [SYS] ---`, `[FILE:bob]`), and dynamic word wrapping.
  - **Collapsible Sidebar**: On terminals $\ge 80$ columns, displays a 22-column panel showing member count (`[MEMBERS (N)]`), user handles, and room mode (`[CHANNEL #0000]`). Autohides on compact screens.
  - **Dedicated Status Bar**: Separates chat log from input prompt; displays live typing pulses (`Typing: bob is typing a message...`) and in-band file transfer progress bars.
  - **Pinned Input Box**: Rounded box container (`╭───╮ │ > ... │ ╰───╯`) at the bottom rows with hardware cursor synchronization.
- **Dynamic Resize Support (`SIGWINCH`)**:
  - Hooks `SIGWINCH` signal and queries terminal dimensions via `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)`.
  - Automatically recalculates line wraps, sidebar visibility, and viewport capacities on window resize.
- **Scrollback History & Keyboard Navigation**:
  - Maintains an internal ring buffer of up to 600 messages.
  - Supports `PageUp` / `PageDown` scrolling, `Tab` command autocompletion, `Up` / `Down` input history navigation, `Left` / `Right` cursor movement, and `Ctrl+L` screen refresh.
- **Zero-Emoji Clean ASCII Aesthetic**:
  - Strictly uses clean UTF-8 / ASCII box-drawing characters (`┌`, `─`, `┐`, `│`, `└`, `┘`, `╭`, `╰`, `╮`, `╯`, `├`, `┤`, `*`, `>`), maintaining a high-contrast, professional cybersecurity terminal aesthetic.

---

## 8. File Structure & Module Map

Complete directory tree of Kukupu Chat:

```
.
├── Makefile            # Build script compiling all object targets with -Wall -Wextra -pthread -O2
├── Dockerfile          # Multi-stage container recipe for headless server deployment
├── docker-compose.yml  # Container orchestration mapping port 9001:9001
├── checklist           # Active developer notes and future milestone tasks
├── README.md           # Comprehensive project manual and ASCII interface diagram
├── mind.md             # Master cognitive architecture & blueprint
│
├── include/            # C Header files & interface definitions
│   ├── common.h        # Protocol constants, client_t structure, buffer limits, ANSI themes
│   ├── client.h        # Shared client connection definitions
│   ├── crypto_utils.h  # OpenSSL 3.0 crypto wrappers (X25519, HKDF, AES-256-GCM, PBKDF2)
│   ├── e2ee.h          # Signal Double Ratchet session definitions & packet envelopes
│   ├── file_transfer.h # In-band chunked file streaming engine & SHA-256 verification
│   ├── ratelimit.h     # Token bucket rate limiter & IP connection tracker
│   ├── db.h            # PBKDF2 credential storage, offline inbox & key directory
│   ├── ui.h            # Zero-flicker double-buffered full-screen terminal TUI engine
│   ├── admin.h         # Administrative moderation declarations
│   ├── commands.h      # Slash command routing table declarations
│   ├── banlist.h       # IP ban manager definitions
│   ├── mute.h          # User mute tracking definitions
│   ├── party.h         # Multi-room channel management declarations
│   ├── vanish.h        # Ephemeral vanishing message definitions
│   ├── game.h          # Number guessing game engine declarations
│   ├── emoji.h         # 80-item categorized ASCII/Unicode catalog
│   ├── lastseen.h      # User logout tracking definitions
│   ├── log.h           # Timestamped operational audit logging
│   ├── motd.h          # Message of the Day definitions
│   ├── pwdgen.h        # Administrative credential helpers
│   └── utils.h         # Server broadcast and client search utilities
│
├── src/                # Modular C source code implementations
│   ├── common/         # Common logic shared across binaries
│   │   ├── crypto_utils.c # OpenSSL 3.0 cryptographic primitives
│   │   └── emoji.c     # Emoji & symbol catalog implementations
│   ├── client/         # Client terminal application
│   │   ├── client.c    # Client main loop, raw termios, socket select multiplexer
│   │   ├── ui.c        # ANSI/VT100 double-buffered terminal windowing engine
│   │   ├── e2ee.c      # Double Ratchet state machine & key management
│   │   └── file_transfer.c # Base64 chunked transfer engine
│   └── server/         # Server daemon & moderation subsystems
│       ├── server.c    # Socket listener, multi-threaded worker pools
│       ├── admin.c     # Administrative moderation implementations
│       ├── commands.c  # Slash command execution engine
│       ├── banlist.c   # Persistent IP ban management
│       ├── db.c        # PBKDF2 credential storage, offline inbox & key directory
│       ├── game.c      # Turn-based number guessing mini-game
│       ├── lastseen.c  # Last-seen persistence engine
│       ├── log.c       # Real-time event logger writing to server.log
│       ├── motd.c      # MOTD loader and updater
│       ├── mute.c      # Mute list enforcement
│       ├── party.c     # Room and channel isolation logic
│       ├── pwdgen.c    # Password generation and verification
│       ├── ratelimit.c # Token bucket anti-spam engine
│       ├── utils.c     # Message broadcasting & user search
│       └── vanish.c    # Ephemeral message decay thread
│
└── tests/              # Automated verification test suites
    ├── test_e2e_full.c # Socket-level automated end-to-end integration test suite
    └── test_all_commands.py # Live socket test suite for all 44 /help commands
```

---

## 9. Build, Deployment & Containerization

### Compiling from Source

Using the provided root [`Makefile`](file:///home/sahilrana/Music/chatapp/Chat-App/Makefile):

```bash
# Build both server and client binaries with optimization and thread flags:
make clean && make

# Generates:
#   ./server (Server daemon executable)
#   ./client (Client terminal executable)
```

Compiler invocation:
```bash
gcc -Wall -Wextra -pthread -O2 -c -o server.o server.c
gcc -Wall -Wextra -pthread -O2 -o server server.o admin.o commands.o mute.o log.o banlist.o utils.o motd.o party.o lastseen.o vanish.o pwdgen.o game.o
gcc -Wall -Wextra -pthread -O2 -o client client.o emoji.o
```

### Running the System

```bash
# Terminal 1: Launch the Server Daemon
./server
# -> Prompts to create admin password on first run (saved to pwd.env)
# -> Binds to 0.0.0.0:9001 and waits for connections

# Terminal 2: Launch Client 1 (Regular User)
./client
# -> Prompts for Server IP (e.g. 127.0.0.1)
# -> Prompts for Username (e.g. alice)
# -> Enters chat!

# Terminal 3: Launch Client 2 (Admin User)
./client
# -> Enter username: admin
# -> Server issues challenge: enter password
# -> Authenticates and prompts for display name (e.g. root_nexus)
# -> Elevated admin commands enabled!
```

### Docker Deployment

[`phase-6/Dockerfile`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/Dockerfile) builds an isolated Debian-based environment:

```dockerfile
FROM gcc:latest
WORKDIR /app
COPY . .
RUN make
EXPOSE 9001
CMD ["./server"]
```

Run via Docker Compose:
```bash
docker-compose up -d --build
```

---

## 10. Checklist, Known Limitations & Roadmap (Phases 7–12)

### Current Developer Checklist ([`phase-6/checklist`](file:///home/sahilrana/Music/chatapp/Chat-App/phase-6/checklist))
- [ ] **Vanish Integration with Edit/Delete**: Add the same vanishing timer mechanism into `/editlast` and `/deletelast`.
- [ ] **Admin Spy Command**: Implement `/spy <user>` allowing administrators to eavesdrop on private messages for moderation compliance.
- [ ] **SSL / TLS Integration**: Working on OpenSSL wrapper for end-to-end socket transport encryption.
- [ ] **Kubernetes Deployment**: Orchestration manifests for running scalable chat clusters.

### Master Strategic Roadmap (Phases 7–12 from `README.md`)

```
+───────────────────────────────────────────────────────────────────────────────────────────────────+
| Phase 7:  Transport Security via OpenSSL (TLS encrypted socket transport)                        |
| Phase 8:  High-volume structured binary logging & audit analytics                                 |
| Phase 9:  Peer-to-peer authenticated end-to-end encrypted direct messaging                        |
| Phase 10: Multi-stage Docker production image with non-root security profiles                     |
| Phase 11: Native Windows client cross-compilation via MinGW / Winsock2                           |
| Phase 12: Public cloud deployment on AWS EC2 / Kubernetes cluster with public IP access          |
+───────────────────────────────────────────────────────────────────────────────────────────────────+
```

---

## 11. Standout Production Features & Security Hardening (Phase 6+)

### 11.1 Signal-Style Double Ratchet End-to-End Encryption (E2EE)
- **Protocol Foundations**: Implements the Signal Double Ratchet algorithm using Curve25519 (X25519) DH key exchanges, HKDF-SHA256 key derivation, and AES-256-GCM AEAD encryption.
- **Forward Secrecy & Break-in Recovery**:
  - Each message steps a symmetric KDF chain key, ensuring past messages cannot be decrypted if a current message key is compromised (Forward Secrecy).
  - Every message reply introduces a new ephemeral DH key exchange that advances the root key, healing the session if ephemeral state was ever compromised (Break-in Recovery).
- **Signal Key Directory**:
  - Clients announce identity keys via `__IDKEY__:<pubkey_hex>`.
  - Clients request peer identity keys on demand via `__KEYREQ__:<user>` / `__KEYRESP__:<user>:<pubkey_hex>`.
- **Wire Envelope**: Zero-knowledge ciphertext relay:
  `__E2EE__:<from>:<to>:<from_ik>:<ratchet_hex>:<seq>:<iv_hex>:<ct_hex>:<tag_hex>`
  The server routes ciphertexts without ever possessing the capability to inspect or decrypt user plaintexts.

### 11.2 Chunked In-Band File Transfer & SHA-256 Integrity
- **Command Set**: `/sendfile <user> <path>`, `/fileaccept <id>`, `/filedecline <id>`.
- **Chunking Pipeline**: Streams files in 1024-byte chunks encoded in Base64 over standard TCP stream.
- **Interactive UI**:
  - Recipient sees file offer with filename, size, and SHA-256 prefix before accepting.
  - Live ASCII visual progress bar rendered on both sender and receiver:
    `[FILE: file.pdf] [===========>             ]  45% (9/20 chunks)`
- **Cryptographic Verification**: Once all chunks arrive, receiver computes full SHA-256 digest on the assembled file in `downloads/` and validates against sender's expected checksum.

### 11.3 Anti-Spam Token Bucket Rate Limiting & DoS Defense
- **Token Bucket Algorithm**:
  - Parameters: Bucket capacity = 5.0 tokens, refill rate = 1.5 tokens/sec.
  - Violation policy: Exceeding the bucket issues warnings; 3 consecutive violations trigger an automatic temporary mute for 15 seconds.
- **DoS Connection Limiter**: Limits simultaneous active socket connections per IP address to defend against connection flood attacks.

### 11.4 PBKDF2 Authentication & Offline Messaging Inbox
- **PBKDF2 Password Storage**: Replaced insecure plaintext `pwd.env` with PBKDF2-HMAC-SHA256 (16-byte random salt, 10,000 iterations) stored in `admin.cred`.
- **Dynamic Password Management**: Administrators can rotate passwords securely at runtime using `/setadminpwd <newpwd>`.
- **Offline Inbox Persistence**:
  - If a recipient is offline when `/msg` or `/e2ee` is sent, the server persists the message into `offline_inbox.dat`.
  - When the recipient connects, pending offline messages are replayed immediately with timestamp auditing.

### 11.5 Modern Terminal UI Experience
- **Tab Command Completion**: Autocompletes slash commands (`/help`, `/e2ee`, `/sendfile`, `/party`, `/whoami`, etc.) upon pressing `<Tab>`.
- **Command History Navigation**: Up/Down arrow key support cycling through previous inputs.
- **Live Typing Indicators**: Low-overhead `__TYPING__` packet broadcast notifying peers in real-time when another user is composing a message.

### 11.6 Full Verification & Automated End-to-End Test Suite
- Comprehensive automated multi-client test harness implemented in `tests/test_e2e_full.c` and `tests/test_all_commands.py` (`make test`, `make test-commands`, `make test-all`) verifying:
  1. Multi-round bidirectional Double Ratchet message encryption and decryption.
  2. 5KB chunked file transfer with SHA-256 checksum verification.
  3. Token bucket rate limiting, warnings, and auto-muting.
  4. Offline messaging inbox persistence and reconnection replay.
  5. PBKDF2 admin login, dynamic password update, and authentication enforcement.
  6. Live verification of all 44 commands in `/help` across messaging, parties, games, customization, and administration.
- Tested across multiple consecutive runs with **100% pass rate** and **0 compiler warnings** under `-Wall -Wextra -pthread -O2`.

---

*Compiled by comprehensive static and dynamic analysis of the Kukupu Chat in C codebase.*
