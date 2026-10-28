CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c99
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDLIBS  ?= -lm

SRC_DIR := src
OBJ_DIR := build
BIN     := wwhd-edit
SRCS    := $(SRC_DIR)/main.c $(SRC_DIR)/save_format.c
OBJS    := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
HEADERS := $(SRC_DIR)/save_format.h

.PHONY: all clean test info help

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

test: $(BIN)
	sh tests/parity.sh

info: $(BIN)
	./$(BIN) --info cking.sav

help:
	./$(BIN) --help

clean:
	rm -rf $(OBJ_DIR) $(BIN)
