/*
 * ParinOS 2단계 C 로더 (Stage 2 Loader)
 * * 시리얼(COM1) 및 VGA 동시 출력 지원 버전
 */

/* 디스크/메모리 레이아웃 상수 */
#define KERNEL_LBA_START    129         /* 커널 시작 LBA */
#define KERNEL_SECTORS      256         /* 읽을 커널 섹터 수 */
#define KERNEL_LOAD_ADDR    0x100000    /* 커널 로드 목적지: 1MB */

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

static void kputchar(char c) {
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

static void kprint(const char *s) {
    while (*s) kputchar(*s++);
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

/* ─── 로더 메인 ─── */
void loader_main(void) {
    serial_init();

    kprint("ParinOS Stage 2 Loader Online\n");
    kprint("Loading kernel to 0x100000...");

    uint8_t *dest = (uint8_t *)KERNEL_LOAD_ADDR;

    for (int i = 0; i < KERNEL_SECTORS; i++) {
        if (ata_read_sector(KERNEL_LBA_START + i, dest + (uint32_t)i * 512U) != 0) {
            kprint("\n[DISK ERROR at LBA ");
            // 에러 시 무한 루프
            for (;;) ;
        }

        if ((i & 0x0F) == 0) kputchar('.');
    }

    kprint(" OK\nJumping to kernel...\n");

    // 1MB 영역으로 점프
    typedef void (*kernel_entry_t)(void);
    kernel_entry_t kernel_entry = (kernel_entry_t)KERNEL_LOAD_ADDR;
    kernel_entry();
}