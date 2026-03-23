//
// src/user/user.c
// 유저 모드 시스템 콜 디스패처 및 syscall 구현
//

#include "user.h"
#include "../hal/vga.h"
#include "../kernel/multitasking.h"
#include "../fs/vfs.h"
#include "../elf/elf.h"
#include "../std/kstdio.h"

// ─────────────────────────────────────────────────────────────────────────────
// 파일 디스크립터 테이블
// FD 0 = stdin (keyboard), FD 1 = stdout (VGA), FD 2 = stderr (VGA)
// FD 3..FD_TABLE_MAX-1 = VFS 파일
// ─────────────────────────────────────────────────────────────────────────────
#define FD_TABLE_MAX  16
#define FD_FILE_MIN   3

static vfs_node_t* g_fd_table[FD_TABLE_MAX];

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
        // stdout / stderr → VGA에 출력
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

// SYS_BRK(45): 스텁 — 유저 힙 확장 (현재는 -ENOMEM 반환)
static uint32_t sys_brk(uint32_t addr) {
    (void)addr;
    return (uint32_t)-12; // -ENOMEM (stub)
}

// SYS_EXEC(11): ELF 바이너리 실행 (경로, argc, argv)
static uint32_t sys_exec(uint32_t path_vaddr, uint32_t argc, uint32_t argv_vaddr) {
    const char* path = (const char*)path_vaddr;
    const char** argv = (const char**)argv_vaddr;
    int ret = elf_execute_with_args(path, (int)argc, argv);
    return (uint32_t)ret;
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 콜 디스패처
// ─────────────────────────────────────────────────────────────────────────────
uint32_t syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (eax) {
        case SYS_EXIT:   return sys_exit(ebx);
        case SYS_EXEC:   return sys_exec(ebx, ecx, edx);
        case SYS_READ:   return sys_read(ebx, ecx, edx);
        case SYS_WRITE:  return sys_write(ebx, ecx, edx);
        case SYS_OPEN:   return sys_open(ebx, ecx, edx);
        case SYS_CLOSE:  return sys_close(ebx);
        case SYS_LSEEK:  return sys_lseek(ebx, ecx, edx);
        case SYS_GETPID: return sys_getpid();
        case SYS_BRK:    return sys_brk(ebx);
        case SYS_YIELD:  return sys_yield();
        default:
            klog_warn("[SYSCALL] Unknown syscall: %d\n", (int)eax);
            return (uint32_t)-38; // -ENOSYS
    }
}

// syscall_init은 idt.c에서 int 0x80 핸들러를 등록한 뒤 호출됩니다.
void syscall_init(void) {
    // FD 테이블 초기화
    for (int i = 0; i < FD_TABLE_MAX; i++) {
        g_fd_table[i] = NULL;
    }
    klog_info("[SYSCALL] System call interface ready (int 0x80)\n");
}
