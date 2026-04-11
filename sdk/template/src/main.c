/* Enable debug logging before pulling in parin.h */
#define PARIN_LOG_LEVEL 0

#include <parin.h>

/*
 * template/src/main.c — ParinOS SDK Program Template
 *
 * Demonstrates the full SDK API surface.
 * Build:  make -C template
 * Run:    /bin/myprogram [--name <str>] [--count <n>] [-v] [files...]
 */

/* ── scanf / printf demo ─────────────────────────────────────────────────── */
static void demo_stdio(void) {
    puts("── stdio (printf / scanf) ─────────────────────────────");

    /* Enhanced printf specifiers */
    printf("  %%d  : %d\n",         -42);
    printf("  %%+d : %+d\n",         42);
    printf("  %%05d: %05d\n",         7);
    printf("  %%o  : %o\n",          255);    /* octal */
    printf("  %%#o : %#o\n",         255);    /* octal with prefix */
    printf("  %%x  : %x\n",          255);    /* hex lower */
    printf("  %%#X : %#X\n",         255);    /* hex upper with prefix */
    printf("  %%ld : %ld\n",  (long)-123456); /* long */
    printf("  %%lu : %lu\n",  (unsigned long)999999UL);
    printf("  %%lx : %lx\n",  (unsigned long)0xDEADBEEFUL);
    printf("  %%n  : chars written so far: ");
    int nc = 0;
    printf("%n", &nc);
    printf("(was %d)\n", nc);
    printf("  %%p  : %p\n",  (void*)0x400000);

    /* sscanf — parse from a string */
    puts("\n  sscanf demo:");
    int   a, b;
    char  word[32];
    float dummy;
    int n = sscanf("42 -7 hello", "%d %d %s", &a, &b, word);
    printf("    sscanf(\"42 -7 hello\", \"%%d %%d %%s\") → %d items: %d  %d  \"%s\"\n",
           n, a, b, word);

    unsigned int hex_val;
    sscanf("0xFF", "%i", (int*)&hex_val);
    printf("    sscanf(\"0xFF\", \"%%i\") → %u (hex auto-detect)\n", hex_val);

    unsigned int oct_val;
    sscanf("010", "%i", (int*)&oct_val);
    printf("    sscanf(\"010\",  \"%%i\") → %u (octal auto-detect)\n", oct_val);
    (void)dummy;
}

/* ── ctype demo ──────────────────────────────────────────────────────────── */
static void demo_ctype(void) {
    puts("── ctype ──────────────────────────────────────────────");
    const char *test = "Hello, ParinOS 2025!";
    int letters = 0, digits = 0, spaces = 0, punct = 0;
    for (const char *p = test; *p; p++) {
        if (isalpha((unsigned char)*p)) letters++;
        if (isdigit((unsigned char)*p)) digits++;
        if (isspace((unsigned char)*p)) spaces++;
        if (ispunct((unsigned char)*p)) punct++;
    }
    printf("  \"%s\"\n", test);
    printf("  letters=%d  digits=%d  spaces=%d  punct=%d\n",
           letters, digits, spaces, punct);
    printf("  toupper: ");
    for (const char *p = test; *p; p++) putchar(toupper((unsigned char)*p));
    putchar('\n');
}

/* ── qsort / bsearch demo ────────────────────────────────────────────────── */
static int int_cmp(const void *a, const void *b) {
    return *(const int*)a - *(const int*)b;
}

static void demo_sort(void) {
    puts("── qsort / bsearch ────────────────────────────────────");
    int arr[] = { 42, 7, 99, 1, 55, 23, 8, 3, 77, 14 };
    int n = (int)(sizeof(arr) / sizeof(arr[0]));

    printf("  Before: ");
    for (int i = 0; i < n; i++) printf("%d ", arr[i]);
    putchar('\n');

    qsort(arr, (size_t)n, sizeof(int), int_cmp);

    printf("  After : ");
    for (int i = 0; i < n; i++) printf("%d ", arr[i]);
    putchar('\n');

    int key = 42;
    int *found = (int*)bsearch(&key, arr, (size_t)n, sizeof(int), int_cmp);
    printf("  bsearch(%d) → %s\n", key, found ? "found" : "not found");
}

/* ── String utilities demo ───────────────────────────────────────────────── */
static void demo_str(void) {
    puts("── parin/str ──────────────────────────────────────────");
    char s[] = "  Hello, ParinOS!  ";
    printf("  trim(\"%s\") = \"%s\"\n", s, str_trim(s));

    char *dup = str_dup("ParinOS");
    str_to_upper(dup);
    printf("  str_dup + to_upper: \"%s\"\n", dup);
    free(dup);

    printf("  starts_with(\"hello\",\"hel\") = %d\n", str_starts_with("hello","hel"));
    printf("  ends_with(\"hello\",\"llo\")   = %d\n", str_ends_with("hello","llo"));

    char *buf; char *parts[8];
    int cnt = str_split("/usr/local/bin", '/', parts, 8, &buf);
    printf("  split(\"/usr/local/bin\", '/') →");
    for (int i = 0; i < cnt; i++) printf(" \"%s\"", parts[i]);
    putchar('\n');
    free(buf);
}

/* ── Math utilities demo ─────────────────────────────────────────────────── */
static void demo_math(void) {
    puts("── parin/math ─────────────────────────────────────────");
    printf("  clamp(200, 0, 100)  = %d\n", parin_clamp(200, 0, 100));
    printf("  round_up(13, 8)     = %u\n", parin_round_up(13, 8));
    printf("  round_down(13, 8)   = %u\n", parin_round_down(13, 8));
    printf("  is_pow2(64)         = %d\n", parin_is_pow2(64));
    printf("  popcount(0xFF)      = %d\n", parin_popcount(0xFF));
    printf("  highest_bit(0x80)   = %d\n", parin_highest_bit(0x80));
}

/* ── Filesystem + logging demo ───────────────────────────────────────────── */
static void demo_fs(void) {
    puts("── parin/fs + parin/log ───────────────────────────────");

    const char *tmp = "/tmp/sdk_demo.txt";
    const char *msg = "Hello, ParinOS SDK!\n";
    if (fs_write_all(tmp, msg, (int)strlen(msg)) > 0) {
        LOG_INFO("Wrote %d bytes to %s", (int)strlen(msg), tmp);
        int sz = 0;
        char *data = fs_read_all(tmp, &sz);
        if (data) {
            printf("  Read back: %s", data);
            free(data);
        }
        fs_remove(tmp);
        LOG_DEBUG("Cleaned up %s", tmp);
    } else {
        LOG_WARN("/tmp may not exist — skipping file demo");
    }

    printf("  fs_exists(\"/bin\")  = %d\n", fs_exists("/bin"));
    printf("  fs_is_dir(\"/bin\")  = %d\n", fs_is_dir("/bin"));
    printf("  fs_is_file(\"/bin\") = %d\n", fs_is_file("/bin"));
}

/* ── I/O helpers demo ────────────────────────────────────────────────────── */
static void demo_io(void) {
    puts("── parin/io ───────────────────────────────────────────");
    io_println(STDOUT_FILENO, "  io_println: Hello!");
    io_print(STDOUT_FILENO,   "  io_print:   no newline...");
    io_println(STDOUT_FILENO, " done.");
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, const char **argv) {
    /* Argument parsing */
    const char *name    = args_get_str(argc, argv, "--name");
    int         count   = args_get_int(argc, argv, "--count", 1);
    int         verbose = args_has_flag(argc, argv, "-v");

    const char *pos[8];
    const char *vflags[] = { "--name", "--count", NULL };
    int npos = args_positional(argc, argv, pos, 8, vflags);

    printf("\n=== ParinOS SDK Demo  PID=%d ===\n\n", getpid());
    if (name)    printf("  --name  = %s\n",  name);
    if (verbose) printf("  -v      = (verbose)\n");
    printf("  --count = %d\n",   count);
    printf("  positional (%d):", npos);
    for (int i = 0; i < npos; i++) printf(" %s", pos[i]);
    puts("\n");

    /* Run all demos */
    demo_stdio();  puts("");
    demo_ctype();  puts("");
    demo_sort();   puts("");
    demo_str();    puts("");
    demo_math();   puts("");
    demo_fs();     puts("");
    demo_io();     puts("");

    LOG_INFO("All demos complete.");
    return 0;
}
