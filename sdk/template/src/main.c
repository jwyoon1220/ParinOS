/*
 * template/src/main.c — ParinOS SDK Program Template
 *
 * Demonstrates the full ParinOS SDK API surface:
 *   - printf / stdio
 *   - Filesystem helpers  (parin/fs.h)
 *   - String utilities    (parin/str.h)
 *   - I/O utilities       (parin/io.h)
 *   - Argument parsing    (parin/args.h)
 *   - Math utilities      (parin/math.h)
 *   - Logging macros      (parin/log.h)
 *
 * Build:  make -C template
 * Run:    /bin/myprogram [--name <str>] [--count <n>] [-v] [files...]
 */

/* Enable debug-level logging for this program — must come before parin.h */
#define PARIN_LOG_LEVEL 0

#include <parin.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void demo_math(void) {
    puts("── Math utilities ─────────────────────────────");
    printf("  clamp(150, 0, 100)       = %d\n", parin_clamp(150, 0, 100));
    printf("  round_up(13, 8)          = %u\n", parin_round_up(13, 8));
    printf("  round_down(13, 8)        = %u\n", parin_round_down(13, 8));
    printf("  is_pow2(64)              = %d\n", parin_is_pow2(64));
    printf("  is_pow2(100)             = %d\n", parin_is_pow2(100));
    printf("  popcount(0xFF)           = %d\n", parin_popcount(0xFF));
    printf("  highest_bit(0x80)        = %d\n", parin_highest_bit(0x80));
}

static void demo_str(void) {
    puts("── String utilities ───────────────────────────");

    char s[] = "  Hello, ParinOS!  ";
    printf("  Original : \"%s\"\n", s);
    printf("  Trimmed  : \"%s\"\n", str_trim(s));

    char upper[] = "hello world";
    printf("  Upper    : \"%s\"\n", str_to_upper(upper));

    char lower[] = "HELLO WORLD";
    printf("  Lower    : \"%s\"\n", str_to_lower(lower));

    printf("  starts_with(\"hello\",\"hel\") = %d\n",
           str_starts_with("hello", "hel"));
    printf("  ends_with(\"hello\",\"llo\")   = %d\n",
           str_ends_with("hello", "llo"));

    /* Splitting */
    char *buf;
    char *parts[8];
    int n = str_split("/usr/bin/ls", '/', parts, 8, &buf);
    printf("  split(\"/usr/bin/ls\", '/') → %d parts:", n);
    for (int i = 0; i < n; i++) printf(" \"%s\"", parts[i]);
    puts("");
    free(buf);

    /* Duplication */
    char *dup = str_dup("ParinOS");
    printf("  str_dup  : \"%s\"\n", dup);
    free(dup);
}

static void demo_fs(void) {
    puts("── Filesystem helpers ─────────────────────────");

    printf("  fs_exists(\"/bin\")   = %d\n", fs_exists("/bin"));
    printf("  fs_is_dir(\"/bin\")   = %d\n", fs_is_dir("/bin"));
    printf("  fs_is_file(\"/bin\")  = %d\n", fs_is_file("/bin"));

    /* Write a temp file, read it back, then remove it */
    const char *tmpfile = "/tmp/sdk_test.txt";
    const char *content = "Hello from ParinOS SDK!\n";

    if (fs_write_all(tmpfile, content, (int)strlen(content)) > 0) {
        LOG_INFO("Wrote %d bytes to %s", (int)strlen(content), tmpfile);

        int sz;
        char *data = fs_read_all(tmpfile, &sz);
        if (data) {
            printf("  Read back (%d bytes): %s", sz, data);
            free(data);
        }
        fs_remove(tmpfile);
        LOG_DEBUG("Removed %s", tmpfile);
    } else {
        LOG_WARN("Could not write to %s (tmpfs may not exist)", tmpfile);
    }

    /* List /bin */
    printf("  /bin entries:\n");
    int dfd = opendir_fd("/bin");
    if (dfd >= 0) {
        struct dirent ent;
        int cnt = 0;
        while (readdir_r(dfd, &ent) == 0 && cnt < 5) {
            char type = (ent.d_type == DT_DIR) ? 'd' : '-';
            printf("    %c  %-16s  %u B\n", type, ent.d_name, ent.d_size);
            cnt++;
        }
        closedir_fd(dfd);
    }
}

static void demo_io(void) {
    puts("── I/O utilities ──────────────────────────────");
    io_println(STDOUT_FILENO, "  io_println: Hello via io_println!");
    io_print(STDOUT_FILENO,   "  io_print:   no newline... ");
    io_println(STDOUT_FILENO, "done.");
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, const char **argv) {
    /* ── Argument parsing ────────────────────────────────────────────────── */
    const char *name  = args_get_str(argc, argv, "--name");
    int         count = args_get_int(argc, argv, "--count", 1);
    int         verbose = args_has_flag(argc, argv, "-v");

    const char *positional[8];
    const char *value_flags[] = { "--name", "--count", NULL };
    int npos = args_positional(argc, argv, positional, 8, value_flags);

    printf("\n=== ParinOS SDK Demo (PID %d) ===\n\n", getpid());

    if (name)    printf("  --name   = %s\n", name);
    if (verbose) printf("  -v       = (verbose mode)\n");
    printf("  --count  = %d\n", count);
    printf("  positional args (%d):", npos);
    for (int i = 0; i < npos; i++) printf(" %s", positional[i]);
    puts("\n");

    /* ── Run demos ───────────────────────────────────────────────────────── */
    demo_math();   puts("");
    demo_str();    puts("");
    demo_fs();     puts("");
    demo_io();     puts("");

    LOG_INFO("Demo complete.");
    return 0;
}
