/*
 * sdk/include/parin/log.h — ParinOS SDK: Simple Logging Macros
 *
 * Header-only.  No extra object file required.
 * Output goes to stderr so it does not pollute stdout data streams.
 *
 * Log levels:
 *   LOG_DEBUG  — verbose, compiled out unless PARIN_LOG_LEVEL <= 0
 *   LOG_INFO   — general information   (PARIN_LOG_LEVEL <= 1, default)
 *   LOG_WARN   — unexpected conditions (PARIN_LOG_LEVEL <= 2)
 *   LOG_ERROR  — fatal / serious errors (always printed)
 *
 * Define PARIN_LOG_LEVEL before including this header to control verbosity:
 *   #define PARIN_LOG_LEVEL 0   // debug + info + warn + error
 *   #define PARIN_LOG_LEVEL 1   // info + warn + error  (default)
 *   #define PARIN_LOG_LEVEL 2   // warn + error
 *   #define PARIN_LOG_LEVEL 3   // error only
 */

#ifndef PARIN_LOG_H
#define PARIN_LOG_H

#include <stdio.h>

#ifndef PARIN_LOG_LEVEL
#define PARIN_LOG_LEVEL 1
#endif

/* ── Log macros ───────────────────────────────────────────────────────────── */

#if PARIN_LOG_LEVEL <= 0
#  define LOG_DEBUG(fmt, ...) \
       fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#  define LOG_DEBUG(fmt, ...)  ((void)0)
#endif

#if PARIN_LOG_LEVEL <= 1
#  define LOG_INFO(fmt, ...) \
       fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__)
#else
#  define LOG_INFO(fmt, ...)   ((void)0)
#endif

#if PARIN_LOG_LEVEL <= 2
#  define LOG_WARN(fmt, ...) \
       fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__)
#else
#  define LOG_WARN(fmt, ...)   ((void)0)
#endif

/* LOG_ERROR is always active */
#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

/* ── Assertion helper ─────────────────────────────────────────────────────── */

#define PARIN_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            LOG_ERROR("Assertion failed: %s  (file %s, line %d)", \
                      #cond, __FILE__, __LINE__); \
        } \
    } while (0)

#endif /* PARIN_LOG_H */
