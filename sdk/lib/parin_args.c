/*
 * sdk/lib/parin_args.c — ParinOS SDK: Command-Line Argument Parsing
 */

#include "parin/args.h"
#include "string.h"
#include "stdlib.h"

/* ── Flag testing ───────────────────────────────────────────────────────── */

int args_has_flag(int argc, const char **argv, const char *flag) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return 1;
    }
    return 0;
}

/* ── Value retrieval ────────────────────────────────────────────────────── */

const char *args_get_str(int argc, const char **argv, const char *flag) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return (const char*)0;
}

int args_get_int(int argc, const char **argv, const char *flag,
                 int default_val) {
    const char *val = args_get_str(argc, argv, flag);
    if (!val) return default_val;
    return atoi(val);
}

/* ── Positional arguments ───────────────────────────────────────────────── */

int args_positional(int argc, const char **argv,
                    const char **positional, int max,
                    const char **skip_flags) {
    int count = 0;
    int i = 1;
    while (i < argc && count < max) {
        const char *arg = argv[i];

        /* Skip this entry if the previous entry was a value-flag */
        /* (handled by advancing i below when we detect a skip_flag) */

        if (arg[0] == '-') {
            /* Check if this flag consumes the next argument */
            if (skip_flags) {
                int skip_next = 0;
                for (int k = 0; skip_flags[k]; k++) {
                    if (strcmp(arg, skip_flags[k]) == 0) {
                        skip_next = 1;
                        break;
                    }
                }
                if (skip_next) {
                    i += 2; /* skip flag + its value */
                    continue;
                }
            }
            i++;
            continue; /* boolean flag, just skip */
        }

        positional[count++] = arg;
        i++;
    }
    return count;
}
