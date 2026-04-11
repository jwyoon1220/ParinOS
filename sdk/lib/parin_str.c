/*
 * sdk/lib/parin_str.c — ParinOS SDK: Extended String Utilities
 */

#include "parin/str.h"
#include "string.h"
#include "stdlib.h"

/* ── Predicates ─────────────────────────────────────────────────────────── */

int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

int str_ends_with(const char *s, const char *suffix) {
    size_t slen  = strlen(s);
    size_t sflen = strlen(suffix);
    if (sflen > slen) return 0;
    return memcmp(s + slen - sflen, suffix, sflen) == 0;
}

int str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* ── In-place transformation ────────────────────────────────────────────── */

char *str_to_upper(char *s) {
    for (char *p = s; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    }
    return s;
}

char *str_to_lower(char *s) {
    for (char *p = s; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }
    return s;
}

char *str_trim(char *s) {
    /* Skip leading whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;

    /* Find the end and strip trailing whitespace */
    if (*s) {
        char *end = s + strlen(s) - 1;
        while (end > s &&
               (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
            end--;
        }
        *(end + 1) = '\0';
    }
    return s;
}

/* ── Allocation helpers ─────────────────────────────────────────────────── */

char *str_dup(const char *s) {
    if (!s) return (char*)0;
    size_t len = strlen(s);
    char *copy = (char*)malloc(len + 1);
    if (!copy) return (char*)0;
    memcpy(copy, s, len + 1);
    return copy;
}

char *str_ndup(const char *s, size_t n) {
    if (!s) return (char*)0;
    size_t len = strlen(s);
    if (n < len) len = n;
    char *copy = (char*)malloc(len + 1);
    if (!copy) return (char*)0;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

/* ── Splitting ──────────────────────────────────────────────────────────── */

int str_split(const char *src, char delim,
              char **parts, int max_parts,
              char **buf_out) {
    if (!src || max_parts <= 0) return 0;

    /* Duplicate the source so we can modify it in-place */
    char *buf = str_dup(src);
    if (!buf) return -1;

    int count = 0;
    char *p = buf;

    while (count < max_parts) {
        parts[count++] = p;

        /* Scan forward for the next delimiter */
        while (*p && *p != delim) p++;
        if (!*p) break;  /* end of string */

        *p++ = '\0';     /* null-terminate this part, advance */
    }

    if (buf_out) *buf_out = buf;
    return count;
}

/* ── Search ─────────────────────────────────────────────────────────────── */

int str_count_char(const char *s, char c) {
    int n = 0;
    while (*s) {
        if (*s++ == c) n++;
    }
    return n;
}
