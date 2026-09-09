CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Isrc/include -Ilibs/include -static -lpthread
LDFLAGS = -static

SRC_DIR = src/code
INC_DIR = src/include
BIN_DIR = bin
OBJ_DIR = obj
OBJ_DIR_SRC = $(OBJ_DIR)/code
OBJ_DIR_UTILS = $(OBJ_DIR)/utils

LIBS_DIR    = libs
LIB_OUT_DIR = $(LIBS_DIR)/lib

SRCS   = $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS   = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/init

UTILITY_SRCS = $(shell find src/utils -type f -name '*.c')
UTILITY_NAMES = $(patsubst src/utils/%.c,%,$(UTILITY_SRCS))
UTILITY_OBJS = $(patsubst src/utils/%.c,$(OBJ_DIR_UTILS)/%.o,$(UTILITY_SRCS))
UTILITY_TARGETS = $(patsubst %,$(BIN_DIR)/%,$(UTILITY_NAMES))

LIB_NAMES := $(notdir $(wildcard $(LIBS_DIR)/src/*))

LDFLAGS += -L$(LIB_OUT_DIR) $(patsubst %,-l:%.a,$(LIB_NAMES))

all: libs $(BIN_DIR) $(OBJ_DIR) $(TARGET) $(UTILITY_TARGETS)
libs:
	$(MAKE) -C $(LIBS_DIR) static

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS) libs
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR_UTILS)/%.o: src/utils/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%: $(OBJ_DIR_UTILS)/%.o libs | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	$(MAKE) -C $(LIBS_DIR) clean

install-libs: libs
	sudo mkdir -p /usr/lib /usr/lib64
	sudo cp -a $(LIBS_DIR)/lib/libunotifi.a /usr/lib/
	sudo cp -a $(LIBS_DIR)/lib/libunotifi.a /usr/lib64/
	if [ -f $(LIBS_DIR)/lib/libunotifi.so ]; then \
		sudo cp -a $(LIBS_DIR)/lib/libunotifi.so /usr/lib/ && \
		sudo cp -a $(LIBS_DIR)/lib/libunotifi.so /usr/lib64/; \
	fi
	sudo ldconfig || true

prepmkinit:
	@sudo mkinitcpio -c ./base/minit-mkinitcpio.conf -g ./base/initramfs.img -k $(shell uname -r)
	@dd if=/dev/zero of=./base/disk.img bs=1M count=512
	@mkfs.ext4 -F ./base/disk.img

	@sudo umount /tmp/_root 2>/dev/null || true
	@mkdir -p /tmp/_root
	@sudo mount -o loop ./base/disk.img /tmp/_root
	@sudo cp -a ./base/rootfs/* /tmp/_root/
	@sudo mkdir /tmp/_root/sbin
	@sudo cp -a ./base/rootfs/init /tmp/_root/sbin/init
	@sudo umount /tmp/_root

.PHONY: all clean prepmkinit libs
.SECONDARY: $(UTILITY_OBJS)
