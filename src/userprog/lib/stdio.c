/*
 * stdio.c — 유저 프로그램 표준 입출력 구현
 *
 * 모든 I/O는 int 0x80 시스템 콜을 통해 커널에 위임됩니다.
 */

#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/syscall.h"

/* ── 표준 파일 객체 ────────────────────────────────────────────────── */
static FILE _stdin_obj  = { .fd = STDIN_FILENO,  .buf_pos = 0, .buf_len = 0, .eof = 0, .err = 0 };
static FILE _stdout_obj = { .fd = STDOUT_FILENO, .buf_pos = 0, .buf_len = 0, .eof = 0, .err = 0 };
static FILE _stderr_obj = { .fd = STDERR_FILENO, .buf_pos = 0, .buf_len = 0, .eof = 0, .err = 0 };

FILE *stdin  = &_stdin_obj;
FILE *stdout = &_stdout_obj;
FILE *stderr = &_stderr_obj;

/* ── fopen / fclose ─────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w')      flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    else if (mode[0] == 'w' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;

    int fd = syscall3(SYS_OPEN, (int)path, flags, 0644);
    if (fd < 0) return (FILE*)0;

    FILE *f = (FILE*)malloc(sizeof(FILE));
    if (!f) { syscall1(SYS_CLOSE, fd); return (FILE*)0; }

    f->fd      = fd;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->eof     = 0;
    f->err     = 0;
    return f;
}

int fclose(FILE *f) {
    if (!f || f == stdin || f == stdout || f == stderr) return -1;
    syscall1(SYS_CLOSE, f->fd);
    free(f);
    return 0;
}

/* ── 문자 출력 ────────────────────────────────────────────────────────── */
int fputc(int c, FILE *f) {
    char ch = (char)c;
    int n = syscall3(SYS_WRITE, f->fd, (int)&ch, 1);
    return (n == 1) ? (unsigned char)c : -1;
}

int putchar(int c) {
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *f) {
    int len = (int)strlen(s);
    return syscall3(SYS_WRITE, f->fd, (int)s, len);
}

int puts(const char *s) {
    int n = fputs(s, stdout);
    putchar('\n');
    return n;
}

/* ── 문자 입력 ────────────────────────────────────────────────────────── */
int fgetc(FILE *f) {
    if (f->eof) return -1; /* EOF */

    /* 버퍼가 비었으면 채움 */
    if (f->buf_pos >= f->buf_len) {
        int n = syscall3(SYS_READ, f->fd, (int)f->buf, FILE_BUF_SIZE);
        if (n <= 0) { f->eof = 1; return -1; }
        f->buf_len = n;
        f->buf_pos = 0;
    }
    return (unsigned char)f->buf[f->buf_pos++];
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *buf, int size, FILE *f) {
    if (!buf || size <= 0) return (char*)0;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c < 0) break;
        buf[i++] = (char)c;
        if ((char)c == '\n') break;
    }
    buf[i] = '\0';
    return (i > 0) ? buf : (char*)0;
}

/* ── 블록 I/O ─────────────────────────────────────────────────────────── */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    size_t total = size * nmemb;
    int n = syscall3(SYS_READ, f->fd, (int)ptr, (int)total);
    if (n <= 0) { f->eof = 1; return 0; }
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    size_t total = size * nmemb;
    int n = syscall3(SYS_WRITE, f->fd, (int)ptr, (int)total);
    if (n <= 0) return 0;
    return (size_t)n / size;
}

/* ── fseek / ftell ───────────────────────────────────────────────────── */
int fseek(FILE *f, long offset, int whence) {
    int ret = syscall3(SYS_LSEEK, f->fd, (int)offset, whence);
    f->buf_pos = f->buf_len = 0; /* 버퍼 무효화 */
    return (ret < 0) ? -1 : 0;
}

long ftell(FILE *f) {
    return (long)syscall3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
}

/* ── 상태 확인 ────────────────────────────────────────────────────────── */
int feof(FILE *f)   { return f->eof; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = 0; f->err = 0; }

/* ── perror ──────────────────────────────────────────────────────────── */
void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": error\n", stderr);
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * 포맷 출력 (vsnprintf → write)
 * ──────────────────────────────────────────────────────────────────────── */

/* 내부 vsnprintf 구현 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;
    size_t pos = 0;

#define PUTC(c) do { if (pos < size - 1) buf[pos++] = (char)(c); } while(0)

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUTC(*p); continue; }
        p++;
        if (!*p) break;

        int zero_pad = 0, left_align = 0;
        if (*p == '-') { left_align = 1; p++; }
        if (*p == '0') { zero_pad   = 1; p++; }

        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p++ - '0'); }

        switch (*p) {
            case 'd': case 'i': {
                int val = va_arg(ap, int);
                char tmp[16]; int len = 0, neg = 0;
                /* INT_MIN 특수 처리: -(-2147483648) 는 정의되지 않은 동작 */
                if (val == (int)0x80000000) {
                    static const char int_min[] = "-2147483648";
                    for (int k = 0; int_min[k]; k++) PUTC(int_min[k]);
                    break;
                }
                if (val < 0) { neg = 1; val = -val; }
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = (char)('0' + val % 10); val /= 10; } }
                if (neg) tmp[len++] = '-';
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(zero_pad ? '0' : ' ');
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                char tmp[16]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = (char)('0' + val % 10); val /= 10; } }
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(zero_pad ? '0' : ' ');
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 'x': case 'X': {
                unsigned int val = va_arg(ap, unsigned int);
                const char *digits = (*p == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                char tmp[16]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = digits[val & 0xf]; val >>= 4; } }
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(zero_pad ? '0' : ' ');
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(' ');
                while (*s) PUTC(*s++);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                int pad = width - 1;
                if (!left_align) while (pad-- > 0) PUTC(' ');
                PUTC(c);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 'p': {
                unsigned int val = (unsigned int)(unsigned long)va_arg(ap, void *);
                PUTC('0'); PUTC('x');
                char tmp[8]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = "0123456789abcdef"[val & 0xf]; val >>= 4; } }
                while (len < 8) tmp[len++] = '0';
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                break;
            }
            case '%': PUTC('%'); break;
            default:  PUTC('%'); PUTC(*p); break;
        }
    }
#undef PUTC
    buf[pos] = '\0';
    return (int)pos;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    syscall3(SYS_WRITE, f->fd, (int)buf, n);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* 버퍼 크기를 알 수 없으므로 합리적인 상한(4096)을 사용합니다.
     * 오버플로우 위험이 있을 때는 snprintf를 사용하세요. */
    int n = vsnprintf(buf, 4096, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
