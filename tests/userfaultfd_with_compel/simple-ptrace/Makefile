# Command BIN
CC			:= gcc
LD			:= ld
CP			:= cp
MKDIR       := mkdir

COMPEL          := ../../../../../crui-untouched/criu-3.16.1/compel/compel-host

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

CFLAGS		:= -I$(INC_DIR) -I ./ -c -g -O2 -Wall
LDFLAGS		:= -lpthread
COMPEL_INC      := $(shell $(COMPEL) includes)
COMPEL_SLIBS    := $(shell $(COMPEL) --static libs)

# Use `make VERBOSE=YES` to print detailed compilation log
ifneq ($(VERBOSE),YES)
HUSH_CC		= @echo ' [CC]\t\t'$@;
HUSH_CC_LD	= @echo ' [CC+LD]\t'$@;
HUSH_LD		= @echo ' [LD]\t\t'$@;
HUSH_MKDIR	= @echo ' [MKDIR]\t'$@;
endif

all: victim obj $(BIN)

victim: victim.c
	$(HUSH_CC) $(CC)  -o $@ $^

obj:
	$(HUSH_MKDIR) $(MKDIR) -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c parasite.h
	$(HUSH_CC) $(CC) $(CFLAGS) $(shell $(COMPEL) includes) -c -o $@ $< 

parasite.h: parasite.po
	$(COMPEL) hgen -o $@ -f $<

parasite.po: parasite.o 
	ld $(shell $(COMPEL) ldflags) -o $@ $< $(shell $(COMPEL) plugins fds)

parasite.o: parasite.c
	$(CC) $(CFLAGS) -c $(shell $(COMPEL) cflags) -o $@ $^

$(BIN): $(OBJ) 
	$(HUSH_LD) $(CC) -o $@ $^ $(LDFLAGS) $(shell $(COMPEL) --static libs)

clean:
	@$(RM) -rf $(BIN) $(OBJ_DIR)

.PHONY: all pre clean
