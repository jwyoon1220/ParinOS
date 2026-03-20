//
// src/fs/vfs.h — Virtual File System 추상화 계층
//
// 파일시스템 종류(FAT32, ext2 등)에 관계없이 동일한 인터페이스로
// 파일과 디렉터리를 조작할 수 있습니다.
//

#ifndef PARINOS_VFS_H
#define PARINOS_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── 에러 코드 ────────────────────────────────────────────────────────────── */
typedef enum {
    VFS_OK          =  0,
    VFS_ERR_NOENT   = -1,   /* 파일/디렉터리 없음 */
    VFS_ERR_IO      = -2,   /* I/O 오류 */
    VFS_ERR_INVAL   = -3,   /* 잘못된 인수 */
    VFS_ERR_NOSYS   = -4,   /* 미구현 */
    VFS_ERR_NOMEM   = -5,   /* 메모리 부족 */
    VFS_ERR_NOFS    = -6,   /* 마운트된 파일시스템 없음 */
    VFS_ERR_DENIED  = -7,   /* 접근 거부 */
    VFS_ERR_EXIST   = -8,   /* 이미 존재 */
    VFS_ERR_NOTDIR  = -9,   /* 디렉터리가 아님 */
    VFS_ERR_ISDIR   = -10,  /* 디렉터리 */
    VFS_ERR_FULL    = -11,  /* 디스크 꽉 참 */
} vfs_err_t;

/* ── 열기 플래그 ──────────────────────────────────────────────────────────── */
#define VFS_O_READ    0x01
#define VFS_O_WRITE   0x02
#define VFS_O_APPEND  0x04
#define VFS_O_TRUNC   0x08
#define VFS_O_CREATE  0x10

/* ── 파일 속성 ────────────────────────────────────────────────────────────── */
#define VFS_ATTR_FILE  0x01
#define VFS_ATTR_DIR   0x02
#define VFS_ATTR_RO    0x04
#define VFS_ATTR_HIDDEN 0x08

/* ── 선형 탐색 상수 ───────────────────────────────────────────────────────── */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

/* ── VFS 파일 정보 ────────────────────────────────────────────────────────── */
typedef struct {
    char     name[256];   /* 파일/디렉터리 이름 */
    uint32_t size;        /* 파일 크기 (바이트) */
    uint8_t  attr;        /* VFS_ATTR_* 비트 조합 */
} vfs_stat_t;

/* ════════════════════════════════════════════════════════════════════════════
 * VFS 노드 (파일 또는 디렉터리를 추상화하는 핸들)
 * ════════════════════════════════════════════════════════════════════════════ */
#define VFS_MAX_NODES  32

typedef struct vfs_node vfs_node_t;
typedef struct vfs_fs   vfs_fs_t;

/* ── 파일시스템 드라이버 오퍼레이션 테이블 ────────────────────────────────── */
typedef struct {
    /** 파일 열기. 성공 시 VFS_OK, 실패 시 음수 에러 코드. */
    int (*open)(vfs_fs_t *fs, const char *path, uint8_t flags, vfs_node_t *out);

    /** 파일 닫기 */
    int (*close)(vfs_node_t *node);

    /** 파일 읽기. *bytes_read 에 실제 읽은 바이트 수 저장. */
    int (*read)(vfs_node_t *node, void *buf, uint32_t len, uint32_t *bytes_read);

    /** 파일 쓰기 */
    int (*write)(vfs_node_t *node, const void *buf, uint32_t len, uint32_t *bytes_written);

    /** 파일 포인터 이동. whence = VFS_SEEK_* */
    int (*seek)(vfs_node_t *node, int32_t offset, int whence);

    /** 파일 정보 조회 */
    int (*stat)(vfs_fs_t *fs, const char *path, vfs_stat_t *out);

    /** 디렉터리 열기 (dirnode에 저장) */
    int (*opendir)(vfs_fs_t *fs, const char *path, vfs_node_t *dirnode);

    /** 다음 디렉터리 엔트리 읽기. VFS_ERR_NOENT 이면 끝. */
    int (*readdir)(vfs_node_t *dirnode, vfs_stat_t *entry);

    /** 파일 삭제 */
    int (*unlink)(vfs_fs_t *fs, const char *path);

    /** 파일 생성 (이미 있으면 VFS_ERR_EXIST) */
    int (*create)(vfs_fs_t *fs, const char *path);

    /** 디렉터리 생성 */
    int (*mkdir)(vfs_fs_t *fs, const char *path);
} vfs_ops_t;

/* ── 파일시스템 인스턴스 ─────────────────────────────────────────────────── */
struct vfs_fs {
    const char  *name;        /* 드라이버 이름 (예: "fat32", "ext2") */
    char         mount_point[64]; /* 마운트 포인트 (예: "/") */
    vfs_ops_t   *ops;         /* 오퍼레이션 테이블 */
    void        *priv;        /* 드라이버 전용 데이터 */
    bool         active;
};

/* ── VFS 노드 ─────────────────────────────────────────────────────────────── */
struct vfs_node {
    vfs_fs_t *fs;       /* 소속 파일시스템 */
    void     *priv;     /* 드라이버 전용 파일 핸들 */
    uint32_t  size;     /* 파일 크기 */
    uint32_t  offset;   /* 현재 읽기/쓰기 위치 */
    uint8_t   attr;
    bool      in_use;
};

/* ════════════════════════════════════════════════════════════════════════════
 * VFS 공개 API
 * ════════════════════════════════════════════════════════════════════════════ */

/** VFS 초기화 (커널 시작 시 1회 호출) */
void vfs_init(void);

/**
 * 파일시스템 마운트.
 * @param fs  초기화된 vfs_fs_t 구조체 포인터 (정적 또는 동적 할당)
 * @param mp  마운트 포인트 (예: "/", "/mnt/ext2")
 */
int vfs_mount(vfs_fs_t *fs, const char *mp);

/** 파일시스템 언마운트 */
int vfs_umount(const char *mp);

/* ── 파일 조작 ──────────────────────────────────────────────────────────── */
int vfs_open  (const char *path, uint8_t flags, vfs_node_t **out);
int vfs_close (vfs_node_t *node);
int vfs_read  (vfs_node_t *node, void *buf, uint32_t len, uint32_t *bytes_read);
int vfs_write (vfs_node_t *node, const void *buf, uint32_t len, uint32_t *bytes_written);
int vfs_seek  (vfs_node_t *node, int32_t offset, int whence);
int vfs_stat  (const char *path, vfs_stat_t *out);
int vfs_unlink(const char *path);
int vfs_create(const char *path);
int vfs_mkdir (const char *path);

/* ── 디렉터리 ───────────────────────────────────────────────────────────── */
int vfs_opendir (const char *path, vfs_node_t **dirnode);
int vfs_readdir (vfs_node_t *dirnode, vfs_stat_t *entry);
int vfs_closedir(vfs_node_t *dirnode);

/** 에러 코드를 문자열로 변환 */
const char *vfs_strerror(int err);

#endif /* PARINOS_VFS_H */
