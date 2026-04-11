/*
 * sdk/lib/stdio.c — ParinOS SDK: Standard I/O Implementation
 *
 * Provides:
 *   FILE*, fopen/fclose, fgetc/fputc, fgets/fputs, fread/fwrite,
 *   fseek/ftell, feof/ferror/clearerr/rewind, ungetc, gets_s, perror,
 *   printf/fprintf/sprintf/snprintf/vprintf/vfprintf/vsnprintf,
 *   scanf/fscanf/sscanf/vscanf/vfscanf/vsscanf.
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

/* ── Standard file objects ───────────────────────────────────────────────── */
static FILE _stdin_obj  = { .fd = STDIN_FILENO,  .buf_pos = 0, .buf_len = 0,
                             .eof = 0, .err = 0, .unget_char = -1 };
static FILE _stdout_obj = { .fd = STDOUT_FILENO, .buf_pos = 0, .buf_len = 0,
                             .eof = 0, .err = 0, .unget_char = -1 };
static FILE _stderr_obj = { .fd = STDERR_FILENO, .buf_pos = 0, .buf_len = 0,
                             .eof = 0, .err = 0, .unget_char = -1 };

FILE *stdin  = &_stdin_obj;
FILE *stdout = &_stdout_obj;
FILE *stderr = &_stderr_obj;

/* ── fopen / fclose ──────────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w')                        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')                   flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    else if (mode[0] == 'w' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;

    int fd = syscall3(SYS_OPEN, (int)path, flags, 0644);
    if (fd < 0) return (FILE*)0;

    FILE *f = (FILE*)malloc(sizeof(FILE));
    if (!f) { syscall1(SYS_CLOSE, fd); return (FILE*)0; }

    f->fd         = fd;
    f->buf_pos    = 0;
    f->buf_len    = 0;
    f->eof        = 0;
    f->err        = 0;
    f->unget_char = -1;
    return f;
}

int fclose(FILE *f) {
    if (!f || f == stdin || f == stdout || f == stderr) return -1;
    syscall1(SYS_CLOSE, f->fd);
    free(f);
    return 0;
}

/* ── ungetc / rewind ─────────────────────────────────────────────────────── */
int ungetc(int c, FILE *f) {
    if (c == -1 || !f) return -1;
    f->unget_char = (unsigned char)c;
    f->eof = 0;
    return (unsigned char)c;
}

void rewind(FILE *f) {
    if (!f) return;
    syscall3(SYS_LSEEK, f->fd, 0, SEEK_SET);
    f->buf_pos    = 0;
    f->buf_len    = 0;
    f->eof        = 0;
    f->err        = 0;
    f->unget_char = -1;
}

/* ── Character output ────────────────────────────────────────────────────── */
int fputc(int c, FILE *f) {
    char ch = (char)c;
    int n = syscall3(SYS_WRITE, f->fd, (int)&ch, 1);
    return (n == 1) ? (unsigned char)c : -1;
}

int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f) {
    int len = (int)strlen(s);
    return syscall3(SYS_WRITE, f->fd, (int)s, len);
}

int puts(const char *s) {
    int n = fputs(s, stdout);
    putchar('\n');
    return n;
}

/* ── Character input ─────────────────────────────────────────────────────── */
int fgetc(FILE *f) {
    if (f->eof) return -1;

    /* Return pushed-back character if any */
    if (f->unget_char >= 0) {
        int c = f->unget_char;
        f->unget_char = -1;
        return c;
    }

    if (f->buf_pos >= f->buf_len) {
        int n = syscall3(SYS_READ, f->fd, (int)f->buf, FILE_BUF_SIZE);
        if (n <= 0) { f->eof = 1; return -1; }
        f->buf_len = n;
        f->buf_pos = 0;
    }
    return (unsigned char)f->buf[f->buf_pos++];
}

int getchar(void) { return fgetc(stdin); }

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

char *gets_s(char *buf, int size) {
    if (!buf || size <= 0) return (char*)0;
    char *r = fgets(buf, size, stdin);
    if (r) {
        int len = (int)strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    }
    return r;
}

/* ── Block I/O ───────────────────────────────────────────────────────────── */
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

/* ── fseek / ftell ───────────────────────────────────────────────────────── */
int fseek(FILE *f, long offset, int whence) {
    int ret = syscall3(SYS_LSEEK, f->fd, (int)offset, whence);
    f->buf_pos    = 0;
    f->buf_len    = 0;
    f->unget_char = -1;
    return (ret < 0) ? -1 : 0;
}

long ftell(FILE *f) {
    return (long)syscall3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
}

/* ── Status ──────────────────────────────────────────────────────────────── */
int feof(FILE *f)      { return f->eof; }
int ferror(FILE *f)    { return f->err; }
void clearerr(FILE *f) { f->eof = 0; f->err = 0; }

void perror(const char *s) {
    if (s && *s) { fputs(s, stderr); fputs(": error\n", stderr); }
}

/* ══════════════════════════════════════════════════════════════════════════
 * printf family — formatted output
 * ══════════════════════════════════════════════════════════════════════════ */

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;
    size_t pos = 0;

#define PUTC(c) do { if (pos < size - 1) buf[pos++] = (char)(c); } while(0)

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUTC(*p); continue; }
        p++;
        if (!*p) break;

        /* ── Flags (any order, any combination) ── */
        int left_align = 0, zero_pad = 0, alt_form = 0, show_sign = 0;
        for (;;) {
            if      (*p == '-') { left_align = 1; p++; }
            else if (*p == '+') { show_sign  = 1; p++; }
            else if (*p == '0') { zero_pad   = 1; p++; }
            else if (*p == '#') { alt_form   = 1; p++; }
            else if (*p == ' ') {                  p++; } /* ignore */
            else break;
        }

        /* ── Field width ── */
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p++ - '0'); }

        /* ── Precision (parse and discard — not needed for basic integers) ── */
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }

        /* ── Length modifier ── */
        int is_long = 0;
        if      (*p == 'l') { is_long = 1; p++; }
        else if (*p == 'z') {              p++; } /* size_t == uint32 on i686 */
        else if (*p == 'h') {              p++; } /* short promoted to int    */

        /* ── Helper: emit integer digits (stored reversed in tmp[]) ── */
#define EMIT_INT(tmp, len, sign_ch)                                     \
        do {                                                            \
            char _sc  = (sign_ch);                                      \
            int  _tot = (len) + (_sc ? 1 : 0);                         \
            int  _pad = width - _tot;                                   \
            if (left_align) {                                           \
                if (_sc) PUTC(_sc);                                     \
                for (int _i = (len)-1; _i >= 0; _i--) PUTC((tmp)[_i]); \
                while (_pad-- > 0) PUTC(' ');                           \
            } else if (zero_pad) {                                      \
                if (_sc) PUTC(_sc);                                     \
                while (_pad-- > 0) PUTC('0');                           \
                for (int _i = (len)-1; _i >= 0; _i--) PUTC((tmp)[_i]); \
            } else {                                                    \
                while (_pad-- > 0) PUTC(' ');                           \
                if (_sc) PUTC(_sc);                                     \
                for (int _i = (len)-1; _i >= 0; _i--) PUTC((tmp)[_i]); \
            }                                                           \
        } while(0)

        switch (*p) {
            /* ── %d / %i — signed decimal ── */
            case 'd': case 'i': {
                long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
                char tmp[22]; int len = 0;
                unsigned long uval;
                char sc = 0;
                if (val < 0) { sc = '-'; uval = (unsigned long)(-(unsigned long)val); }
                else         { sc = show_sign ? '+' : 0; uval = (unsigned long)val;   }
                if (uval == 0) { tmp[len++] = '0'; }
                else { while (uval) { tmp[len++] = (char)('0' + uval % 10); uval /= 10; } }
                EMIT_INT(tmp, len, sc);
                break;
            }
            /* ── %u — unsigned decimal ── */
            case 'u': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                            : (unsigned long)va_arg(ap, unsigned int);
                char tmp[22]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = (char)('0' + val % 10); val /= 10; } }
                EMIT_INT(tmp, len, 0);
                break;
            }
            /* ── %o — octal ── */
            case 'o': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                            : (unsigned long)va_arg(ap, unsigned int);
                char tmp[22]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = (char)('0' + (val & 7)); val >>= 3; } }
                /* alt_form: prepend '0' only if MSB digit is not already '0' */
                int pfx = (alt_form && tmp[len - 1] != '0') ? 1 : 0;
                int pad = width - len - pfx;
                if (left_align) {
                    if (pfx) PUTC('0');
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                    while (pad-- > 0) PUTC(' ');
                } else if (zero_pad) {
                    if (pfx) PUTC('0');
                    while (pad-- > 0) PUTC('0');
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                } else {
                    while (pad-- > 0) PUTC(' ');
                    if (pfx) PUTC('0');
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                }
                break;
            }
            /* ── %x / %X — hexadecimal ── */
            case 'x': case 'X': {
                unsigned long val = is_long ? va_arg(ap, unsigned long)
                                            : (unsigned long)va_arg(ap, unsigned int);
                const char *hex = (*p == 'x') ? "0123456789abcdef"
                                               : "0123456789ABCDEF";
                char tmp[22]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val) { tmp[len++] = hex[val & 0xf]; val >>= 4; } }
                int pfx = alt_form ? 2 : 0;   /* "0x" or "0X" */
                int pad = width - len - pfx;
                char pfx2 = (*p == 'x') ? 'x' : 'X';
                if (left_align) {
                    if (alt_form) { PUTC('0'); PUTC(pfx2); }
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                    while (pad-- > 0) PUTC(' ');
                } else if (zero_pad) {
                    if (alt_form) { PUTC('0'); PUTC(pfx2); }
                    while (pad-- > 0) PUTC('0');
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                } else {
                    while (pad-- > 0) PUTC(' ');
                    if (alt_form) { PUTC('0'); PUTC(pfx2); }
                    for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                }
                break;
            }
            /* ── %s — string ── */
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
            /* ── %c — character ── */
            case 'c': {
                char c = (char)va_arg(ap, int);
                int pad = width - 1;
                if (!left_align) while (pad-- > 0) PUTC(' ');
                PUTC(c);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            /* ── %p — pointer ── */
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
            /* ── %n — chars written so far ── */
            case 'n':
                *va_arg(ap, int*) = (int)pos;
                break;
            /* ── %% — literal percent ── */
            case '%': PUTC('%'); break;
            default:  PUTC('%'); PUTC(*p); break;
        }
#undef EMIT_INT
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
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap); return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap); return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    /* No size known — use safe upper bound; prefer snprintf() when possible */
    int n = vsnprintf(buf, 4096, fmt, ap);
    va_end(ap); return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap); return n;
}

/* ══════════════════════════════════════════════════════════════════════════
 * scanf family — formatted input
 * ══════════════════════════════════════════════════════════════════════════ */

/* Abstract character source used by the scan core */
typedef struct {
    int        is_str;   /* 1 = string, 0 = FILE* */
    const char *str_p;   /* current position (string mode) */
    FILE       *file;    /* stream (file mode) */
    int         nread;   /* total chars consumed */
} _ScanSrc;

static int _src_getc(_ScanSrc *src) {
    int c = (src->is_str) ? (*src->str_p ? (unsigned char)*src->str_p++ : -1)
                          : fgetc(src->file);
    if (c >= 0) src->nread++;
    return c;
}

static void _src_ungetc(_ScanSrc *src, int c) {
    if (c < 0) return;
    src->nread--;
    if (src->is_str) src->str_p--;
    else             ungetc(c, src->file);
}

static void _src_skip_ws(_ScanSrc *src) {
    int c;
    while ((c = _src_getc(src)) >= 0) {
        if (c!=' ' && c!='\t' && c!='\n' && c!='\r' && c!='\f' && c!='\v') {
            _src_ungetc(src, c);
            return;
        }
    }
}

/* Core scanner — drives all public scanf variants */
static int vscan_core(_ScanSrc *src, const char *fmt, va_list ap) {
    int assigned = 0;

    for (const char *f = fmt; *f; f++) {
        /* Whitespace in format → skip any whitespace in the source */
        if (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') {
            _src_skip_ws(src);
            continue;
        }

        /* Literal character: must match exactly */
        if (*f != '%') {
            int c = _src_getc(src);
            if (c < 0 || c != (unsigned char)*f) {
                if (c >= 0) _src_ungetc(src, c);
                return assigned;
            }
            continue;
        }

        f++;
        if (!*f) break;

        /* Suppress assignment */
        int suppress = 0;
        if (*f == '*') { suppress = 1; f++; }

        /* Field width */
        int width = 0;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        int max_w = (width > 0) ? width : 0x7FFFFFFF;

        /* Length modifier */
        int is_long = 0;
        if      (*f == 'l') { is_long = 1; f++; }
        else if (*f == 'h') {              f++; } /* short: treat as int */

        switch (*f) {

            /* ── Integer specifiers: d i u o x X ── */
            case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
                _src_skip_ws(src);
                int c = _src_getc(src);
                if (c < 0) return (assigned > 0) ? assigned : -1;

                int is_signed = (*f == 'd' || *f == 'i');
                int base = 10;
                if (*f == 'o')              base = 8;
                else if (*f=='x'||*f=='X') base = 16;

                unsigned long uval = 0;
                int neg = 0, digits = 0, rem = max_w;

                /* Optional sign */
                if (is_signed && rem > 0) {
                    if      (c == '-') { neg = 1; rem--; c = _src_getc(src); }
                    else if (c == '+') {          rem--; c = _src_getc(src); }
                }

                /* Base detection for %i */
                if (*f == 'i' && c == '0' && rem > 0) {
                    int nx = _src_getc(src); rem--;
                    if (nx == 'x' || nx == 'X') {
                        base = 16;
                        c = (rem-- > 0) ? _src_getc(src) : -1;
                        /* '0x' prefix consumed; don't count as digits */
                    } else {
                        base = 8;
                        _src_ungetc(src, nx); rem++;
                        /* '0' will be the first octal digit below */
                    }
                }

                /* Digits */
                while (rem > 0 && c >= 0) {
                    int d;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (base==16 && c>='a' && c<='f') d = c-'a'+10;
                    else if (base==16 && c>='A' && c<='F') d = c-'A'+10;
                    else break;
                    if (d >= base) break;
                    uval = uval * (unsigned long)base + (unsigned long)d;
                    digits++; rem--;
                    c = _src_getc(src);
                }
                if (c >= 0) _src_ungetc(src, c);
                if (digits == 0) return assigned;

                if (!suppress) {
                    if (is_signed) {
                        long sv = neg ? -(long)uval : (long)uval;
                        if (is_long) *va_arg(ap, long*)         = sv;
                        else         *va_arg(ap, int*)           = (int)sv;
                    } else {
                        if (is_long) *va_arg(ap, unsigned long*) = uval;
                        else         *va_arg(ap, unsigned int*)  = (unsigned int)uval;
                    }
                    assigned++;
                }
                break;
            }

            /* ── %s — word (stops at whitespace) ── */
            case 's': {
                _src_skip_ws(src);
                int c = _src_getc(src);
                if (c < 0) return (assigned > 0) ? assigned : -1;
                char *dst = suppress ? (char*)0 : va_arg(ap, char*);
                int n = 0, rem = max_w;
                while (rem > 0 && c >= 0 &&
                       c!=' ' && c!='\t' && c!='\n' && c!='\r') {
                    if (dst) dst[n] = (char)c;
                    n++; rem--;
                    c = _src_getc(src);
                }
                if (c >= 0) _src_ungetc(src, c);
                if (n == 0) return assigned;
                if (dst) dst[n] = '\0';
                if (!suppress) assigned++;
                break;
            }

            /* ── %c — raw character(s), no whitespace skip ── */
            case 'c': {
                int w = (width > 0) ? width : 1;
                char *dst = suppress ? (char*)0 : va_arg(ap, char*);
                int n = 0;
                while (n < w) {
                    int c = _src_getc(src);
                    if (c < 0) { if (n == 0) return (assigned>0)?assigned:-1; break; }
                    if (dst) dst[n] = (char)c;
                    n++;
                }
                if (!suppress && n > 0) assigned++;
                break;
            }

            /* ── %n — store chars consumed, does not increment assigned ── */
            case 'n':
                if (!suppress) *va_arg(ap, int*) = src->nread;
                break;

            /* ── %% — literal percent sign ── */
            case '%': {
                int c = _src_getc(src);
                if (c < 0 || c != '%') {
                    if (c >= 0) _src_ungetc(src, c);
                    return assigned;
                }
                break;
            }

            default:
                return assigned;
        }
    }
    return assigned;
}

/* ── Public scanf wrappers ───────────────────────────────────────────────── */

int vsscanf(const char *str, const char *fmt, va_list ap) {
    _ScanSrc src = { .is_str=1, .str_p=str, .file=(FILE*)0, .nread=0 };
    return vscan_core(&src, fmt, ap);
}

int vfscanf(FILE *f, const char *fmt, va_list ap) {
    _ScanSrc src = { .is_str=0, .str_p=(const char*)0, .file=f, .nread=0 };
    return vscan_core(&src, fmt, ap);
}

int vscanf(const char *fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsscanf(str, fmt, ap);
    va_end(ap); return n;
}

int fscanf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfscanf(f, fmt, ap);
    va_end(ap); return n;
}

int scanf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap); return n;
}
