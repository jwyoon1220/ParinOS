# sdk/template/Makefile — Build template using the ParinOS SDK
#
# Usage:
#   make              — build the program
#   make clean        — remove build output
#   make install      — copy binary to ../../disk_src/bin/

# ── SDK location (relative to this Makefile) ──────────────────────────────
SDK_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# ── Program settings ──────────────────────────────────────────────────────
PROG    := myprogram
SRCS    := src/main.c
OUTDIR  := .
BINDIR  := $(abspath $(SDK_DIR)/../disk_src/bin)

# ── Toolchain (inherited from SDK) ────────────────────────────────────────
UPCC := $(shell which i686-linux-gnu-gcc 2>/dev/null || echo gcc)
UPLD := $(shell which i686-linux-gnu-ld  2>/dev/null || echo ld)

SDK_INC      := $(SDK_DIR)/include
SDK_LINK     := $(SDK_DIR)/link/userprog.ld
SDK_LIBPARIN := $(SDK_DIR)/build/libparin.a

CFLAGS  := -ffreestanding -nostdlib -m32 -fno-pic \
           -fno-stack-protector -O2 -Wall -Wextra \
           -I$(SDK_INC)
LDFLAGS := -m elf_i386 -nostdlib -T $(SDK_LINK)

# ── Build directory ────────────────────────────────────────────────────────
BUILD := $(SDK_DIR)/build/prog/$(PROG)

# ── Object files ──────────────────────────────────────────────────────────
OBJS := $(patsubst src/%.c, $(BUILD)/%.o, $(SRCS))

# ── Targets ───────────────────────────────────────────────────────────────
.PHONY: all clean install

all: $(SDK_LIBPARIN) $(OUTDIR)/$(PROG)
	@echo "Built: $(OUTDIR)/$(PROG)"

# Ensure SDK library exists
$(SDK_LIBPARIN):
	$(MAKE) -C $(SDK_DIR) all

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(UPCC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/$(PROG): $(OBJS) $(SDK_LIBPARIN)
	$(UPLD) $(LDFLAGS) $^ -o $@

install: all
	@mkdir -p $(BINDIR)
	cp $(OUTDIR)/$(PROG) $(BINDIR)/$(PROG)
	@echo "Installed to $(BINDIR)/$(PROG)"

clean:
	rm -rf $(BUILD) $(OUTDIR)/$(PROG)
