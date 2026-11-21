CC       := gcc
INCLUDE  := include
CFLAGS   := -std=c23 -Wall -Wextra -O2 -Isrc $(filter -I%,$(EINC)) -I$(INCLUDE)
LDFLAGS  += -static $(filter -L%,$(EINC))
LDLIBS   += $(filter -l%,$(EINC))

SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/init

SRCS     := $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

.PHONY: all clean run
all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)