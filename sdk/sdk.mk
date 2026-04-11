# sdk/sdk.mk — Reusable build fragment for ParinOS user programs
#
# Usage in your program's Makefile:
#   SDK_DIR ?= ../../sdk          # path to the sdk/ directory
#   include $(SDK_DIR)/sdk.mk
#
# Then define PROG and SRCS, and call the $(PROG) target:
#   PROG := hello
#   SRCS := src/hello.c
#   $(eval $(call SDK_PROG,$(PROG),$(SRCS)))
#
# Or write your own link rule using the variables exported here:
#   $(UPCC) $(SDK_CFLAGS) -c src/foo.c -o foo.o
#   $(UPLD) $(SDK_LDFLAGS) foo.o $(SDK_LIBPARIN) -o foo

# ── Resolve SDK root (works when included from any depth) ─────────────────
_SDK_MK_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

SDK_DIR   ?= $(_SDK_MK_DIR)
SDK_INC   := $(SDK_DIR)/include
SDK_LINK  := $(SDK_DIR)/link/userprog.ld
SDK_BUILD := $(SDK_DIR)/build
SDK_LIBPARIN := $(SDK_BUILD)/libparin.a

# ── Toolchain ─────────────────────────────────────────────────────────────
UPCC := $(shell which i686-linux-gnu-gcc 2>/dev/null || echo gcc)
UPLD := $(shell which i686-linux-gnu-ld  2>/dev/null || echo ld)

# ── Flags ─────────────────────────────────────────────────────────────────
SDK_CFLAGS  := -ffreestanding -nostdlib -m32 -fno-pic \
               -fno-stack-protector -O2 -Wall -Wextra \
               -I$(SDK_INC)
SDK_LDFLAGS := -m elf_i386 -nostdlib -T $(SDK_LINK)

# ── Ensure the SDK library is built before any program ────────────────────
$(SDK_LIBPARIN):
	$(MAKE) -C $(SDK_DIR) all

# ── Macro: build a single-source-list program ─────────────────────────────
# Usage: $(eval $(call SDK_PROG,progname,src1.c src2.c,...,outdir))
#   outdir defaults to current dir if omitted
define SDK_PROG
_$(1)_SRCS  := $(2)
_$(1)_OBJS  := $$(patsubst %.c, $(SDK_BUILD)/prog/$(1)/%.o, $$(notdir $$(_$(1)_SRCS)))
_$(1)_OUTDIR := $(if $(3),$(3),.)

$$(SDK_BUILD)/prog/$(1)/%.o: $$(filter %/$$*.c, $$(_$(1)_SRCS))
	@mkdir -p $$(dir $$@)
	$(UPCC) $(SDK_CFLAGS) -c $$< -o $$@

$$(_$(1)_OUTDIR)/$(1): $$(_$(1)_OBJS) $(SDK_LIBPARIN)
	@mkdir -p $$(_$(1)_OUTDIR)
	$(UPLD) $(SDK_LDFLAGS) $$^ -o $$@

$(1): $$(_$(1)_OUTDIR)/$(1)
.PHONY: $(1)
endef
