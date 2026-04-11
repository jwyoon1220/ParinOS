/*
 * sdk/include/parin/str.h — ParinOS SDK: Extended String Utilities
 *
 * Higher-level string helpers on top of the basic string.h functions.
 * Implemented in sdk/lib/parin_str.c (compiled into libparin.a).
 */

#ifndef PARIN_STR_H
#define PARIN_STR_H

#include <stddef.h>

/* ── Predicates ───────────────────────────────────────────────────────────── */

/** Return 1 if s starts with prefix, 0 otherwise. */
int str_starts_with(const char *s, const char *prefix);

/** Return 1 if s ends with suffix, 0 otherwise. */
int str_ends_with(const char *s, const char *suffix);

/** Return 1 if the two strings are equal, 0 otherwise. */
int str_eq(const char *a, const char *b);

/* ── Transformation (in-place) ────────────────────────────────────────────── */

/** Convert s to upper-case in-place.  Returns s. */
char *str_to_upper(char *s);

/** Convert s to lower-case in-place.  Returns s. */
char *str_to_lower(char *s);

/**
 * Trim leading and trailing whitespace in-place.
 * Returns a pointer to the first non-whitespace character inside s
 * (the original buffer is modified to null-terminate the trimmed string).
 */
char *str_trim(char *s);

/* ── Allocation helpers ───────────────────────────────────────────────────── */

/**
 * Duplicate s into a malloc'd buffer.
 * Caller must free() the returned pointer when done.
 * Returns NULL on allocation failure.
 */
char *str_dup(const char *s);

/**
 * Duplicate at most n characters of s.
 * The returned string is always null-terminated.
 * Caller must free() the returned pointer when done.
 */
char *str_ndup(const char *s, size_t n);

/* ── Splitting ────────────────────────────────────────────────────────────── */

/**
 * Split src by single-character delimiter delim into parts[].
 *
 * Each element of parts[] will point into a malloc'd copy of src
 * (returned in *buf_out — caller must free() it when done).
 *
 * Returns the number of parts stored (capped at max_parts).
 * On allocation failure returns -1.
 *
 * Example:
 *   char *buf;
 *   char *parts[8];
 *   int n = str_split("/bin/ls", '/', parts, 8, &buf);
 *   // n=3: parts={"","bin","ls"}
 *   free(buf);
 */
int str_split(const char *src, char delim,
              char **parts, int max_parts,
              char **buf_out);

/* ── Search ───────────────────────────────────────────────────────────────── */

/** Return the number of occurrences of character c in s. */
int str_count_char(const char *s, char c);

#endif /* PARIN_STR_H */
