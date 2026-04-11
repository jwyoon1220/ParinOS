/*
 * sdk/lib/crt0.c — C Runtime Startup for ParinOS User Programs
 *
 * The kernel's ELF loader calls _start(argc, argv) directly.
 * _start invokes main() and then issues SYS_EXIT with the return value.
 */

#include "syscall.h"

extern int main(int argc, const char **argv);

void _start(int argc, const char **argv) {
    int ret = main(argc, argv);
    syscall1(SYS_EXIT, ret);
    while (1) {}
}
