# sdk/Makefile — ParinOS Software Development Kit
#
# Targets:
#   make all       — Build sdk/build/libparin.a (default)
#   make clean     — Remove build artefacts
#
# After building, point your program's Makefile to this SDK:
#   SDK_DIR := /path/to/sdk
#   include $(SDK_DIR)/sdk.mk

# ── Paths ─────────────────────────────────────────────────────────────────
SDK_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD    := $(SDK_DIR)/build
INC      := $(SDK_DIR)/include
LIB_SRC  := $(SDK_DIR)/lib

# ── Toolchain ─────────────────────────────────────────────────────────────
CC  := $(shell which i686-linux-gnu-gcc 2>/dev/null || echo gcc)
AS  := $(shell which nasm               2>/dev/null || echo nasm)
AR  := $(shell which i686-linux-gnu-ar  2>/dev/null || echo ar)

CFLAGS := -ffreestanding -nostdlib -m32 -fno-pic \
          -fno-stack-protector -O2 -Wall -Wextra \
          -I$(INC)

# ── Library sources ────────────────────────────────────────────────────────
LIB_C_SRCS := $(LIB_SRC)/crt0.c \
              $(LIB_SRC)/stdio.c \
              $(LIB_SRC)/stdlib.c \
              $(LIB_SRC)/string.c
LIB_C_OBJS := $(patsubst $(LIB_SRC)/%.c, $(BUILD)/%.o, $(LIB_C_SRCS))
LIB_ASM_OBJ := $(BUILD)/syscall.o

ALL_OBJS := $(LIB_C_OBJS) $(LIB_ASM_OBJ)
LIBPARIN  := $(BUILD)/libparin.a

# ── Default target ─────────────────────────────────────────────────────────
.PHONY: all clean

all: $(LIBPARIN)
	@echo "ParinOS SDK built: $(LIBPARIN)"

# ── Compile C sources ──────────────────────────────────────────────────────
$(BUILD)/%.o: $(LIB_SRC)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Assemble syscall stubs ─────────────────────────────────────────────────
$(LIB_ASM_OBJ): $(LIB_SRC)/syscall.asm
	@mkdir -p $(BUILD)
	$(AS) -f elf32 $< -o $@

# ── Archive static library ────────────────────────────────────────────────
$(LIBPARIN): $(ALL_OBJS)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^

# ── Clean ─────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
