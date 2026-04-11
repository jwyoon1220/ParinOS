/*
 * sdk/include/parin/fs.h — ParinOS SDK: Filesystem Utilities
 *
 * Higher-level filesystem helpers built on top of unistd.h and sys/stat.h.
 * Implemented in sdk/lib/parin_fs.c (compiled into libparin.a).
 */

#ifndef PARIN_FS_H
#define PARIN_FS_H

#include <stddef.h>

/* ── Existence / type tests ──────────────────────────────────────────────── */

/** Return 1 if path exists (file or directory), 0 otherwise. */
int fs_exists(const char *path);

/** Return 1 if path is a regular file, 0 otherwise. */
int fs_is_file(const char *path);

/** Return 1 if path is a directory, 0 otherwise. */
int fs_is_dir(const char *path);

/* ── File metadata ────────────────────────────────────────────────────────── */

/**
 * Return the size of the file at path in bytes, or -1 on error.
 * Directories always report 0.
 */
int fs_size(const char *path);

/* ── Reading ──────────────────────────────────────────────────────────────── */

/**
 * Read the entire contents of path into a malloc'd buffer.
 *
 * On success:
 *   - Returns a pointer to a null-terminated buffer containing the file data.
 *   - If out_size is not NULL, *out_size is set to the number of bytes read
 *     (not counting the trailing '\0').
 *   - Caller must free() the returned buffer.
 *
 * On error returns NULL.
 */
char *fs_read_all(const char *path, int *out_size);

/* ── Writing ──────────────────────────────────────────────────────────────── */

/**
 * Write size bytes from data to path, creating or truncating the file.
 *
 * Returns the number of bytes written on success, or -1 on error.
 */
int fs_write_all(const char *path, const void *data, int size);

/**
 * Append size bytes from data to path (creates the file if it does not exist).
 *
 * Returns the number of bytes written on success, or -1 on error.
 */
int fs_append(const char *path, const void *data, int size);

/* ── Copying / removing ───────────────────────────────────────────────────── */

/**
 * Copy the file at src to dst (creates or overwrites dst).
 *
 * Returns 0 on success, -1 on error.
 */
int fs_copy(const char *src, const char *dst);

/**
 * Remove the file at path.
 *
 * Returns 0 on success, -1 on error.
 */
int fs_remove(const char *path);

/* ── Directory creation ───────────────────────────────────────────────────── */

/**
 * Create a directory at path with the given mode (e.g. 0755).
 * Unlike mkdir(), this succeeds silently if path already exists as a directory.
 *
 * Returns 0 on success, -1 on error.
 */
int fs_mkdir(const char *path, int mode);

#endif /* PARIN_FS_H */
