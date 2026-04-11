/*
 * sdk/include/parin.h — ParinOS SDK Umbrella Header
 *
 * Include this single header to get the full ParinOS SDK API:
 *
 *   #include <parin.h>
 *
 * Or include individual headers as needed for smaller compile units.
 */

#ifndef PARIN_H
#define PARIN_H

/* ── Standard C-like interfaces ──────────────────────────────────────────── */
#include "syscall.h"    /* Raw sysenter stubs + syscall numbers              */
#include "unistd.h"     /* open/read/write/close/lseek/getpid/exec/…         */
#include "stdio.h"      /* FILE*, printf/scanf families, fgets, fread, …     */
#include "stdlib.h"     /* malloc/free/exit/atoi/qsort/bsearch/…             */
#include "string.h"     /* strlen/strcpy/memset/memcmp/…                     */
#include "ctype.h"      /* isdigit/isalpha/isspace/toupper/tolower/…         */
#include "dirent.h"     /* opendir_fd/readdir_r/closedir_fd                  */
#include "sys/stat.h"   /* stat() / S_ISREG / S_ISDIR                        */

/* ── Developer-friendly extensions ──────────────────────────────────────── */
#include "parin/math.h"  /* clamp, round_up/down, is_pow2, popcount, …      */
#include "parin/log.h"   /* LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR      */
#include "parin/str.h"   /* str_dup, str_trim, str_split, str_starts_with, … */
#include "parin/fs.h"    /* fs_exists, fs_read_all, fs_write_all, fs_copy, … */
#include "parin/io.h"    /* io_write_all, io_read_all, io_read_line, …       */
#include "parin/args.h"  /* args_has_flag, args_get_str, args_get_int, …     */

#endif /* PARIN_H */
