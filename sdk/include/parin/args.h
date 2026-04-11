/*
 * sdk/include/parin/args.h — ParinOS SDK: Command-Line Argument Parsing
 *
 * Simple helpers for programs that accept flags like:
 *   myprogram -v --output /tmp/out.txt --count 42 file.txt
 *
 * Implemented in sdk/lib/parin_args.c (compiled into libparin.a).
 */

#ifndef PARIN_ARGS_H
#define PARIN_ARGS_H

/* ── Flag testing ─────────────────────────────────────────────────────────── */

/**
 * Return 1 if flag appears anywhere in argv[1..argc-1], 0 otherwise.
 *
 * Example:
 *   if (args_has_flag(argc, argv, "-v")) { ... }
 */
int args_has_flag(int argc, const char **argv, const char *flag);

/* ── Value retrieval ──────────────────────────────────────────────────────── */

/**
 * Find flag in argv and return the next argument as a string.
 *
 * Example:
 *   const char *out = args_get_str(argc, argv, "--output");
 *   // argv: --output /tmp/out.txt  →  returns "/tmp/out.txt"
 *
 * Returns NULL if flag is not found or has no following argument.
 */
const char *args_get_str(int argc, const char **argv, const char *flag);

/**
 * Like args_get_str() but converts the value to an integer via atoi().
 *
 * Returns default_val if flag is not found or its value is not numeric.
 */
int args_get_int(int argc, const char **argv, const char *flag,
                 int default_val);

/* ── Positional arguments ─────────────────────────────────────────────────── */

/**
 * Collect all argv entries that do not start with '-' into positional[].
 *
 * Returns the number of positional arguments stored (capped at max).
 * Skips the values that immediately follow known flags listed in
 * skip_flags[] (NULL-terminated array), so that "--output file.txt" does
 * not add "file.txt" as a positional argument.
 *
 * Pass skip_flags=NULL if you have no value-flags to skip.
 *
 * Example:
 *   const char *pos[8];
 *   const char *value_flags[] = { "--output", "--count", NULL };
 *   int n = args_positional(argc, argv, pos, 8, value_flags);
 */
int args_positional(int argc, const char **argv,
                    const char **positional, int max,
                    const char **skip_flags);

#endif /* PARIN_ARGS_H */
