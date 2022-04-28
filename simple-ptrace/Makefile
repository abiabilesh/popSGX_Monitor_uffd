# Command BIN
CC			:= gcc
LD			:= ld
CP			:= cp
MKDIR       := mkdir

# Dirs
SRC_DIR     := ./src
INC_DIR     := ./inc
TEST_DIR    := ./test
OBJ_DIR     := ./obj
BUILD		:= ./build

# Source and object files
SRC         := $(shell find $(SRC_DIR) -name '*.c')
OBJ			:= $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o, $(SRC))
BIN			:= monitor

CFLAGS		:= -I$(INC_DIR) -c -g -O2 -Wall
LDFLAGS		:= -lpthread

# Use `make VERBOSE=YES` to print detailed compilation log
ifneq ($(VERBOSE),YES)
HUSH_CC		= @echo ' [CC]\t\t'$@;
HUSH_CC_LD	= @echo ' [CC+LD]\t'$@;
HUSH_LD		= @echo ' [LD]\t\t'$@;
HUSH_MKDIR	= @echo ' [MKDIR]\t'$@;
endif

all: obj $(BIN)

obj:
	$(HUSH_MKDIR) $(MKDIR) -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(HUSH_CC) $(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	$(HUSH_LD) $(CC) -o $@ $^ $(LDFLAGS)

clean:
	@$(RM) -rf $(BIN) $(OBJ_DIR)

.PHONY: all pre clean
