/*
 * template/src/main.c — ParinOS SDK Program Template
 *
 * This file is the starting point for a new ParinOS user program.
 * Build with:  make -C template
 * Run on ParinOS:  /bin/myprogram
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

int main(int argc, const char **argv) {
    /* ── Print a greeting ─────────────────────────────────────────────── */
    printf("Hello from ParinOS! PID = %d\n", getpid());

    /* ── Show command-line arguments ──────────────────────────────────── */
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }

    /* ── List the /bin directory ──────────────────────────────────────── */
    puts("\n/bin contents:");
    int dfd = opendir_fd("/bin");
    if (dfd >= 0) {
        struct dirent ent;
        while (readdir_r(dfd, &ent) == 0) {
            char type = (ent.d_type == DT_DIR) ? 'd' : '-';
            printf("  %c  %-20s  %u bytes\n", type, ent.d_name, ent.d_size);
        }
        closedir_fd(dfd);
    }

    /* ── stat() example ──────────────────────────────────────────────── */
    struct stat st;
    if (stat("/bin", &st) == 0) {
        printf("\nstat(\"/bin\"): size=%u  isdir=%d\n",
               st.st_size, S_ISDIR(st.st_mode));
    }

    return 0;
}
