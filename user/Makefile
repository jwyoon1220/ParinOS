# user/Makefile — 유저 프로그램 전체 빌드
#
# 사용법:
#   make all      — 모든 유저 프로그램 빌드
#   make shell    — shell만 빌드
#   make clean    — 빌드 결과물 삭제
#
# 각 프로그램 디렉터리의 Makefile로 개별 빌드도 가능합니다.

# ── 경로 설정 ────────────────────────────────────────────────────────────
SELF_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TOP       := $(abspath $(SELF_DIR)/..)

BINDIR    := $(TOP)/disk_src/bin
BUILD     := $(TOP)/makefile-build/user
INC       := $(SELF_DIR)/include
LD_SCRIPT := $(SELF_DIR)/userprog.ld

# ── 컴파일러 설정 ────────────────────────────────────────────────────────
UPCC  := $(shell which i686-linux-gnu-gcc 2>/dev/null || echo gcc)
UPLD  := $(shell which i686-linux-gnu-ld  2>/dev/null || echo ld)
UPAS  := $(shell which nasm 2>/dev/null || echo nasm)

CFLAGS  := -ffreestanding -nostdlib -m32 -fno-pic \
           -fno-stack-protector -O2 -Wall -Wextra \
           -I$(INC)
LDFLAGS := -m elf_i386 -nostdlib -T $(LD_SCRIPT)

# ── 라이브러리 ──────────────────────────────────────────────────────────
LIB_DIR  := $(SELF_DIR)/lib
LIB_SRCS := $(LIB_DIR)/crt0.c $(LIB_DIR)/stdio.c \
            $(LIB_DIR)/stdlib.c $(LIB_DIR)/string.c
LIB_OBJS := $(patsubst $(LIB_DIR)/%.c, $(BUILD)/lib/%.o, $(LIB_SRCS))
LIB_OBJS += $(BUILD)/lib/syscall.o

# ── 프로그램 목록 ────────────────────────────────────────────────────────
PROGS := shell cat echo ls mkdir rm cp hello jvm

# ── 기본 타겟 ────────────────────────────────────────────────────────────
.PHONY: all clean lib $(PROGS)

all: prep lib $(addprefix $(BINDIR)/,$(PROGS))
	@echo "All user programs built."

prep:
	@mkdir -p $(BUILD)/lib $(BINDIR)
	@$(foreach p,$(PROGS),mkdir -p $(BUILD)/$(p);)

# ── 라이브러리 빌드 ──────────────────────────────────────────────────────
lib: $(LIB_OBJS)

$(BUILD)/lib/%.o: $(LIB_DIR)/%.c
	@mkdir -p $(BUILD)/lib
	$(UPCC) $(CFLAGS) -c $< -o $@

$(BUILD)/lib/syscall.o: $(LIB_DIR)/syscall.asm
	@mkdir -p $(BUILD)/lib
	$(UPAS) -f elf32 $< -o $@

# ── 개별 프로그램 빌드 규칙 생성 매크로 ──────────────────────────────────
define PROG_template
PROG_$(1)_SRCS := $$(wildcard $(SELF_DIR)/$(1)/src/*.c)
PROG_$(1)_OBJS := $$(patsubst $(SELF_DIR)/$(1)/src/%.c, $(BUILD)/$(1)/%.o, $$(PROG_$(1)_SRCS))

$(BUILD)/$(1)/%.o: $(SELF_DIR)/$(1)/src/%.c
	@mkdir -p $(BUILD)/$(1)
	$(UPCC) $(CFLAGS) -c $$< -o $$@

$(BINDIR)/$(1): $$(PROG_$(1)_OBJS) $(LIB_OBJS)
	@mkdir -p $(BINDIR)
	$(UPLD) $(LDFLAGS) $$^ -o $$@

$(1): $(BINDIR)/$(1)
endef

$(foreach prog,$(PROGS),$(eval $(call PROG_template,$(prog))))

# ── 정리 ────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD) $(addprefix $(BINDIR)/,$(PROGS))
