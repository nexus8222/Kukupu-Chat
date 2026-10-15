CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2 -Iinclude
LDFLAGS = -lssl -lcrypto

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
              $(SRC_DIR)/client/e2ee.c \
              $(SRC_DIR)/client/file_transfer.c \
              $(SRCS_COMMON)

# Object files
OBJS_SERVER = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_SERVER))
OBJS_CLIENT = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_CLIENT))

TARGET_SERVER = server
TARGET_CLIENT = client
TARGET_TEST = test_e2e_full

.PHONY: all clean test

all: $(TARGET_SERVER) $(TARGET_CLIENT)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIENT): $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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

test-all: test test-commands

$(TARGET_TEST): $(TESTS_DIR)/test_e2e_full.c $(BUILD_DIR)/common/crypto_utils.o $(BUILD_DIR)/client/e2ee.o $(BUILD_DIR)/client/file_transfer.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_TEST) *.o
