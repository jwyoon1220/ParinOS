CC = i686-linux-gnu-gcc
AS = nasm
LD = i686-linux-gnu-ld
NM = i686-linux-gnu-nm
IMAGE = $(BUILD_DIR)/ParinOS.img
KERNEL_SYM = kernel.sym
INC_DEST = include  # 🌟 이 변수가 상단에 확실히 정의되어 있어야 합니다.
SRC_DIR = src
ASM_DIR = $(SRC_DIR)/asm
LOADER_DIR = $(SRC_DIR)/loader
BUILD_DIR = makefile-build

CFLAGS = -ffreestanding -O2 -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables
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
KERNEL_SYM = kernel.sym  # 🌟 커널 심볼 테이블 파일 이름

LOADER_PAD_SIZE = 65536

all: $(IMAGE) $(KERNEL_SYM) # 🌟 빌드 시 심볼 파일도 생성하도록 설정

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

# C 파일 컴파일
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/loader/%.o: $(LOADER_DIR)/%.c | prep
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 어셈블리 컴파일
$(BUILD_DIR)/%_asm.o: $(ASM_DIR)/%.asm | prep
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# 2단계 C 로더 링크
$(BUILD_DIR)/loader.bin: $(BUILD_DIR)/loader_entry.o $(LOADER_C_OBJECTS)
	$(LD) $(LDFLAGS_LOADER) $^ --oformat binary -o $@
	truncate -s $(LOADER_PAD_SIZE) $@

# 🌟 최종 커널 링크 및 심볼 추출
# 먼저 ELF 형태로 링크한 뒤, 거기서 바이너리를 추출하고 심볼도 뽑아냅니다.
$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel_entry.o $(C_OBJECTS) $(ASM_OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	objcopy -O binary $< $@

# 🌟 커널 심볼 테이블 생성 (테스트 프로그램 링킹용)
$(KERNEL_SYM): $(BUILD_DIR)/kernel.elf
	@echo "Generating kernel symbol table..."
	$(NM) $< | awk '{ if($$2 == "T" || $$2 == "D" || $$2 == "B") print $$3 " = 0x" $$1 ";" }' > $@

$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.elf
	cat $^ > $@
	truncate -s 1048576  $@

run:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) headers
	clear
	qemu-system-i386 -m 256M -drive file=makefile-build/ParinOS.img -drive id=disk0,file=disk.img,if=none,format=raw -device ahci,id=ahci -serial stdio -device ide-hd,drive=disk0,bus=ahci.0

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_SYM)

.PHONY: headers all prep run clean

headers:
	@mkdir -p $(INC_DEST)
	@echo "Copying header files to $(INC_DEST)..."
	@cd $(SRC_DIR) && find . -name "*.h" -exec cp --parents \{\} ../$(INC_DEST)/ \;
	@echo "Done."