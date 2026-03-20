//
// src/fs/vfs_fat.c — FAT32 → VFS 어댑터 구현
//

#include "vfs_fat.h"
#include "vfs.h"
#include "fat.h"
#include "../vga.h"
#include "../std/malloc.h"
#include "../std/string.h"
#include "../mem/mem.h"

/* 파일 핸들 래퍼: File + Dir 모두 담을 수 있는 공용체 */
typedef struct {
    bool    is_dir;
    File    file;
    Dir     dir;
} fat_handle_t;

/* ── 오퍼레이션 구현 ─────────────────────────────────────────────────────── */

static int fat_vfs_open(vfs_fs_t *fs, const char *path,
                        uint8_t flags, vfs_node_t *out) {
    Fat *fat = (Fat *)fs->priv;
    (void)fat;

    fat_handle_t *h = (fat_handle_t *)kmalloc(sizeof(fat_handle_t));
    if (!h) return VFS_ERR_NOMEM;
    memset(h, 0, sizeof(*h));

    /* VFS 플래그 → FAT 플래그 변환 */
    uint8_t fat_flags = 0;
    if (flags & VFS_O_READ)   fat_flags |= FAT_READ;
    if (flags & VFS_O_WRITE)  fat_flags |= FAT_WRITE;
    if (flags & VFS_O_APPEND) fat_flags |= FAT_APPEND;
    if (flags & VFS_O_TRUNC)  fat_flags |= FAT_TRUNC;
    if (flags & VFS_O_CREATE) fat_flags |= FAT_CREATE;

    if (fat_file_open(&h->file, path, fat_flags) != FAT_ERR_NONE) {
        kfree(h);
        return VFS_ERR_NOENT;
    }

    out->priv   = h;
    out->size   = h->file.size;
    out->offset = 0;
    out->attr   = VFS_ATTR_FILE;
    return VFS_OK;
}

static int fat_vfs_close(vfs_node_t *node) {
    fat_handle_t *h = (fat_handle_t *)node->priv;
    if (!h) return VFS_OK;
    if (!h->is_dir) fat_file_close(&h->file);
    kfree(h);
    node->priv = NULL;
    return VFS_OK;
}

static int fat_vfs_read(vfs_node_t *node, void *buf, uint32_t len,
                        uint32_t *bytes_read) {
    fat_handle_t *h = (fat_handle_t *)node->priv;
    if (!h || h->is_dir) return VFS_ERR_INVAL;

    int br = 0;
    int err = fat_file_read(&h->file, buf, (int)len, &br);
    if (err != FAT_ERR_NONE && err != FAT_ERR_EOF) return VFS_ERR_IO;
    if (bytes_read) *bytes_read = (uint32_t)br;
    node->offset += (uint32_t)br;
    return VFS_OK;
}

static int fat_vfs_write(vfs_node_t *node, const void *buf, uint32_t len,
                         uint32_t *bytes_written) {
    fat_handle_t *h = (fat_handle_t *)node->priv;
    if (!h || h->is_dir) return VFS_ERR_INVAL;

    int bw = 0;
    int err = fat_file_write(&h->file, buf, (int)len, &bw);
    if (err != FAT_ERR_NONE) return VFS_ERR_IO;
    if (bytes_written) *bytes_written = (uint32_t)bw;
    node->offset += (uint32_t)bw;
    return VFS_OK;
}

static int fat_vfs_seek(vfs_node_t *node, int32_t offset, int whence) {
    fat_handle_t *h = (fat_handle_t *)node->priv;
    if (!h || h->is_dir) return VFS_ERR_INVAL;

    int fat_whence;
    switch (whence) {
        case VFS_SEEK_SET: fat_whence = FAT_SEEK_START; break;
        case VFS_SEEK_CUR: fat_whence = FAT_SEEK_CURR;  break;
        case VFS_SEEK_END: fat_whence = FAT_SEEK_END;   break;
        default: return VFS_ERR_INVAL;
    }
    int err = fat_file_seek(&h->file, offset, fat_whence);
    if (err != FAT_ERR_NONE) return VFS_ERR_IO;
    node->offset = h->file.offset;
    return VFS_OK;
}

static int fat_vfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *out) {
    (void)fs;
    DirInfo info;
    if (fat_stat(path, &info) != FAT_ERR_NONE) return VFS_ERR_NOENT;

    int nlen = info.name_len < 255 ? info.name_len : 254;
    int k;
    for (k = 0; k < nlen; k++) out->name[k] = info.name[k];
    out->name[k] = '\0';
    out->size     = info.size;
    out->attr     = (info.attr & FAT_ATTR_DIR) ? VFS_ATTR_DIR : VFS_ATTR_FILE;
    if (info.attr & FAT_ATTR_RO)     out->attr |= VFS_ATTR_RO;
    if (info.attr & FAT_ATTR_HIDDEN) out->attr |= VFS_ATTR_HIDDEN;
    return VFS_OK;
}

static int fat_vfs_opendir(vfs_fs_t *fs, const char *path, vfs_node_t *out) {
    (void)fs;
    fat_handle_t *h = (fat_handle_t *)kmalloc(sizeof(fat_handle_t));
    if (!h) return VFS_ERR_NOMEM;
    memset(h, 0, sizeof(*h));
    h->is_dir = true;

    if (fat_dir_open(&h->dir, path) != FAT_ERR_NONE) {
        kfree(h);
        return VFS_ERR_NOENT;
    }
    out->priv = h;
    out->attr = VFS_ATTR_DIR;
    return VFS_OK;
}

static int fat_vfs_readdir(vfs_node_t *node, vfs_stat_t *entry) {
    fat_handle_t *h = (fat_handle_t *)node->priv;
    if (!h || !h->is_dir) return VFS_ERR_INVAL;

    DirInfo info;
    int err = fat_dir_read(&h->dir, &info);
    if (err == FAT_ERR_EOF)        return VFS_ERR_NOENT;
    if (err != FAT_ERR_NONE)       return VFS_ERR_IO;

    int nlen = info.name_len < 255 ? info.name_len : 254;
    int k;
    for (k = 0; k < nlen; k++) entry->name[k] = info.name[k];
    entry->name[k] = '\0';
    entry->size     = info.size;
    entry->attr     = (info.attr & FAT_ATTR_DIR) ? VFS_ATTR_DIR : VFS_ATTR_FILE;

    fat_dir_next(&h->dir);
    return VFS_OK;
}

static int fat_vfs_unlink(vfs_fs_t *fs, const char *path) {
    (void)fs;
    if (fat_unlink(path) != FAT_ERR_NONE) return VFS_ERR_IO;
    return VFS_OK;
}

static int fat_vfs_create(vfs_fs_t *fs, const char *path) {
    (void)fs;
    File f;
    if (fat_file_open(&f, path, FAT_CREATE | FAT_WRITE) != FAT_ERR_NONE)
        return VFS_ERR_IO;
    fat_file_close(&f);
    return VFS_OK;
}

static int fat_vfs_mkdir(vfs_fs_t *fs, const char *path) {
    (void)fs;
    Dir d;
    if (fat_dir_create(&d, path) != FAT_ERR_NONE) return VFS_ERR_IO;
    return VFS_OK;
}

/* ── 오퍼레이션 테이블 ────────────────────────────────────────────────────── */
static vfs_ops_t g_fat_ops = {
    .open    = fat_vfs_open,
    .close   = fat_vfs_close,
    .read    = fat_vfs_read,
    .write   = fat_vfs_write,
    .seek    = fat_vfs_seek,
    .stat    = fat_vfs_stat,
    .opendir = fat_vfs_opendir,
    .readdir = fat_vfs_readdir,
    .unlink  = fat_vfs_unlink,
    .create  = fat_vfs_create,
    .mkdir   = fat_vfs_mkdir,
};

/* ── FAT32 파일시스템 레코드 풀 ────────────────────────────────────────────── */
#define VFS_FAT_MAX  4
static vfs_fs_t g_fat_fs_pool[VFS_FAT_MAX];
static int      g_fat_fs_count = 0;

/* ── 공개 함수 ───────────────────────────────────────────────────────────── */
int vfs_fat_register(Fat *fat, const char *mp) {
    if (g_fat_fs_count >= VFS_FAT_MAX) return VFS_ERR_NOMEM;

    vfs_fs_t *fs = &g_fat_fs_pool[g_fat_fs_count++];
    memset(fs, 0, sizeof(*fs));
    fs->name = "fat32";
    fs->ops  = &g_fat_ops;
    fs->priv = fat;

    return vfs_mount(fs, mp);
}
