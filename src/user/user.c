//
// src/user/user.c
// 유저 모드 시스템 콜 디스패처 및 syscall 구현
//

#include "user.h"
#include "../hal/vga.h"
#include "../drivers/serial.h"
#include "../kernel/multitasking.h"
#include "../fs/vfs.h"
#include "../elf/elf.h"
#include "../std/kstdio.h"
#include "../mem/vmm.h"
#include "../net/net.h"
#include "../std/malloc.h"
#include "../std/kstring.h"

// ─────────────────────────────────────────────────────────────────────────────
// 파일 디스크립터 테이블
// FD 0 = stdin (keyboard), FD 1 = stdout (VGA), FD 2 = stderr (VGA)
// FD 3..FD_TABLE_MAX-1 = VFS 파일 또는 디렉터리
// ─────────────────────────────────────────────────────────────────────────────
#define FD_TABLE_MAX  16
#define FD_FILE_MIN   3

static vfs_node_t* g_fd_table[FD_TABLE_MAX];
static uint8_t     g_fd_is_dir[FD_TABLE_MAX]; // 1 = 디렉터리 FD

// ─────────────────────────────────────────────────────────────────────────────
// 개별 시스템 콜 구현
// ─────────────────────────────────────────────────────────────────────────────

// SYS_EXIT(1): 현재 프로세스 종료
static uint32_t sys_exit(uint32_t code) {
    klog_info("[SYSCALL] sys_exit(%d)\n", (int)code);
    kprocess_exit();
    // 실행 여기 도달하지 않음
    return 0;
}

// SYS_WRITE(4): 파일 디스크립터에 쓰기
//   fd=1/2 → VGA 출력
//   fd>=3  → VFS 파일 쓰기
static uint32_t sys_write(uint32_t fd, uint32_t buf_vaddr, uint32_t count) {
    const char* s = (const char*)buf_vaddr;

    if (fd == 1 || fd == 2) {
        // stdout / stderr → 시리얼 + VGA 출력
        for (uint32_t i = 0; i < count; i++) {
            write_serial(s[i]);
        }
        for (uint32_t i = 0; i < count; i++) {
            lkputchar(s[i]);
        }
        return count;
    }

    if (fd >= FD_FILE_MIN && fd < FD_TABLE_MAX && g_fd_table[fd] != NULL) {
        uint32_t written = 0;
        int err = vfs_write(g_fd_table[fd], s, count, &written);
        if (err != VFS_OK) return (uint32_t)-1;
        return written;
    }

    return (uint32_t)-9; // -EBADF
}

// SYS_READ(3): 파일 디스크립터에서 읽기
//   fd=0 → 키보드 한 줄 읽기
//   fd>=3 → VFS 파일 읽기
static uint32_t sys_read(uint32_t fd, uint32_t buf_vaddr, uint32_t count) {
    char* buf = (char*)buf_vaddr;

    if (fd == 0) {
        // stdin → 키보드에서 한 줄 읽기
        int n = keyboard_readline(buf, (int)count);
        if (n < 0) return (uint32_t)-5; // -EIO
        // 개행 추가 (POSIX read 동작과 유사)
        if ((uint32_t)n < count) {
            buf[n++] = '\n';
        }
        return (uint32_t)n;
    }

    if (fd >= FD_FILE_MIN && fd < FD_TABLE_MAX && g_fd_table[fd] != NULL) {
        uint32_t bytes_read = 0;
        int err = vfs_read(g_fd_table[fd], buf, count, &bytes_read);
        if (err != VFS_OK && bytes_read == 0) return 0; // EOF
        return bytes_read;
    }

    return (uint32_t)-9; // -EBADF
}

// SYS_OPEN(5): 파일 열기
//   Linux O_RDONLY=0, O_WRONLY=1, O_RDWR=2, O_CREAT=0x40, O_TRUNC=0x200, O_APPEND=0x400
static uint32_t sys_open(uint32_t path_vaddr, uint32_t flags, uint32_t mode) {
    (void)mode;
    const char* path = (const char*)path_vaddr;

    // 빈 FD 슬롯 탐색
    int fd = -1;
    for (int i = FD_FILE_MIN; i < FD_TABLE_MAX; i++) {
        if (g_fd_table[i] == NULL) { fd = i; break; }
    }
    if (fd < 0) return (uint32_t)-23; // -ENFILE

    // Linux 플래그 → VFS 플래그 변환
    uint8_t vfs_flags = 0;
    int access = (int)(flags & 3);
    if (access == 0) vfs_flags |= VFS_O_READ;                    // O_RDONLY
    if (access == 1) vfs_flags |= VFS_O_WRITE;                   // O_WRONLY
    if (access == 2) vfs_flags |= (VFS_O_READ | VFS_O_WRITE);    // O_RDWR
    if (flags & 0x40)  vfs_flags |= VFS_O_CREATE;                // O_CREAT
    if (flags & 0x200) vfs_flags |= VFS_O_TRUNC;                 // O_TRUNC
    if (flags & 0x400) vfs_flags |= VFS_O_APPEND;                // O_APPEND
    if (vfs_flags == 0) vfs_flags = VFS_O_READ;

    vfs_node_t* node = NULL;
    int err = vfs_open(path, vfs_flags, &node);
    if (err != VFS_OK) return (uint32_t)(int32_t)err; // 음수 에러 코드

    g_fd_table[fd] = node;
    return (uint32_t)fd;
}

// SYS_CLOSE(6): 파일 닫기
static uint32_t sys_close(uint32_t fd) {
    /* 소켓 FD는 별도 소켓 레이어로 위임 */
    if (is_socket_fd((int)fd)) {
        int err = ksocket_close((int)fd);
        return (err == 0) ? 0 : (uint32_t)-9;
    }
    if (fd < FD_FILE_MIN || fd >= FD_TABLE_MAX || g_fd_table[fd] == NULL) {
        return (uint32_t)-9; // -EBADF
    }
    vfs_close(g_fd_table[fd]);
    g_fd_table[fd] = NULL;
    return 0;
}

// SYS_LSEEK(19): 파일 포인터 이동
static uint32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence) {
    if (fd < FD_FILE_MIN || fd >= FD_TABLE_MAX || g_fd_table[fd] == NULL) {
        return (uint32_t)-9; // -EBADF
    }
    int vfs_whence;
    switch (whence) {
        case 0: vfs_whence = VFS_SEEK_SET; break;
        case 1: vfs_whence = VFS_SEEK_CUR; break;
        case 2: vfs_whence = VFS_SEEK_END; break;
        default: return (uint32_t)-22; // -EINVAL
    }
    int err = vfs_seek(g_fd_table[fd], (int32_t)offset, vfs_whence);
    if (err != VFS_OK) return (uint32_t)(int32_t)err;
    return g_fd_table[fd]->offset;
}

// SYS_GETPID(20): 현재 프로세스 ID 반환
static uint32_t sys_getpid(void) {
    return (uint32_t)kprocess_id();
}

// SYS_YIELD(158): CPU 양보
static uint32_t sys_yield(void) {
    kschedule();
    return 0;
}

// SYS_BRK(45): 유저 힙 브레이크 포인터 조정
//   addr == 0  → 현재 brk 반환 (조회)
//   addr > cur → 힙 확장: 필요한 페이지를 VMM 으로 할당 후 brk 갱신
//   addr < cur → 힙 축소: brk 값만 갱신 (페이지는 유지)
static uint32_t sys_brk(uint32_t addr) {
    kthread_t *t = kthread_current();
    if (!t) return (uint32_t)-12; /* -ENOMEM */

    uint32_t cur = t->user_brk;

    /* 조회 또는 초기화 전 */
    if (addr == 0 || addr == cur) return cur;

    if (addr > cur) {
        /* 힙 확장: [cur, new_top) 범위에 아직 매핑되지 않은 페이지 할당
         *
         *  cur_page_top: cur 를 포함하는 페이지의 바로 다음 페이지 시작
         *                (즉, 아직 할당되지 않은 첫 번째 페이지)
         *  new_page_top: addr 를 커버하기 위해 필요한 페이지 경계 끝
         */
        uint32_t cur_page_top = (cur + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);
        uint32_t new_page_top = (addr + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);

        if (new_page_top > cur_page_top) {
            uint32_t count = (new_page_top - cur_page_top) / PAGE_SIZE;
            vmm_result_t res = vmm_alloc_virtual_pages(cur_page_top, count, VMM_ALLOC_USER);
            if (res != VMM_SUCCESS) {
                /* 할당 실패: 기존 brk 를 그대로 반환 */
                return cur;
            }
        }
    }
    /* addr < cur (힙 축소): brk 값만 낮춤, 페이지는 해제하지 않음 */

    t->user_brk = addr;
    return addr;
}

typedef struct {
    char *path;
    int argc;
    char **argv;
} exec_args_t;

static exec_args_t g_exec_staging;
static volatile int g_exec_staging_busy = 0;

static void exec_thread_entry(void) {
    char *path = g_exec_staging.path;
    int argc = g_exec_staging.argc;
    char **argv = g_exec_staging.argv;

    g_exec_staging_busy = 0; // Release staging lock

    elf_execute_in_ring3(path, argc, (const char **)argv);

    // If it returns, exit process
    kprocess_exit();
}

static uint32_t sys_exec(uint32_t path_vaddr, uint32_t argc, uint32_t argv_vaddr) {
    const char* path = (const char*)path_vaddr;
    const char** argv = (const char**)argv_vaddr;

    if (path == NULL || argv == NULL || argc == 0 || argc > 32) {
        return (uint32_t)-22; // -EINVAL
    }

    // 1. path 복사
    char* kpath = (char*)kmalloc(256);
    if (!kpath) return (uint32_t)-12; // -ENOMEM
    strcpy(kpath, path);

    // 2. argv 복사
    char** kargv = (char**)kmalloc(sizeof(char*) * (argc + 1));
    if (!kargv) {
        kfree(kpath);
        return (uint32_t)-12;
    }

    for (uint32_t i = 0; i < argc; i++) {
        if (argv[i] == NULL) {
            kargv[i] = NULL;
            continue;
        }
        int len = strlen(argv[i]);
        kargv[i] = (char*)kmalloc(len + 1);
        if (!kargv[i]) {
            for (uint32_t j = 0; j < i; j++) {
                if (kargv[j]) kfree(kargv[j]);
            }
            kfree(kargv);
            kfree(kpath);
            return (uint32_t)-12;
        }
        strcpy(kargv[i], argv[i]);
    }
    kargv[argc] = NULL;

    // 3. 파일 및 ELF 유효성 검사 (Ring 3 파괴 전 임시 검사)
    File file;
    if (fat_file_open(&file, kpath, FAT_READ) != FAT_ERR_NONE) {
        for (uint32_t i = 0; i < argc; i++) {
            if (kargv[i]) kfree(kargv[i]);
        }
        kfree(kargv);
        kfree(kpath);
        return (uint32_t)-2; // -ENOENT
    }

    Elf32_Ehdr ehdr;
    int bytes_read = 0;
    int read_ok = (fat_file_read(&file, &ehdr, sizeof(Elf32_Ehdr), &bytes_read) == FAT_ERR_NONE);
    fat_file_close(&file);

    if (!read_ok || !elf_check_supported(&ehdr)) {
        for (uint32_t i = 0; i < argc; i++) {
            if (kargv[i]) kfree(kargv[i]);
        }
        kfree(kargv);
        kfree(kpath);
        return (uint32_t)-8; // -ENOEXEC
    }

    // Acquire staging area lock
    while (g_exec_staging_busy) {
        kschedule();
    }
    g_exec_staging_busy = 1;
    g_exec_staging.path = kpath;
    g_exec_staging.argc = (int)argc;
    g_exec_staging.argv = kargv;

    int child_pid = kcreate_process(kpath, exec_thread_entry);
    if (child_pid < 0) {
        g_exec_staging_busy = 0;
        for (uint32_t i = 0; i < argc; i++) {
            if (kargv[i]) kfree(kargv[i]);
        }
        kfree(kargv);
        kfree(kpath);
        return (uint32_t)-1;
    }

    int child_tid = processes[child_pid].thread_ids[0];

    // Wait for the child to release staging area AND terminate
    while (g_exec_staging_busy || (threads[child_tid].state != KTHREAD_UNUSED && threads[child_tid].pid == (uint32_t)child_pid)) {
        kschedule();
    }

    // Clean up copied memory in parent
    for (uint32_t i = 0; i < argc; i++) {
        if (kargv[i]) kfree(kargv[i]);
    }
    kfree(kargv);
    kfree(kpath);

    return 0;
}

// SYS_UNLINK(10): 파일 삭제
static uint32_t sys_unlink(uint32_t path_vaddr) {
    const char* path = (const char*)path_vaddr;
    int err = vfs_unlink(path);
    return (err == VFS_OK) ? 0 : (uint32_t)-1;
}

// SYS_MKDIR(39): 디렉터리 생성
static uint32_t sys_mkdir(uint32_t path_vaddr, uint32_t mode) {
    (void)mode;
    const char* path = (const char*)path_vaddr;
    int err = vfs_mkdir(path);
    return (err == VFS_OK) ? 0 : (uint32_t)-1;
}

// SYS_STAT(106): 파일/디렉터리 정보 조회
// 유저 공간 struct stat: { uint32_t st_size; uint16_t st_mode; }
static uint32_t sys_stat(uint32_t path_vaddr, uint32_t stat_vaddr) {
    const char* path = (const char*)path_vaddr;
    struct {
        uint32_t st_size;
        uint16_t st_mode;
    }* buf = (void*)stat_vaddr;

    vfs_stat_t vstat;
    int err = vfs_stat(path, &vstat);
    if (err != VFS_OK) return (uint32_t)-1;

    buf->st_size = vstat.size;
    buf->st_mode = (vstat.attr & VFS_ATTR_DIR) ? 0x4000 : 0x8000;
    return 0;
}

// SYS_OPENDIR(200): 디렉터리 열기 → FD 반환
static uint32_t sys_opendir(uint32_t path_vaddr) {
    const char* path = (const char*)path_vaddr;

    int fd = -1;
    for (int i = FD_FILE_MIN; i < FD_TABLE_MAX; i++) {
        if (g_fd_table[i] == NULL) { fd = i; break; }
    }
    if (fd < 0) return (uint32_t)-23; // -ENFILE

    vfs_node_t* dirnode = NULL;
    int err = vfs_opendir(path, &dirnode);
    if (err != VFS_OK) return (uint32_t)(int32_t)err;

    g_fd_table[fd]  = dirnode;
    g_fd_is_dir[fd] = 1;
    return (uint32_t)fd;
}

// SYS_READDIR(201): 다음 디렉터리 엔트리 읽기
// 유저 공간 struct dirent: { char d_name[256]; uint32_t d_size; uint8_t d_type; }
static uint32_t sys_readdir(uint32_t fd, uint32_t dirent_vaddr) {
    if (fd < FD_FILE_MIN || fd >= FD_TABLE_MAX ||
        g_fd_table[fd] == NULL || !g_fd_is_dir[fd]) {
        return (uint32_t)-9; // -EBADF
    }

    struct {
        char     d_name[256];
        uint32_t d_size;
        uint8_t  d_type;
    }* ent = (void*)dirent_vaddr;

    vfs_stat_t vstat;
    int err = vfs_readdir(g_fd_table[fd], &vstat);
    if (err != VFS_OK) return (uint32_t)-1; // EOF 또는 오류

    /* 엔트리 데이터 복사 */
    int i;
    for (i = 0; i < 255 && vstat.name[i]; i++)
        ent->d_name[i] = vstat.name[i];
    ent->d_name[i] = '\0';
    ent->d_size = vstat.size;
    ent->d_type = (vstat.attr & VFS_ATTR_DIR) ? 2 : 1; /* DT_DIR=2, DT_REG=1 */
    return 0;
}

// SYS_CLOSEDIR(202): 디렉터리 FD 닫기
static uint32_t sys_closedir(uint32_t fd) {
    if (fd < FD_FILE_MIN || fd >= FD_TABLE_MAX ||
        g_fd_table[fd] == NULL || !g_fd_is_dir[fd]) {
        return (uint32_t)-9; // -EBADF
    }
    vfs_closedir(g_fd_table[fd]);
    g_fd_table[fd]  = NULL;
    g_fd_is_dir[fd] = 0;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 소켓 시스템 콜 구현  (SYS_SOCKET=300 ~ SYS_GETHOST=308)
// ─────────────────────────────────────────────────────────────────────────────

// SYS_SOCKET(300): 소켓 생성  socket(domain, type, protocol)
static uint32_t sys_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    (void)protocol;
    int sfd = ksocket_create((int)domain, (int)type);
    return (sfd < 0) ? (uint32_t)(int32_t)sfd : (uint32_t)sfd;
}

// SYS_CONNECT(301): 원격 호스트에 연결  connect(sfd, addr, port)
static uint32_t sys_connect(uint32_t sfd, uint32_t addr, uint32_t port) {
    int ret = ksocket_connect((int)sfd, addr, (uint16_t)port);
    return (uint32_t)(int32_t)ret;
}

// SYS_SEND(302): 데이터 전송  send(sfd, buf_vaddr, len)
static uint32_t sys_send(uint32_t sfd, uint32_t buf_vaddr, uint32_t len) {
    int ret = ksocket_send((int)sfd, (const void *)buf_vaddr, len);
    return (uint32_t)(int32_t)ret;
}

// SYS_RECV(303): 데이터 수신  recv(sfd, buf_vaddr, len)
static uint32_t sys_recv(uint32_t sfd, uint32_t buf_vaddr, uint32_t len) {
    int ret = ksocket_recv((int)sfd, (void *)buf_vaddr, len);
    return (uint32_t)(int32_t)ret;
}

// SYS_BIND(304): 소켓 주소 바인드  bind(sfd, addr, port)
static uint32_t sys_bind(uint32_t sfd, uint32_t addr, uint32_t port) {
    int ret = ksocket_bind((int)sfd, addr, (uint16_t)port);
    return (uint32_t)(int32_t)ret;
}

// SYS_LISTEN(305): 연결 수신 대기  listen(sfd, backlog)
static uint32_t sys_listen(uint32_t sfd, uint32_t backlog) {
    int ret = ksocket_listen((int)sfd, (int)backlog);
    return (uint32_t)(int32_t)ret;
}

// SYS_ACCEPT(306): 연결 수락  accept(sfd, addr_vaddr, port_vaddr)
static uint32_t sys_accept(uint32_t sfd, uint32_t addr_vaddr,
                            uint32_t port_vaddr) {
    uint32_t *addr_ptr = (addr_vaddr != 0) ? (uint32_t *)addr_vaddr : (uint32_t *)0;
    uint16_t *port_ptr = (port_vaddr != 0) ? (uint16_t *)port_vaddr : (uint16_t *)0;
    int ret = ksocket_accept((int)sfd, addr_ptr, port_ptr);
    return (uint32_t)(int32_t)ret;
}

// SYS_GETHOST(308): 호스트명 → IPv4  gethostbyname(name_vaddr, addr_vaddr)
static uint32_t sys_gethost(uint32_t name_vaddr, uint32_t addr_vaddr) {
    const char *name = (const char *)name_vaddr;
    uint32_t   *addr = (uint32_t *)addr_vaddr;
    int ret = ksocket_gethostbyname(name, addr);
    return (uint32_t)(int32_t)ret;
}

// SYS_CLEAR(400): VESA 화면 클리어
static uint32_t sys_clear(void) {
    vga_clear();
    return 0;
}

// SYS_DUMP_HEAP(401): 커널 힙 메모리 상태 덤프
static uint32_t sys_dump_heap(void) {
    dump_heap_stat();
    return 0;
}

// SYS_DUMP_THREADS(402): 커널 멀티태스킹 프로세스/스레드 정보 덤프
static uint32_t sys_dump_threads(void) {
    extern void dump_multitasking_info(void);
    dump_multitasking_info();
    return 0;
}


// ─────────────────────────────────────────────────────────────────────────────
// General Protection Fault 핸들러 (ISR 13)
// ─────────────────────────────────────────────────────────────────────────────
void gp_fault_handler(uint32_t error_code) {
    int user_fault = error_code & 0x4;
    kprintf_serial("[GP] General Protection Fault: error_code=0x%x user=%d\n",
                   error_code, user_fault ? 1 : 0);
    if (user_fault) {
        kprintf_serial("[GP] Terminating user process\n");
        __asm__ volatile(
            "mov $0x10, %%ax\n\t"
            "mov %%ax, %%ds\n\t"
            "mov %%ax, %%es\n\t"
            "mov %%ax, %%fs\n\t"
            "mov %%ax, %%gs\n\t"
            : : : "eax"
        );
        kprocess_exit();
        return;
    }
    kprintf_serial("[GP] Kernel GP fault — halting\n");
    __asm__ volatile("cli; hlt");
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 콜 디스패처
// ─────────────────────────────────────────────────────────────────────────────
uint32_t syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (eax) {
        case SYS_EXIT:    return sys_exit(ebx);
        case SYS_READ:    return sys_read(ebx, ecx, edx);
        case SYS_WRITE:   return sys_write(ebx, ecx, edx);
        case SYS_OPEN:    return sys_open(ebx, ecx, edx);
        case SYS_CLOSE:   return sys_close(ebx);
        case SYS_UNLINK:  return sys_unlink(ebx);
        case SYS_EXEC:    return sys_exec(ebx, ecx, edx);
        case SYS_LSEEK:   return sys_lseek(ebx, ecx, edx);
        case SYS_GETPID:  return sys_getpid();
        case SYS_MKDIR:   return sys_mkdir(ebx, ecx);
        case SYS_BRK:     return sys_brk(ebx);
        case SYS_STAT:    return sys_stat(ebx, ecx);
        case SYS_YIELD:   return sys_yield();
        case SYS_OPENDIR: return sys_opendir(ebx);
        case SYS_READDIR: return sys_readdir(ebx, ecx);
        case SYS_CLOSEDIR:return sys_closedir(ebx);
        case SYS_SOCKET:  return sys_socket(ebx, ecx, edx);
        case SYS_CONNECT: return sys_connect(ebx, ecx, edx);
        case SYS_SEND:    return sys_send(ebx, ecx, edx);
        case SYS_RECV:    return sys_recv(ebx, ecx, edx);
        case SYS_BIND:    return sys_bind(ebx, ecx, edx);
        case SYS_LISTEN:  return sys_listen(ebx, ecx);
        case SYS_ACCEPT:  return sys_accept(ebx, ecx, edx);
        case SYS_GETHOST: return sys_gethost(ebx, ecx);
        case SYS_CLEAR:        return sys_clear();
        case SYS_DUMP_HEAP:    return sys_dump_heap();
        case SYS_DUMP_THREADS: return sys_dump_threads();
        default:
            klog_warn("[SYSCALL] Unknown syscall: %d ebx=0x%x ecx=0x%x edx=0x%x\n",
                      (int)eax, ebx, ecx, edx);
            return (uint32_t)-38; // -ENOSYS
    }
}

// SYSENTER 커널 스택 (4KB, 정적 할당)
static uint8_t sysenter_stack[4096];

// wrmsr 헬퍼: MSR 레지스터에 32비트 값 쓰기
static inline void wrmsr(uint32_t msr, uint32_t val) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(val), "d"(0));
}

// syscall_init은 idt.c에서 int 0x80 핸들러를 등록한 뒤 호출됩니다.
void syscall_init(void) {
    // FD 테이블 초기화
    for (int i = 0; i < FD_TABLE_MAX; i++) {
        g_fd_table[i]  = NULL;
        g_fd_is_dir[i] = 0;
    }

    // SYSENTER MSR 설정
    // CS: 커널 코드 세그먼트 (0x08), sysexit 시 유저 CS = 0x08+16 = 0x18|3 = 0x1B
    wrmsr(MSR_SYSENTER_CS,  0x08);
    // EIP: sysenter 진입점
    wrmsr(MSR_SYSENTER_EIP, (uint32_t)sysenter_handler);
    // ESP: 커널 스택 최상단 (스택은 하향 성장)
    wrmsr(MSR_SYSENTER_ESP, (uint32_t)(sysenter_stack + sizeof(sysenter_stack)));

    klog_info("[SYSCALL] System call interface ready (int 0x80 + sysenter)\n");
}
