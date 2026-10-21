CC ?= gcc
CFLAGS = -Wall -Wextra -pthread -O2 -Iinclude
LDFLAGS = -lssl -lcrypto

# Auto-detect local OpenSSL paths if present
LOCAL_INC = $(wildcard $(HOME)/.local/usr/include)
ifneq ($(LOCAL_INC),)
    CFLAGS += -I$(HOME)/.local/usr/include
    LDFLAGS += -L$(HOME)/.local/usr/lib/x86_64-linux-gnu
endif

BUILD_DIR = build
SRC_DIR = src
INC_DIR = include
TESTS_DIR = tests

# Common Sources (shared between server and client)
SRCS_COMMON = $(SRC_DIR)/common/crypto_utils.c \
              $(SRC_DIR)/common/emoji.c

# Server Sources
SRCS_SERVER = $(SRC_DIR)/server/server.c \
              $(SRC_DIR)/server/admin.c \
              $(SRC_DIR)/server/commands.c \
              $(SRC_DIR)/server/mute.c \
              $(SRC_DIR)/server/log.c \
              $(SRC_DIR)/server/banlist.c \
              $(SRC_DIR)/server/motd.c \
              $(SRC_DIR)/server/party.c \
              $(SRC_DIR)/server/lastseen.c \
              $(SRC_DIR)/server/vanish.c \
              $(SRC_DIR)/server/pwdgen.c \
              $(SRC_DIR)/server/game.c \
              $(SRC_DIR)/server/ratelimit.c \
              $(SRC_DIR)/server/db.c \
              $(SRC_DIR)/server/utils.c \
              $(SRCS_COMMON)

# Client Sources
SRCS_CLIENT = $(SRC_DIR)/client/client.c \
              $(SRC_DIR)/client/ui.c \
              $(SRC_DIR)/client/login_ui.c \
              $(SRC_DIR)/client/e2ee.c \
              $(SRC_DIR)/client/file_transfer.c \
              $(SRCS_COMMON)

# Linux Object files
OBJS_SERVER = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_SERVER))
OBJS_CLIENT = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_CLIENT))

TARGET_SERVER = server
TARGET_CLIENT = client
TARGET_TEST = test_e2e_full

# Windows Cross-Compilation Settings (MinGW-w64)
ifneq ($(wildcard $(HOME)/.local/bin/x86_64-w64-mingw32-gcc),)
    CC_WIN ?= $(HOME)/.local/bin/x86_64-w64-mingw32-gcc
else
    CC_WIN ?= x86_64-w64-mingw32-gcc
endif
WIN_BUILD_DIR = $(BUILD_DIR)/win

WIN_INC = -Iinclude
WIN_LOCAL_INC = $(wildcard $(HOME)/.local/mingw64/mingw64/include)
ifneq ($(WIN_LOCAL_INC),)
    WIN_INC += -I$(HOME)/.local/mingw64/mingw64/include
endif

MINGW_SYS_INC = $(wildcard $(HOME)/.local/usr/x86_64-w64-mingw32/include)
ifneq ($(MINGW_SYS_INC),)
    WIN_INC += -I$(HOME)/.local/usr/x86_64-w64-mingw32/include
endif

WIN_CFLAGS = -Wall -Wextra -O2 $(WIN_INC) -pthread
WIN_LDFLAGS = -static -lssl -lcrypto -lws2_32 -lmswsock -lcrypt32 -lssp -pthread
WIN_LOCAL_LIB = $(wildcard $(HOME)/.local/mingw64/mingw64/lib)
ifneq ($(WIN_LOCAL_LIB),)
    WIN_LDFLAGS := -L$(HOME)/.local/mingw64/mingw64/lib $(WIN_LDFLAGS)
endif

MINGW_SYS_LIB = $(wildcard $(HOME)/.local/usr/x86_64-w64-mingw32/lib)
ifneq ($(MINGW_SYS_LIB),)
    WIN_LDFLAGS := -L$(HOME)/.local/usr/x86_64-w64-mingw32/lib $(WIN_LDFLAGS)
endif

TARGET_SERVER_WIN = server.exe
TARGET_CLIENT_WIN = client.exe

OBJS_SERVER_WIN = $(patsubst $(SRC_DIR)/%.c, $(WIN_BUILD_DIR)/%.o, $(SRCS_SERVER))
OBJS_CLIENT_WIN = $(patsubst $(SRC_DIR)/%.c, $(WIN_BUILD_DIR)/%.o, $(SRCS_CLIENT))

.PHONY: all clean test test-commands test-auth test-all windows

all: $(TARGET_SERVER) $(TARGET_CLIENT)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIENT): $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Windows targets (.exe)
windows: $(TARGET_SERVER_WIN) $(TARGET_CLIENT_WIN)

$(WIN_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC_WIN) $(WIN_CFLAGS) -c -o $@ $<

$(TARGET_SERVER_WIN): $(OBJS_SERVER_WIN)
	$(CC_WIN) $(WIN_CFLAGS) -o $@ $^ $(WIN_LDFLAGS)

$(TARGET_CLIENT_WIN): $(OBJS_CLIENT_WIN)
	$(CC_WIN) $(WIN_CFLAGS) -o $@ $^ $(WIN_LDFLAGS)

test: $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_TEST)
	@killall -9 $(TARGET_SERVER) 2>/dev/null || true
	@./$(TARGET_SERVER) >/dev/null 2>&1 & SRV_PID=$$!; \
	sleep 0.6; \
	./$(TARGET_TEST); \
	TEST_RES=$$?; \
	kill -9 $$SRV_PID 2>/dev/null || true; \
	exit $$TEST_RES

test-commands: $(TARGET_SERVER)
	python3 tests/test_all_commands.py

test-auth: $(TARGET_SERVER)
	python3 tests/test_auth_and_features.py

test-all: test test-commands test-auth

$(TARGET_TEST): $(TESTS_DIR)/test_e2e_full.c $(BUILD_DIR)/common/crypto_utils.o $(BUILD_DIR)/client/e2ee.o $(BUILD_DIR)/client/file_transfer.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_TEST) $(TARGET_SERVER_WIN) $(TARGET_CLIENT_WIN) *.o
