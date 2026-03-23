//
// src/fs/vfs.c — Virtual File System 구현
//

#include "vfs.h"
#include "../hal/vga.h"
#include "../mem/mem.h"
#include "../std/malloc.h"
#include "../std/kstring.h"

/* ── 마운트 테이블 ────────────────────────────────────────────────────────── */
#define VFS_MAX_MOUNTS  8

static vfs_fs_t *g_mounts[VFS_MAX_MOUNTS];
static int       g_mount_count = 0;

/* ── 노드 풀 (정적) ────────────────────────────────────────────────────────── */
static vfs_node_t g_node_pool[VFS_MAX_NODES];

/* ─────────────────────────────────────────────────────────────────────────────
 * 내부 헬퍼
 * ───────────────────────────────────────────────────────────────────────────*/

/** 빈 노드를 풀에서 할당 */
static vfs_node_t *alloc_node(void) {
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (!g_node_pool[i].in_use) {
            g_node_pool[i].in_use = true;
            return &g_node_pool[i];
        }
    }
    return NULL;
}

/** 노드를 풀로 반환 */
static void free_node(vfs_node_t *node) {
    if (node) {
        node->in_use = false;
        node->fs     = NULL;
        node->priv   = NULL;
    }
}

/**
 * 경로를 처리할 파일시스템을 마운트 테이블에서 찾습니다.
 * 가장 길게 일치하는 마운트 포인트를 선택합니다.
 */
static vfs_fs_t *resolve_fs(const char *path) {
    vfs_fs_t *best    = NULL;
    int       best_len = -1;

    for (int i = 0; i < g_mount_count; i++) {
        vfs_fs_t *fs = g_mounts[i];
        if (!fs || !fs->active) continue;

        int mplen = strlen(fs->mount_point);
        if (strncmp(path, fs->mount_point, mplen) == 0) {
            if (mplen > best_len) {
                best     = fs;
                best_len = mplen;
            }
        }
    }
    return best;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * VFS 공개 API
 * ───────────────────────────────────────────────────────────────────────────*/

void vfs_init(void) {
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;
    memset(g_node_pool, 0, sizeof(g_node_pool));
    klog_info("[VFS] Virtual File System initialized\n");
}

int vfs_mount(vfs_fs_t *fs, const char *mp) {
    if (!fs || !mp) return VFS_ERR_INVAL;
    if (g_mount_count >= VFS_MAX_MOUNTS) return VFS_ERR_NOMEM;

    /* 마운트 포인트 설정 */
    int mplen = strlen(mp);
    if (mplen >= 63) return VFS_ERR_INVAL;
    int k;
    for (k = 0; k < mplen; k++) fs->mount_point[k] = mp[k];
    fs->mount_point[k] = '\0';
    fs->active = true;

    g_mounts[g_mount_count++] = fs;
    klog_ok("[VFS] Mounted '%s' at '%s'\n", fs->name, mp);
    return VFS_OK;
}

int vfs_umount(const char *mp) {
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i] && strcmp(g_mounts[i]->mount_point, mp) == 0) {
            g_mounts[i]->active = false;
            g_mounts[i] = NULL;
            return VFS_OK;
        }
    }
    return VFS_ERR_NOENT;
}

int vfs_open(const char *path, uint8_t flags, vfs_node_t **out) {
    if (!path || !out) return VFS_ERR_INVAL;

    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->open) return VFS_ERR_NOSYS;

    vfs_node_t *node = alloc_node();
    if (!node) return VFS_ERR_NOMEM;

    node->fs     = fs;
    node->offset = 0;

    int err = fs->ops->open(fs, path, flags, node);
    if (err != VFS_OK) {
        free_node(node);
        return err;
    }

    *out = node;
    return VFS_OK;
}

int vfs_close(vfs_node_t *node) {
    if (!node || !node->in_use) return VFS_ERR_INVAL;
    if (node->fs && node->fs->ops && node->fs->ops->close) {
        node->fs->ops->close(node);
    }
    free_node(node);
    return VFS_OK;
}

int vfs_read(vfs_node_t *node, void *buf, uint32_t len, uint32_t *bytes_read) {
    if (!node || !buf) return VFS_ERR_INVAL;
    if (!node->fs || !node->fs->ops || !node->fs->ops->read) return VFS_ERR_NOSYS;
    return node->fs->ops->read(node, buf, len, bytes_read);
}

int vfs_write(vfs_node_t *node, const void *buf, uint32_t len, uint32_t *bytes_written) {
    if (!node || !buf) return VFS_ERR_INVAL;
    if (!node->fs || !node->fs->ops || !node->fs->ops->write) return VFS_ERR_NOSYS;
    return node->fs->ops->write(node, buf, len, bytes_written);
}

int vfs_seek(vfs_node_t *node, int32_t offset, int whence) {
    if (!node) return VFS_ERR_INVAL;
    if (node->fs && node->fs->ops && node->fs->ops->seek)
        return node->fs->ops->seek(node, offset, whence);
    /* 기본 구현 */
    switch (whence) {
        case VFS_SEEK_SET:
            if (offset < 0) return VFS_ERR_INVAL;
            node->offset = (uint32_t)offset;
            break;
        case VFS_SEEK_CUR:
            if (offset < 0 && (uint32_t)(-offset) > node->offset)
                node->offset = 0;
            else
                node->offset = (uint32_t)((int32_t)node->offset + offset);
            break;
        case VFS_SEEK_END:
            node->offset = node->size + (uint32_t)offset;
            break;
        default: return VFS_ERR_INVAL;
    }
    return VFS_OK;
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->stat) return VFS_ERR_NOSYS;
    return fs->ops->stat(fs, path, out);
}

int vfs_unlink(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->unlink) return VFS_ERR_NOSYS;
    return fs->ops->unlink(fs, path);
}

int vfs_create(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->create) return VFS_ERR_NOSYS;
    return fs->ops->create(fs, path);
}

int vfs_mkdir(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->mkdir) return VFS_ERR_NOSYS;
    return fs->ops->mkdir(fs, path);
}

int vfs_opendir(const char *path, vfs_node_t **dirnode) {
    if (!path || !dirnode) return VFS_ERR_INVAL;
    vfs_fs_t *fs = resolve_fs(path);
    if (!fs) return VFS_ERR_NOFS;
    if (!fs->ops || !fs->ops->opendir) return VFS_ERR_NOSYS;

    vfs_node_t *node = alloc_node();
    if (!node) return VFS_ERR_NOMEM;

    node->fs   = fs;
    node->attr = VFS_ATTR_DIR;

    int err = fs->ops->opendir(fs, path, node);
    if (err != VFS_OK) {
        free_node(node);
        return err;
    }
    *dirnode = node;
    return VFS_OK;
}

int vfs_readdir(vfs_node_t *dirnode, vfs_stat_t *entry) {
    if (!dirnode || !entry) return VFS_ERR_INVAL;
    if (!dirnode->fs || !dirnode->fs->ops || !dirnode->fs->ops->readdir)
        return VFS_ERR_NOSYS;
    return dirnode->fs->ops->readdir(dirnode, entry);
}

int vfs_closedir(vfs_node_t *dirnode) {
    return vfs_close(dirnode);
}

const char *vfs_strerror(int err) {
    switch (err) {
        case VFS_OK:          return "OK";
        case VFS_ERR_NOENT:   return "No such file or directory";
        case VFS_ERR_IO:      return "I/O error";
        case VFS_ERR_INVAL:   return "Invalid argument";
        case VFS_ERR_NOSYS:   return "Not implemented";
        case VFS_ERR_NOMEM:   return "Out of memory";
        case VFS_ERR_NOFS:    return "No filesystem mounted";
        case VFS_ERR_DENIED:  return "Permission denied";
        case VFS_ERR_EXIST:   return "File exists";
        case VFS_ERR_NOTDIR:  return "Not a directory";
        case VFS_ERR_ISDIR:   return "Is a directory";
        case VFS_ERR_FULL:    return "No space left";
        default:              return "Unknown error";
    }
}
