CC = i686-linux-gnu-gcc
AS = nasm
LD = i686-linux-gnu-ld

SRC_DIR = src
ASM_DIR = $(SRC_DIR)/asm
BUILD_DIR = makefile-build

CFLAGS = -ffreestanding -O2 -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables
# 🌟 링커 스크립트를 사용하도록 수정 (Ttext 삭제)
LDFLAGS = -m elf_i386 -nostdlib -no-pie -T kernel.ld

# 1. 모든 하위 폴더의 소스 탐색
C_SOURCES = $(shell find $(SRC_DIR) -name "*.c")
ALL_ASM = $(shell find $(ASM_DIR) -name "*.asm")
ASM_SOURCES = $(filter-out $(ASM_DIR)/boot.asm $(ASM_DIR)/kernel_entry.asm, $(ALL_ASM))

# 2. 오브젝트 경로 생성
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst $(ASM_DIR)/%.asm, $(BUILD_DIR)/%_asm.o, $(ASM_SOURCES))

IMAGE = $(BUILD_DIR)/ParinOS.img

all: $(IMAGE)

prep:
	@mkdir -p $(BUILD_DIR)

# 부트로더 (바이너리)
$(BUILD_DIR)/boot.bin: $(ASM_DIR)/boot.asm | prep
	$(AS) -f bin $< -o $@

# 커널 진입점
$(BUILD_DIR)/kernel_entry.o: $(ASM_DIR)/kernel_entry.asm | prep
	$(AS) -f elf32 $< -o $@

# C 파일 컴파일
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 어셈블리 컴파일
$(BUILD_DIR)/%_asm.o: $(ASM_DIR)/%.asm | prep
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# 🌟 3. 최종 커널 링크 (kernel.ld의 규칙을 따름)
# ld가 $^를 나열할 때 kernel_entry.o가 가장 먼저 오도록 보장해야 합니다.
$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel_entry.o $(C_OBJECTS) $(ASM_OBJECTS)
	$(LD) $(LDFLAGS) $^ --oformat binary -o $@

$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/kernel.bin
	cat $^ > $@
	truncate -s 131072 $@

run:
	$(MAKE) clean
	$(MAKE) all
	clear
	qemu-system-i386 -drive format=raw,file=$(IMAGE) -rtc base=localtime -serial stdio -serial file:serial.log

clean:
	rm -rf $(BUILD_DIR)