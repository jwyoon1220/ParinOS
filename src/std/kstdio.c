//
// src/std/kstdio.c — 커널 표준 입출력 구현
//

#include "kstdio.h"
#include "../vga.h"
#include "../mem/mem.h"

/* ── vprintf 포워딩 ─────────────────────────────────────────────────────────
 * kvprintf 는 vga.c 의 kprintf 를 va_list 형태로 호출합니다.
 * vga.c 의 kprintf 는 내부적으로 vga_vprintf 를 갖추고 있다고 가정하지만,
 * 없을 경우를 대비해 ksnprintf → kprint 방식으로 구현합니다.
 * ─────────────────────────────────────────────────────────────────────────── */

/* 내부 포맷터 — 전방 선언 (kvprintf 에서 사용) */
static int vsnprintf_k(char *buf, size_t size, const char *fmt, va_list ap);

void kvprintf(const char *fmt, va_list ap) {
    char buf[512];
    vsnprintf_k(buf, sizeof(buf), fmt, ap);
    kprint(buf);
}

/* ── ksprintf / ksnprintf ────────────────────────────────────────────────── */

/* 내부 포맷터 (va_list 버전) */
static int vsnprintf_k(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;

    size_t pos = 0;

#define PUTC(c) do { if (pos < size - 1) buf[pos++] = (char)(c); } while(0)

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUTC(*p); continue; }
        p++;
        if (!*p) break;

        /* 플래그 */
        int zero_pad = 0, left_align = 0;
        if (*p == '-') { left_align = 1; p++; }
        if (*p == '0') { zero_pad   = 1; p++; }

        /* 너비 */
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p++ - '0'); }

        /* 변환 */
        switch (*p) {
            case 'd': case 'i': {
                int val = va_arg(ap, int);
                char tmp[16]; int len = 0, neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val > 0) { tmp[len++] = (char)('0' + val % 10); val /= 10; } }
                if (neg) tmp[len++] = '-';
                /* 너비 채우기 */
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
                else { while (val > 0) { tmp[len++] = (char)('0' + val % 10); val /= 10; } }
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
                else { while (val > 0) { tmp[len++] = digits[val & 0xf]; val >>= 4; } }
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(zero_pad ? '0' : ' ');
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 'o': {
                unsigned int val = va_arg(ap, unsigned int);
                char tmp[16]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val > 0) { tmp[len++] = (char)('0' + (val & 7)); val >>= 3; } }
                int pad = width - len;
                if (!left_align) while (pad-- > 0) PUTC(zero_pad ? '0' : ' ');
                for (int i = len - 1; i >= 0; i--) PUTC(tmp[i]);
                if (left_align)  while (pad-- > 0) PUTC(' ');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = 0; const char *t = s; while (*t++) len++;
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
                unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void *);
                PUTC('0'); PUTC('x');
                char tmp[8]; int len = 0;
                if (val == 0) { tmp[len++] = '0'; }
                else { while (val > 0) { tmp[len++] = "0123456789abcdef"[val & 0xf]; val >>= 4; } }
                while (len < 8) tmp[len++] = '0'; /* 8자리 0-패딩 */
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

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf_k(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int ksprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* 충분히 큰 크기 — 실제 커널 코드에서는 ksnprintf 사용 권장 */
    int n = vsnprintf_k(buf, (size_t)4096, fmt, ap);
    va_end(ap);
    return n;
}

/* ── ksscanf ──────────────────────────────────────────────────────────────── */

static int vsscanf_k(const char *src, const char *fmt, va_list ap) {
    int count = 0;
    const char *s = src;

    for (const char *f = fmt; *f; f++) {
        /* 포맷의 공백 → 입력의 공백 스킵 */
        if (*f == ' ' || *f == '\t' || *f == '\n') {
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;
            continue;
        }
        if (*f != '%') {
            if (*s == *f) { s++; }
            else break;
            continue;
        }
        f++;
        if (!*f) break;

        /* 입력의 앞 공백 스킵 (%c 제외) */
        if (*f != 'c') {
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        }

        switch (*f) {
            case 'd': case 'i': {
                int *out = va_arg(ap, int *);
                int sign = 1, val = 0;
                if (*s == '-') { sign = -1; s++; }
                else if (*s == '+') { s++; }
                if (*s < '0' || *s > '9') goto done;
                while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
                *out = sign * val;
                count++;
                break;
            }
            case 'u': {
                unsigned int *out = va_arg(ap, unsigned int *);
                unsigned int val = 0;
                if (*s < '0' || *s > '9') goto done;
                while (*s >= '0' && *s <= '9') { val = val * 10 + (unsigned)(*s - '0'); s++; }
                *out = val;
                count++;
                break;
            }
            case 'x': case 'X': {
                unsigned int *out = va_arg(ap, unsigned int *);
                unsigned int val = 0;
                /* 선택적 0x 접두어 */
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
                while (1) {
                    char c = *s;
                    int d;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    else break;
                    val = val * 16 + (unsigned)d;
                    s++;
                }
                *out = val;
                count++;
                break;
            }
            case 's': {
                char *out = va_arg(ap, char *);
                while (*s && *s != ' ' && *s != '\t' && *s != '\n')
                    *out++ = *s++;
                *out = '\0';
                count++;
                break;
            }
            case 'c': {
                char *out = va_arg(ap, char *);
                if (!*s) goto done;
                *out = *s++;
                count++;
                break;
            }
            case '%':
                if (*s == '%') s++;
                else goto done;
                break;
            default:
                goto done;
        }
    }
done:
    return count;
}

int ksscanf(const char *src, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf_k(src, fmt, ap);
    va_end(ap);
    return n;
}

/* ── keyboard_readline ────────────────────────────────────────────────────── */

/* 키보드 입력 버퍼 (keyboard.c 의 shell_input 대신 직접 폴링) */
static volatile char g_readline_buf[256];
static volatile int  g_readline_ready = 0;
static volatile int  g_readline_len   = 0;

/**
 * keyboard_readline_feed — keyboard ISR 에서 호출.
 * kscanf 사용 시 키보드 핸들러가 이 함수를 호출해야 합니다.
 */
void keyboard_readline_feed(char c) {
    static char   lbuf[256];
    static int    lidx = 0;

    if (g_readline_ready) return; /* 아직 읽히지 않은 행이 있음 */

    if (c == '\n' || c == '\r') {
        lbuf[lidx] = '\0';
        /* 버퍼 복사 */
        int i;
        for (i = 0; i <= lidx && i < 255; i++)
            g_readline_buf[i] = lbuf[i];
        g_readline_len   = lidx;
        g_readline_ready = 1;
        lidx = 0;
    } else if (c == '\b') {
        if (lidx > 0) lidx--;
    } else if (lidx < 254) {
        lbuf[lidx++] = c;
    }
}

int keyboard_readline(char *buf, int maxlen) {
    /* 이전 준비 상태 초기화 */
    g_readline_ready = 0;
    g_readline_len   = 0;

    /* 줄이 완성될 때까지 스핀 대기 (인터럽트 필요) */
    while (!g_readline_ready) {
        asm volatile("hlt");
    }

    int len = g_readline_len;
    if (len >= maxlen) len = maxlen - 1;

    int i;
    for (i = 0; i < len; i++) buf[i] = (char)g_readline_buf[i];
    buf[i] = '\0';
    g_readline_ready = 0;
    return len;
}

/* ── kscanf ───────────────────────────────────────────────────────────────── */

int kscanf(const char *fmt, ...) {
    char line[256];
    keyboard_readline(line, (int)sizeof(line));

    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf_k(line, fmt, ap);
    va_end(ap);
    return n;
}
