CC = i686-linux-gnu-gcc
AS = nasm
LD = i686-linux-gnu-ld

SRC_DIR = src
ASM_DIR = $(SRC_DIR)/asm
LOADER_DIR = $(SRC_DIR)/loader
BUILD_DIR = makefile-build

CFLAGS = -ffreestanding -O2 -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables
# 🌟 링커 스크립트를 사용하도록 수정 (Ttext 삭제)
LDFLAGS        = -m elf_i386 -nostdlib -no-pie -T kernel.ld
LDFLAGS_LOADER = -m elf_i386 -nostdlib -no-pie -T loader.ld

# 1. 모든 하위 폴더의 소스 탐색
C_SOURCES = $(shell find $(SRC_DIR) -name "*.c" -not -path "$(LOADER_DIR)/*")
ALL_ASM = $(shell find $(ASM_DIR) -name "*.asm")
ASM_SOURCES = $(filter-out $(ASM_DIR)/boot.asm $(ASM_DIR)/kernel_entry.asm $(ASM_DIR)/loader_entry.asm, $(ALL_ASM))
LOADER_C_SOURCES = $(wildcard $(LOADER_DIR)/*.c)

# 2. 오브젝트 경로 생성
C_OBJECTS        = $(patsubst $(SRC_DIR)/%.c,  $(BUILD_DIR)/%.o,       $(C_SOURCES))
ASM_OBJECTS      = $(patsubst $(ASM_DIR)/%.asm, $(BUILD_DIR)/%_asm.o,  $(ASM_SOURCES))
LOADER_C_OBJECTS = $(patsubst $(LOADER_DIR)/%.c, $(BUILD_DIR)/loader/%.o, $(LOADER_C_SOURCES))

IMAGE = $(BUILD_DIR)/ParinOS.img

# 2단계 로더 크기: 64KB = 128섹터 (LBA 1~128)
# 커널은 LBA 129부터 시작 (loader.c의 KERNEL_LBA_START와 일치)
LOADER_PAD_SIZE = 65536

all: $(IMAGE)

prep:
	@mkdir -p $(BUILD_DIR)

# 부트로더 (바이너리)
$(BUILD_DIR)/boot.bin: $(ASM_DIR)/boot.asm | prep
	$(AS) -f bin $< -o $@

# 2단계 로더 진입점 (어셈블리)
$(BUILD_DIR)/loader_entry.o: $(ASM_DIR)/loader_entry.asm | prep
	$(AS) -f elf32 $< -o $@

# 커널 진입점
$(BUILD_DIR)/kernel_entry.o: $(ASM_DIR)/kernel_entry.asm | prep
	$(AS) -f elf32 $< -o $@

# C 파일 컴파일 (커널)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# C 파일 컴파일 (로더)
$(BUILD_DIR)/loader/%.o: $(LOADER_DIR)/%.c | prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 어셈블리 컴파일
$(BUILD_DIR)/%_asm.o: $(ASM_DIR)/%.asm | prep
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# 🌟 2단계 C 로더 링크 → 64KB로 패딩 (LBA 1~128)
$(BUILD_DIR)/loader.bin: $(BUILD_DIR)/loader_entry.o $(LOADER_C_OBJECTS)
	$(LD) $(LDFLAGS_LOADER) $^ --oformat binary -o $@
	truncate -s $(LOADER_PAD_SIZE) $@

# 🌟 최종 커널 링크 (kernel.ld의 규칙을 따름)
# ld가 $^를 나열할 때 kernel_entry.o가 가장 먼저 오도록 보장해야 합니다.
$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel_entry.o $(C_OBJECTS) $(ASM_OBJECTS)
	$(LD) $(LDFLAGS) $^ --oformat binary -o $@

# 🌟 디스크 이미지 생성:
#   LBA 0       : boot.bin  (512바이트, 1섹터)
#   LBA 1~128   : loader.bin (64KB = 128섹터)
#   LBA 129~    : kernel.bin
$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.bin
	cat $^ > $@
	# 1mb
	truncate -s 1048576  $@

run:
	$(MAKE) clean
	$(MAKE) all
	clear
	qemu-system-i386 -m 256M -drive file=makefile-build/ParinOS.img -drive id=disk0,file=disk.img,if=none,format=raw -device ahci,id=ahci -serial stdio -device ide-hd,drive=disk0,bus=ahci.0

clean:
	rm -rf $(BUILD_DIR)