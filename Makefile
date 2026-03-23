# --- 컴파일러 및 도구 설정 ---
CC  = i686-linux-gnu-gcc
CXX = i686-linux-gnu-g++
AS  = nasm
LD  = i686-linux-gnu-ld
NM  = i686-linux-gnu-nm
OBJCOPY = objcopy

# 유저 프로그램 컴파일러: 크로스 컴파일러 우선, 없으면 네이티브 gcc -m32 사용
UPCC := $(shell which i686-linux-gnu-gcc 2>/dev/null || echo gcc)
UPLD := $(shell which i686-linux-gnu-ld  2>/dev/null || echo ld)

# --- 경로 및 파일 설정 ---
SRC_DIR   = src
ASM_DIR   = $(SRC_DIR)/asm
LOADER_DIR = $(SRC_DIR)/loader
BUILD_DIR = makefile-build
INC_DEST  = include

IMAGE      = $(BUILD_DIR)/ParinOS.img
KERNEL_SYM = kernel.sym
DISK_IMG   = disk.img
DISK_SRC   = ./disk_src
DISK_SIZE_MB = 1024

# --- 컴파일 플래그 ---
CFLAGS   = -ffreestanding -O2 -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -mmmx -msse2
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -m32 -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti \
           -std=c++17 -I$(SRC_DIR)/cpp
LDFLAGS        = -m elf_i386 -nostdlib -no-pie -T kernel.ld
LDFLAGS_LOADER = -m elf_i386 -nostdlib -no-pie -T loader.ld

# --- 소스 및 오브젝트 리스트 ---
C_SOURCES   = $(shell find $(SRC_DIR) -name "*.c" -not -path "$(LOADER_DIR)/*")
CXX_SOURCES = $(shell find $(SRC_DIR) -name "*.cpp" -not -path "$(LOADER_DIR)/*")
ALL_ASM     = $(shell find $(ASM_DIR) -name "*.asm")
ASM_SOURCES = $(filter-out $(ASM_DIR)/boot.asm $(ASM_DIR)/kernel_entry.asm $(ASM_DIR)/loader_entry.asm, $(ALL_ASM))
LOADER_C_SOURCES = $(wildcard $(LOADER_DIR)/*.c)

C_OBJECTS        = $(patsubst $(SRC_DIR)/%.c,  $(BUILD_DIR)/%.o,       $(C_SOURCES))
CXX_OBJECTS      = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,       $(CXX_SOURCES))
ASM_OBJECTS      = $(patsubst $(ASM_DIR)/%.asm, $(BUILD_DIR)/%_asm.o,  $(ASM_SOURCES))
LOADER_C_OBJECTS = $(patsubst $(LOADER_DIR)/%.c, $(BUILD_DIR)/loader/%.o, $(LOADER_C_SOURCES))

FONT_FILE  = $(DISK_SRC)/FONT.TTF
FONT_OBJECT= $(BUILD_DIR)/font_ttf.o

LOADER_PAD_SIZE = 65536

# --- 유저 프로그램 설정 ---
USERPROG_DIR    = $(SRC_DIR)/userprog
USERPROG_INC    = $(USERPROG_DIR)/include
USERPROG_LIB    = $(USERPROG_DIR)/lib
USERPROG_LD     = $(USERPROG_DIR)/userprog.ld
USERPROG_BINDIR = $(DISK_SRC)/bin

# -nostdinc 제거: 컴파일러 기본 헤더(stdint.h, stdarg.h, stddef.h 등) 사용
USERPROG_CFLAGS  = -ffreestanding -nostdlib -m32 -fno-pic \
                   -fno-stack-protector -O2 -Wall -Wextra \
                   -I$(USERPROG_INC)
USERPROG_LDFLAGS = -m elf_i386 -nostdlib -T $(USERPROG_LD)
# 유저 라이브러리 소스 (lib/*.c)
USERPROG_LIB_SRCS = $(wildcard $(USERPROG_LIB)/*.c)
USERPROG_LIB_OBJS = $(patsubst $(USERPROG_LIB)/%.c, \
                      $(BUILD_DIR)/userprog/lib/%.o, $(USERPROG_LIB_SRCS))

# 유저 프로그램 소스 (userprog/*.c)
USERPROG_SRCS = $(wildcard $(USERPROG_DIR)/*.c)
USERPROG_BINS = $(patsubst $(USERPROG_DIR)/%.c, $(USERPROG_BINDIR)/%, $(USERPROG_SRCS))

# --- 기본 타겟 ---
all: prep $(IMAGE) $(KERNEL_SYM) userprog $(DISK_IMG)

prep:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(DISK_SRC)
	@mkdir -p $(BUILD_DIR)/userprog/lib
	@mkdir -p $(USERPROG_BINDIR)

# --- 빌드 규칙 ---

# 부트로더
$(BUILD_DIR)/boot.bin: $(ASM_DIR)/boot.asm
	$(AS) -f bin $< -o $@

# 로더 및 커널 진입점
$(BUILD_DIR)/loader_entry.o: $(ASM_DIR)/loader_entry.asm
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/kernel_entry.o: $(ASM_DIR)/kernel_entry.asm
	$(AS) -f elf32 $< -o $@

# 컴파일 규칙
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/loader/%.o: $(LOADER_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_asm.o: $(ASM_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# 링크 과정
$(BUILD_DIR)/loader.bin: $(BUILD_DIR)/loader_entry.o $(LOADER_C_OBJECTS)
	$(LD) $(LDFLAGS_LOADER) $^ --oformat binary -o $@
	truncate -s $(LOADER_PAD_SIZE) $@

$(FONT_OBJECT): $(FONT_FILE)
	@mkdir -p $(dir $@)
	cd $(DISK_SRC) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 FONT.TTF ../$@

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel_entry.o $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(FONT_OBJECT)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# 심볼 테이블 추출
$(KERNEL_SYM): $(BUILD_DIR)/kernel.elf
	@echo "Generating kernel symbol table..."
	$(NM) $< | awk '{ if($$2 == "T" || $$2 == "D" || $$2 == "B") print $$3 " = 0x" $$1 ";" }' > $@

# OS 이미지 생성
$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.elf
	cat $^ > $@
	truncate -s 8388608 $@

# --- 데이터 디스크 이미지 생성 (FAT32) ---
# mtools를 사용하여 sudo 없이 수행
$(DISK_IMG):
	@echo "Creating $(DISK_SIZE_MB)MB FAT32 disk image..."
	@rm -f $(DISK_IMG)
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE_MB)
	mformat -i $(DISK_IMG) -F -v "PARIN_DATA" ::
	@if [ -d $(DISK_SRC) ]; then \
		echo "Copying files from $(DISK_SRC) to disk image..."; \
		mcopy -i $(DISK_IMG) -s $(DISK_SRC)/* ::/; \
	fi

# --- 유틸리티 타겟 ---

# 유저 라이브러리 오브젝트 컴파일
$(BUILD_DIR)/userprog/lib/%.o: $(USERPROG_LIB)/%.c
	@mkdir -p $(dir $@)
	$(UPCC) $(USERPROG_CFLAGS) -c $< -o $@

# 유저 프로그램 오브젝트 컴파일
$(BUILD_DIR)/userprog/%.o: $(USERPROG_DIR)/%.c
	@mkdir -p $(dir $@)
	$(UPCC) $(USERPROG_CFLAGS) -c $< -o $@

# 유저 프로그램 링크 (프로그램 오브젝트 + 라이브러리)
$(USERPROG_BINDIR)/%: $(BUILD_DIR)/userprog/%.o $(USERPROG_LIB_OBJS)
	@mkdir -p $(dir $@)
	$(UPLD) $(USERPROG_LDFLAGS) $^ -o $@

# 유저 프로그램 전체 빌드 타겟
userprog: $(USERPROG_BINS)
	@echo "User programs built: $(USERPROG_BINS)"

headers:
	@mkdir -p $(INC_DEST)
	@echo "Copying header files to $(INC_DEST)..."
	@cd $(SRC_DIR) && find . -name "*.h" -exec cp --parents \{\} ../$(INC_DEST)/ \;
	@echo "Done."

run: all headers
	@echo "Launching QEMU..."
	qemu-system-x86_64 -m 256M -cpu qemu32,+mmx,+sse2 \
		-drive file=$(IMAGE),format=raw,index=0,media=disk \
		-drive file=$(DISK_IMG),format=raw,id=disk0,if=none \
		-device ahci,id=ahci \
		-device ide-hd,drive=disk0,bus=ahci.0 \
		-serial stdio

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_SYM) $(DISK_IMG) $(INC_DEST)

.PHONY: all prep headers run clean userprog