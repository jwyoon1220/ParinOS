/*
 * ParinOS 2단계 C 로더 (Stage 2 Loader)
 * * 시리얼(COM1) 및 VGA 동시 출력 지원 버전
 */

/* 디스크/메모리 레이아웃 상수 */
#define KERNEL_LBA_START    129         /* 커널 시작 LBA (boot 1섹터 + loader 128섹터) */

/* ELF32 상수 */
#define ELF_MAGIC0  0x7F
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'
#define PT_LOAD     1

/* I/O 포트 정의 */
#define ATA_DATA_PORT       0x1F0
#define ATA_SECTOR_COUNT    0x1F2
#define ATA_LBA_LOW         0x1F3
#define ATA_LBA_MID         0x1F4
#define ATA_LBA_HIGH        0x1F5
#define ATA_DRIVE_HEAD      0x1F6
#define ATA_CMD_STATUS      0x1F7
#define ATA_STATUS_BSY      0x80
#define ATA_STATUS_DRQ      0x08
#define ATA_CMD_READ        0x20

#define COM1_PORT           0x3F8

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

/* ELF32 구조체 */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/* ─── I/O 포트 제어 ─── */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ─── 시리얼 포트 제어 ─── */
static void serial_init() {
    outb(COM1_PORT + 1, 0x00);    // 인터럽트 비활성화
    outb(COM1_PORT + 3, 0x80);    // DLAB 활성화
    outb(COM1_PORT + 0, 0x03);    // Baud Rate 38400
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);    // 8N1
    outb(COM1_PORT + 2, 0xC7);    // FIFO 활성화
    outb(COM1_PORT + 4, 0x0B);    // Modem 제어
}

static int is_transmit_empty() {
    return inb(COM1_PORT + 5) & 0x20;
}

static void serial_putchar(char c) {
    if (c == '\n') {
        while (is_transmit_empty() == 0);
        outb(COM1_PORT, '\r');
    }
    while (is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

/* ─── 출력 통합 (VGA + Serial) ─── */
static uint16_t *const vga_buf = (uint16_t *)0xB8000;
static int vga_col = 0;
static int vga_row = 1;

static void lkputchar(char c) {
    // VGA 처리
    if (c == '\n') {
        vga_row++;
        vga_col = 0;
    } else {
        vga_buf[vga_row * 80 + vga_col] = (uint16_t)((0x0A << 8) | (uint8_t)c);
        vga_col++;
        if (vga_col >= 80) {
            vga_col = 0;
            vga_row++;
        }
    }
    // 시리얼 처리
    serial_putchar(c);
}

static void lkprint(const char *s) {
    while (*s) lkputchar(*s++);
}

/* ─── ATA 디스크 읽기 ─── */
static int ata_read_sector(uint32_t lba, uint8_t *buf) {
    uint32_t timeout;

    for (timeout = 1000000; timeout > 0; timeout--) {
        if (!(inb(ATA_CMD_STATUS) & ATA_STATUS_BSY)) break;
    }

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba));
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_CMD_STATUS, ATA_CMD_READ);

    for (timeout = 1000000; timeout > 0; timeout--) {
        uint8_t status = inb(ATA_CMD_STATUS);
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) break;
    }
    if (timeout == 0) return -1;

    uint16_t *ptr = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_DATA_PORT);
    }
    return 0;
}

/* ─── ELF 세그먼트 로드 헬퍼 ─── */
static void load_segment(uint32_t lba_base, uint32_t file_offset,
                         uint8_t *dest, uint32_t filesz) {
    uint32_t lba      = lba_base + file_offset / 512;
    uint32_t in_off   = file_offset % 512;   /* 첫 섹터 내 바이트 오프셋 */
    uint32_t remaining = filesz;

    while (remaining > 0) {
        uint8_t sector_buf[512];
        if (ata_read_sector(lba, sector_buf) != 0) {
            lkprint("\n[ELF] Disk read error\n");
            for (;;) ;
        }

        uint32_t copy = 512 - in_off;
        if (copy > remaining) copy = remaining;

        for (uint32_t i = 0; i < copy; i++) {
            dest[i] = sector_buf[in_off + i];
        }

        dest      += copy;
        remaining -= copy;
        in_off     = 0;   /* 이후 섹터는 오프셋 0부터 시작 */
        lba++;
    }
}

/* ─── 로더 메인 ─── */
void loader_main(void) {
    serial_init();

    lkprint("ParinOS Stage 2 Loader\n");
    lkprint("Loading kernel to 0x100000...");

    /* Step 1: 헤더 영역(4섹터=2KB) 읽기 – ELF 헤더 + 프로그램 헤더 테이블 */
    uint8_t header_buf[4 * 512];
    for (int i = 0; i < 4; i++) {
        if (ata_read_sector(KERNEL_LBA_START + i, header_buf + i * 512) != 0) {
            lkprint("\n[ELF] Cannot read ELF header\n");
            for (;;) ;
        }
    }

    /* Step 2: ELF 매직 확인 */
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)header_buf;
    if (ehdr->e_ident[0] != ELF_MAGIC0 || ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 || ehdr->e_ident[3] != ELF_MAGIC3) {
        lkprint("\n[ELF] Not a valid ELF binary!\n");
        for (;;) ;
    }

    /* Step 3: 각 PT_LOAD 세그먼트를 물리 주소(p_paddr)에 로드 */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr *)(header_buf + ehdr->e_phoff +
                                           (uint32_t)i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD || phdr->p_filesz == 0) continue;

        uint8_t *dest = (uint8_t *)phdr->p_paddr;
        load_segment(KERNEL_LBA_START, phdr->p_offset, dest, phdr->p_filesz);

        /* BSS 영역(p_memsz > p_filesz) 초기화 */
        if (phdr->p_memsz > phdr->p_filesz) {
            uint8_t *bss = dest + phdr->p_filesz;
            uint32_t bss_sz = phdr->p_memsz - phdr->p_filesz;
            for (uint32_t j = 0; j < bss_sz; j++) bss[j] = 0;
        }

        lkputchar('.');
    }

    lkprint(" OK.\nJumping to kernel...\n");

    /* Step 4: 커널 엔트리 포인트로 점프 */
    typedef void (*kernel_entry_t)(void);
    kernel_entry_t kernel_entry = (kernel_entry_t)ehdr->e_entry;
    kernel_entry();
}