/*
 * ParinOS 2단계 C 로더 (Stage 2 Loader)
 * ELF32 커널 로딩 지원 버전
 *
 * 메모리 레이아웃:
 *   0x10000 - 0x1FFFF : Stage2 로더 코드 (64KB)
 *   0x90000           : 스택 (boot.asm에서 설정)
 *   0x100000+         : 커널 세그먼트 로드 목적지 (PT_LOAD p_paddr)
 *   0x200000+         : kernel.elf 임시 버퍼 (512KB, 커널과 겹치지 않음)
 */

/* 디스크/메모리 레이아웃 상수 */
#define KERNEL_LBA_START    129         /* 커널 ELF 시작 LBA */
#define KERNEL_SECTORS      1024        /* 읽을 최대 섹터 수 (512KB) */
#define ELF_TEMP_ADDR       0x200000    /* kernel.elf 임시 버퍼 주소 (2MB) */

/* ELF32 관련 상수 (ELF 표준 스펙) */
#define EI_MAG0             0
#define EI_MAG1             1
#define EI_MAG2             2
#define EI_MAG3             3
#define EI_CLASS            4           /* 클래스(비트폭) 인덱스 */
#define EI_DATA             5           /* 엔디안 인덱스 */
#define ELFMAG0             0x7F
#define ELFMAG1             'E'
#define ELFMAG2             'L'
#define ELFMAG3             'F'
#define ELFCLASS32          1           /* 32비트 ELF */
#define ELFDATA2LSB         1           /* 리틀엔디안 (2의 보수) */
#define EM_386              3           /* x86 아키텍처 */
#define PT_LOAD             1           /* 로드 가능한 세그먼트 */

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

/* ─── ELF32 구조체 (표준 ELF 스펙 준수, packed) ─── */

/* ELF32 파일 헤더 */
typedef struct {
    uint8_t    e_ident[16];    /* 매직, 클래스, 엔디안, 버전 등 */
    uint16_t   e_type;         /* 파일 타입 */
    uint16_t   e_machine;      /* 아키텍처 (EM_386 = 3) */
    uint32_t   e_version;      /* 버전 */
    uint32_t   e_entry;        /* 커널 진입점 가상 주소 */
    uint32_t   e_phoff;        /* 프로그램 헤더 테이블 파일 오프셋 */
    uint32_t   e_shoff;        /* 섹션 헤더 테이블 파일 오프셋 */
    uint32_t   e_flags;        /* 아키텍처 종속 플래그 */
    uint16_t   e_ehsize;       /* ELF 헤더 크기 */
    uint16_t   e_phentsize;    /* 프로그램 헤더 엔트리 크기 */
    uint16_t   e_phnum;        /* 프로그램 헤더 엔트리 수 */
    uint16_t   e_shentsize;    /* 섹션 헤더 엔트리 크기 */
    uint16_t   e_shnum;        /* 섹션 헤더 엔트리 수 */
    uint16_t   e_shstrndx;     /* 섹션 이름 문자열 테이블 인덱스 */
} __attribute__((packed)) Elf32_Ehdr;

/* ELF32 프로그램 헤더 (세그먼트 헤더) */
typedef struct {
    uint32_t   p_type;         /* 세그먼트 타입 (PT_LOAD = 1) */
    uint32_t   p_offset;       /* 파일 내 오프셋 */
    uint32_t   p_vaddr;        /* 가상 주소 */
    uint32_t   p_paddr;        /* 물리 주소 (LMA, 로더가 사용) */
    uint32_t   p_filesz;       /* 파일 내 크기 */
    uint32_t   p_memsz;        /* 메모리 내 크기 (BSS 포함, >= p_filesz) */
    uint32_t   p_flags;        /* 접근 권한 플래그 */
    uint32_t   p_align;        /* 정렬 */
} __attribute__((packed)) Elf32_Phdr;

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

/* ─── 메모리 유틸리티 (표준 라이브러리 없이 자체 구현) ─── */
static void loader_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void loader_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = val;
}

/* ─── 시리얼 포트 제어 ─── */
static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    // 인터럽트 비활성화
    outb(COM1_PORT + 3, 0x80);    // DLAB 활성화
    outb(COM1_PORT + 0, 0x03);    // Baud Rate 38400
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);    // 8N1
    outb(COM1_PORT + 2, 0xC7);    // FIFO 활성화
    outb(COM1_PORT + 4, 0x0B);    // Modem 제어
}

static int is_transmit_empty(void) {
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

/* uint32_t 값을 16진수(0xXXXXXXXX)로 출력 */
static void lkprint_hex(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;
    buf[0] = '0'; buf[1] = 'x';
    for (i = 9; i >= 2; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[10] = '\0';
    lkprint(buf);
}

/* ─── ATA PIO 28-bit LBA 디스크 읽기 ─── */
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

/*
 * load_kernel_image - 디스크에서 kernel.elf를 out_buf에 읽어 들인다.
 *
 * out_buf  : ELF 파일 전체를 저장할 버퍼 (ELF_TEMP_ADDR)
 * max      : 버퍼 최대 크기 (KERNEL_SECTORS * 512)
 *
 * 반환값: 0=성공, -1=디스크 오류
 *
 * 주의: 나중에 FAT32 파일 로딩으로 교체할 때는 이 함수만 수정하면 된다.
 */
static int load_kernel_image(void *out_buf, uint32_t max) {
    uint8_t *buf = (uint8_t *)out_buf;
    uint32_t sectors = max / 512;
    uint32_t i;

    for (i = 0; i < sectors; i++) {
        if (ata_read_sector((uint32_t)(KERNEL_LBA_START + i),
                            buf + i * 512U) != 0) {
            lkprint("\n[DISK ERROR] LBA=");
            lkprint_hex((uint32_t)(KERNEL_LBA_START + i));
            lkprint("\n");
            return -1;
        }
        if ((i & 0x3F) == 0) lkputchar('.');
    }
    return 0;
}

/*
 * load_elf_kernel - ELF 버퍼를 파싱하여 PT_LOAD 세그먼트를 메모리에 배치한다.
 *
 * elf_buf   : kernel.elf 전체가 들어 있는 버퍼
 * out_entry : 성공 시 e_entry (커널 진입점) 저장
 *
 * 반환값: 0=성공, 음수=실패
 */
static int load_elf_kernel(void *elf_buf, uint32_t *out_entry) {
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_buf;
    Elf32_Phdr *phdr;
    uint16_t i;

    /* ── ELF 유효성 검사 ── */

    /* 매직 넘버 확인: 0x7F 'E' 'L' 'F' */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        lkprint("[ELF ERROR] Invalid magic (not an ELF file)\n");
        return -1;
    }

    /* 32비트 클래스 확인 */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        lkprint("[ELF ERROR] Not a 32-bit ELF (ELFCLASS32 required)\n");
        return -2;
    }

    /* 리틀엔디안 확인 */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        lkprint("[ELF ERROR] Not little-endian (ELFDATA2LSB required)\n");
        return -3;
    }

    /* x86 아키텍처 확인 */
    if (ehdr->e_machine != EM_386) {
        lkprint("[ELF ERROR] Not EM_386 architecture\n");
        return -4;
    }

    /* 프로그램 헤더 존재 확인 */
    if (ehdr->e_phnum == 0 || ehdr->e_phoff == 0) {
        lkprint("[ELF ERROR] No program headers found\n");
        return -5;
    }

    lkprint("[ELF] e_entry=");
    lkprint_hex(ehdr->e_entry);
    lkprint(" e_phnum=");
    lkprint_hex((uint32_t)ehdr->e_phnum);
    lkprint("\n");

    /* ── PT_LOAD 세그먼트 로딩 ── */
    phdr = (Elf32_Phdr *)((uint8_t *)elf_buf + ehdr->e_phoff);

    for (i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        /* p_memsz >= p_filesz 검사 */
        if (phdr[i].p_memsz < phdr[i].p_filesz) {
            lkprint("[ELF ERROR] p_memsz < p_filesz in segment ");
            lkprint_hex((uint32_t)i);
            lkprint("\n");
            return -6;
        }

        lkprint("[ELF] LOAD paddr=");
        lkprint_hex(phdr[i].p_paddr);
        lkprint(" filesz=");
        lkprint_hex(phdr[i].p_filesz);
        lkprint(" memsz=");
        lkprint_hex(phdr[i].p_memsz);
        lkprint("\n");

        /*
         * 목적지 주소로 p_paddr 사용:
         *   kernel.ld가 identity mapping (LMA == VMA == 0x100000+)이므로
         *   p_paddr == p_vaddr 이며, 물리 주소로 직접 복사한다.
         */
        uint8_t *dest = (uint8_t *)phdr[i].p_paddr;
        const uint8_t *src = (const uint8_t *)elf_buf + phdr[i].p_offset;

        /* 파일 데이터 복사 */
        loader_memcpy(dest, src, phdr[i].p_filesz);

        /* BSS 영역 제로 초기화 (p_memsz > p_filesz 인 경우) */
        loader_memset(dest + phdr[i].p_filesz, 0,
                      phdr[i].p_memsz - phdr[i].p_filesz);
    }

    *out_entry = ehdr->e_entry;
    return 0;
}

/* ─── 로더 메인 ─── */
void loader_main(void) {
    serial_init();

    lkprint("ParinOS Stage 2 Loader (ELF32)\n");
    lkprint("Loading kernel.elf to 0x200000...");

    /* ELF 파일을 임시 버퍼(0x200000)에 로드 */
    uint8_t *elf_buf = (uint8_t *)ELF_TEMP_ADDR;
    if (load_kernel_image(elf_buf, (uint32_t)KERNEL_SECTORS * 512U) != 0) {
        lkprint("[FATAL] Disk read failed. System halted.\n");
        for (;;);
    }

    lkprint(" OK.\n");
    lkprint("Parsing and loading ELF32 segments...\n");

    /* ELF 파싱 및 PT_LOAD 세그먼트를 p_paddr로 복사 */
    uint32_t entry = 0;
    if (load_elf_kernel(elf_buf, &entry) != 0) {
        lkprint("[FATAL] ELF load failed. System halted.\n");
        for (;;);
    }

    lkprint("ELF loaded. Jumping to kernel at ");
    lkprint_hex(entry);
    lkprint("...\n");

    /* e_entry로 점프하여 커널 실행 (커널은 이미 올바른 주소에 배치됨) */
    typedef void (*kernel_entry_t)(void);
    kernel_entry_t kernel_entry = (kernel_entry_t)entry;
    kernel_entry();
}