# ParinOS SDK

The **ParinOS Software Development Kit (SDK)** provides everything you need to write, compile, and run user-space programs on ParinOS.

---

## Directory layout

```
sdk/
├── Makefile          — Build the runtime library (libparin.a)
├── sdk.mk            — Reusable build fragment (include in your Makefile)
├── README.md         — This file
│
├── include/          — Public C headers
│   ├── syscall.h     — Raw sysenter syscall stubs + syscall numbers
│   ├── unistd.h      — POSIX-style low-level I/O (open/read/write/…)
│   ├── stdio.h       — FILE* buffered I/O and printf family
│   ├── stdlib.h      — malloc/free, atoi, exit, …
│   ├── string.h      — strlen, strcpy, memset, …
│   ├── dirent.h      — Directory listing (opendir_fd/readdir_r/closedir_fd)
│   └── sys/
│       └── stat.h    — stat() — query file size and type
│
├── lib/              — Runtime library source files
│   ├── crt0.c        — C runtime startup (_start → main → SYS_EXIT)
│   ├── syscall.asm   — sysenter syscall stubs (syscall0…syscall3)
│   ├── stdio.c       — stdio implementation
│   ├── stdlib.c      — stdlib implementation
│   └── string.c      — string/memory implementation
│
├── link/
│   └── userprog.ld   — Linker script (load address 0x400000)
│
├── build/            — Build output (created by `make`)
│   └── libparin.a    — Static runtime library
│
└── template/         — Starter template for a new program
    ├── Makefile
    └── src/
        └── main.c
```

---

## Prerequisites

| Tool | Purpose |
|------|---------|
| `nasm` | Assemble `syscall.asm` |
| `i686-linux-gnu-gcc` | Cross-compile C sources to ELF32 |
| `i686-linux-gnu-ld` | Link ELF32 binaries |
| `i686-linux-gnu-ar` | Create `libparin.a` |

> **Fallback:** If the cross-compiler is not found, the build falls back to the
> native `gcc` / `ld` with `-m32` flags.  Install the cross-toolchain with:
> ```sh
> sudo apt install gcc-i686-linux-gnu binutils-i686-linux-gnu nasm
> ```

---

## Building the SDK library

```sh
cd sdk
make
```

This produces `sdk/build/libparin.a`.

---

## Creating a new program

### Option 1 — Copy and customise the template

```sh
cp -r sdk/template myprogram
cd myprogram
# Edit src/main.c
make          # build
make install  # copy to disk_src/bin/
```

### Option 2 — Write your own Makefile using `sdk.mk`

```makefile
SDK_DIR := ../sdk
include $(SDK_DIR)/sdk.mk

PROG := hello
SRCS := src/hello.c

$(eval $(call SDK_PROG,$(PROG),$(SRCS),$(BINDIR)))
```

### Option 3 — Manual build (full control)

```sh
SDK=sdk
CC="i686-linux-gnu-gcc"
LD="i686-linux-gnu-ld"

# Compile
$CC -ffreestanding -nostdlib -m32 -fno-pic -fno-stack-protector -O2 \
    -I$SDK/include -c src/hello.c -o hello.o

# Link
$LD -m elf_i386 -nostdlib -T $SDK/link/userprog.ld \
    hello.o $SDK/build/libparin.a -o hello

# Install
cp hello disk_src/bin/hello
```

---

## API reference

### syscall.h — Raw syscall interface

| Symbol | Description |
|--------|-------------|
| `syscall0(num)` | Syscall with no arguments |
| `syscall1(num, a1)` | Syscall with 1 argument |
| `syscall2(num, a1, a2)` | Syscall with 2 arguments |
| `syscall3(num, a1, a2, a3)` | Syscall with 3 arguments |

Common syscall numbers: `SYS_READ` (3), `SYS_WRITE` (4), `SYS_OPEN` (5),
`SYS_CLOSE` (6), `SYS_EXIT` (1), `SYS_GETPID` (20), `SYS_EXEC` (11).

### unistd.h — POSIX low-level I/O

```c
int  open(const char *path, int flags, int mode);
int  read(int fd, void *buf, int count);
int  write(int fd, const void *buf, int count);
int  close(int fd);
int  lseek(int fd, int offset, int whence);
int  unlink(const char *path);
int  mkdir(const char *path, int mode);
int  getpid(void);
void _exit(int code);
int  execve(const char *path, int argc, const char **argv);
void sched_yield(void);
```

File open flags: `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`.  
Seek origins: `SEEK_SET`, `SEEK_CUR`, `SEEK_END`.

### stdio.h — Buffered I/O

```c
FILE *fopen(const char *path, const char *mode);  /* "r","w","a","r+","w+" */
int   fclose(FILE *f);
int   printf(const char *fmt, ...);
int   fprintf(FILE *f, const char *fmt, ...);
int   snprintf(char *buf, size_t size, const char *fmt, ...);
int   fputc(int c, FILE *f);  int putchar(int c);
int   fputs(const char *s, FILE *f);  int puts(const char *s);
int   fgetc(FILE *f);  int getchar(void);
char *fgets(char *buf, int size, FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int   feof(FILE *f);  int ferror(FILE *f);  void clearerr(FILE *f);
int   fseek(FILE *f, long offset, int whence);  long ftell(FILE *f);
void  perror(const char *s);
extern FILE *stdin, *stdout, *stderr;
```

Supported `printf` specifiers: `%d`, `%i`, `%u`, `%x`, `%X`, `%s`, `%c`, `%p`, `%%`.  
Width and left-align (`%-Ns`) / zero-pad (`%0Nd`) flags are supported.

### stdlib.h — Memory and utilities

```c
void *malloc(size_t size);   /* 64 KB static pool */
void  free(void *ptr);
void *calloc(size_t n, size_t size);
void *realloc(void *ptr, size_t size);
void  exit(int code);        /* calls SYS_EXIT */
int   atoi(const char *s);
long  atol(const char *s);
unsigned long strtoul(const char *s, char **end, int base);
long  strtol(const char *s, char **end, int base);
int   abs(int x);
```

### string.h — Strings and memory

```c
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
char  *strtok(char *s, const char *delim);  /* non-reentrant */
```

### dirent.h — Directory listing

```c
int opendir_fd(const char *path);               /* returns fd or <0 on error */
int readdir_r(int fd, struct dirent *ent);      /* 0 = ok, non-0 = end/error */
void closedir_fd(int fd);

struct dirent {
    char     d_name[256];
    uint32_t d_size;
    uint8_t  d_type;   /* DT_REG=1, DT_DIR=2 */
};
```

### sys/stat.h — File status

```c
struct stat { uint32_t st_size; uint16_t st_mode; };
int stat(const char *path, struct stat *buf);   /* SYS_STAT = 106 */

S_ISREG(st_mode)   /* true for regular files */
S_ISDIR(st_mode)   /* true for directories   */
```

---

## Memory model

| Region | Address range | Description |
|--------|--------------|-------------|
| NULL guard | `0x00000000–0x00000FFF` | Unmapped |
| (reserved) | `0x00001000–0x003FFFFF` | — |
| Code/data/heap | `0x00400000–0x7FFFFFFF` | ELF load address + heap |
| Stack | `0x80000000–0xBFFFFFFF` | Grows downward from `0xBFFFF000` |
| Kernel | `0xC0000000–0xFFFFFFFF` | Not accessible from user space |

The `malloc()` implementation uses a 64 KB static pool embedded in the `.bss`
section.  There is no `brk()`/`sbrk()` support; for large allocations, increase
`HEAP_SIZE` in `sdk/lib/stdlib.c` and rebuild.

---

## Syscall convention (sysenter)

```
EAX = syscall number
EBX = argument 1
ECX = argument 2
EDX = argument 3
ESI = return EIP  (set by the stub before sysenter)
EBP = return ESP  (set by the stub before sysenter)
```

Return value is in `EAX` after `sysexit`.

---

## Adding the program to the OS image

```sh
# Build your program and copy to disk_src/bin/
make -C myprogram install

# Rebuild the full OS image (includes disk.img with the new binary)
make -C .. all
```

Then run with QEMU:

```sh
make -C .. run
```

At the ParinOS shell prompt, type the program name:

```
ParinOS> myprogram
```

---

## What's new — developer-friendly C standard-library APIs

### ctype.h — Character classification & conversion

```c
int isalpha(int c);   int isdigit(int c);   int isalnum(int c);
int isupper(int c);   int islower(int c);   int isspace(int c);
int isblank(int c);   int isprint(int c);   int isgraph(int c);
int ispunct(int c);   int iscntrl(int c);   int isxdigit(int c);
int toupper(int c);   int tolower(int c);
```

All header-only static inlines; no extra object file required.

---

### Enhanced printf — new format specifiers

| Specifier | Example | Notes |
|-----------|---------|-------|
| `%o` | `printf("%o", 255)` → `377` | Unsigned octal |
| `%#o` | `printf("%#o", 255)` → `0377` | Octal with `0` prefix |
| `%ld` / `%li` | `printf("%ld", -1L)` | Signed `long` |
| `%lu` / `%lo` / `%lx` / `%lX` | `printf("%lu", 1UL)` | Unsigned `long` variants |
| `%zu` | `printf("%zu", sizeof(int))` | `size_t` (= `unsigned` on i686) |
| `%#x` / `%#X` | `printf("%#x", 255)` → `0xff` | Hex with `0x`/`0X` prefix |
| `%+d` | `printf("%+d", 5)` → `+5` | Always show sign |
| `%n` | `printf("hi%n", &n)` → n=2 | Store chars written so far |
| `%05d` | `printf("%05d", 7)` → `00007` | Zero-padded width |
| `%-10s` | `printf("%-10s", "hi")` → `"hi        "` | Left-aligned |

---

### scanf family — formatted input

```c
int scanf(const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int vfscanf(FILE *f, const char *fmt, va_list ap);
int vsscanf(const char *str, const char *fmt, va_list ap);
```

**Supported conversion specifiers:**

| Specifier | Reads | Notes |
|-----------|-------|-------|
| `%d` | `int*` | Signed decimal integer |
| `%i` | `int*` | Integer with base auto-detection (0x=hex, 0=octal) |
| `%u` | `unsigned int*` | Unsigned decimal |
| `%o` | `unsigned int*` | Unsigned octal |
| `%x` / `%X` | `unsigned int*` | Unsigned hexadecimal |
| `%ld` / `%li` / `%lu` / `%lo` / `%lx` | `long*` / `unsigned long*` | Long variants |
| `%s` | `char*` | Whitespace-delimited word (null-terminated) |
| `%c` | `char*` | Raw character(s); no whitespace skipping |
| `%n` | `int*` | Store number of chars consumed so far |
| `%*…` | — | Suppress assignment (read but discard) |
| Width | `%5d`, `%10s` | Limit maximum characters consumed |
| `%%` | — | Match a literal `%` |

Return value: number of items successfully assigned, or `-1` on input failure
before any assignment.

**Examples:**
```c
int a, b;
char name[32];
scanf("%d %d %s", &a, &b, name);          /* read from keyboard  */
fscanf(f, "%d %d",  &a, &b);              /* read from file      */
sscanf("42 hello", "%d %s", &a, name);   /* parse from string   */

/* Base auto-detect with %i */
int v; sscanf("0xFF", "%i", &v);  /* v = 255 */
sscanf("010",  "%i", &v);         /* v = 8   */
sscanf("42",   "%i", &v);         /* v = 42  */

/* Suppress a field */
sscanf("100 skip 200", "%d %*s %d", &a, &b); /* a=100, b=200 */
```

---

### Additional stdio functions

```c
int  ungetc(int c, FILE *f);     /* Push one character back into stream     */
void rewind(FILE *f);            /* Seek to start, clear error/EOF flags     */
char *gets_s(char *buf, int n);  /* Safe stdin line read (strips '\n')       */
```

---

### stdlib.h additions — sorting and searching

```c
void  qsort(void *base, size_t n, size_t sz,
            int (*cmp)(const void *, const void *));

void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *));
```

`qsort` uses an in-place introsort (quicksort with insertion sort for small
partitions and median-of-three pivot selection).  `bsearch` requires the array
to be sorted in ascending order according to `cmp`.

**Example:**
```c
int cmp_int(const void *a, const void *b) {
    return *(const int*)a - *(const int*)b;
}

int arr[] = { 5, 3, 8, 1, 9 };
qsort(arr, 5, sizeof(int), cmp_int);
/* arr is now { 1, 3, 5, 8, 9 } */

int key = 8;
int *p = bsearch(&key, arr, 5, sizeof(int), cmp_int);
if (p) printf("found: %d\n", *p);
```
