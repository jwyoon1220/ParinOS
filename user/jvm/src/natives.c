/*
 * natives.c — Java 네이티브 메서드 디스패처
 *
 * .class 파일 없이 C로 직접 구현되는 java 표준 라이브러리 메서드들:
 *   java/io/PrintStream, java/lang/StringBuilder,
 *   java/lang/String, java/lang/Integer, java/lang/Math,
 *   java/lang/System
 */

#include "jvm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* ── int → 10진수 문자열 ──────────────────────────────────────────── */
static int int_to_str(int32_t n, char *buf, int bufsz) {
    if (bufsz < 2) return 0;
    if (n == (int32_t)0x80000000) {
        const char *s = "-2147483648";
        int l = 11;
        if (l >= bufsz) l = bufsz - 1;
        memcpy(buf, s, (size_t)l);
        buf[l] = '\0';
        return l;
    }
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    char tmp[12]; int len = 0;
    if (n == 0) { tmp[len++] = '0'; }
    else { while (n) { tmp[len++] = (char)('0' + n % 10); n /= 10; } }
    if (neg) tmp[len++] = '-';
    int total = len;
    for (int i = 0; i < total; i++) buf[i] = tmp[total - 1 - i];
    buf[total] = '\0';
    return total;
}

/* ── StringBuilder append 헬퍼 ────────────────────────────────────── */
static void sbld_append_str(jobj_t *sb, const char *s, int slen) {
    if (!sb || !sb->sbld.buf) return;
    int avail = sb->sbld.cap - sb->sbld.len - 1;
    int copy  = (slen < avail) ? slen : avail;
    if (copy > 0) {
        memcpy(sb->sbld.buf + sb->sbld.len, s, (size_t)copy);
        sb->sbld.len += copy;
        sb->sbld.buf[sb->sbld.len] = '\0';
    }
}

/* ── native_invoke ───────────────────────────────────────────────────
 * 반환: 1 = 처리됨(스택 조작 완료), 0 = 미처리(스택 불변)
 * 호출자의 stack/sp 를 직접 조작한다.
 * ────────────────────────────────────────────────────────────────── */
int native_invoke(jvm_t *jvm, const char *cls, const char *name,
                  const char *desc, jval_t *stack, int *sp) {
    (void)jvm; (void)desc;

    /* ── java/io/PrintStream ─────────────────────────────────────── */
    if (strcmp(cls, "java/io/PrintStream") == 0) {
        int is_println = (strcmp(name, "println") == 0);
        int is_print   = (strcmp(name, "print")   == 0);
        if (is_println || is_print) {
            /* 스택: [..., ps_ref, arg]  또는  [..., ps_ref] (인자 없는 println) */
            if (*sp >= 2) {
                jval_t arg = stack[*sp - 1]; (*sp)--;
                (*sp)--; /* PrintStream ref 팝 */
                if (arg.tag == TAG_INT) {
                    char buf[24];
                    int  len = int_to_str(arg.ival, buf, (int)sizeof(buf));
                    write(STDOUT_FILENO, buf, len);
                } else if (arg.tag == TAG_REF && arg.ref) {
                    jobj_t *o = (jobj_t *)arg.ref;
                    if (o->type == OBJ_STRING)
                        write(STDOUT_FILENO, o->str.chars, o->str.len);
                    else if (o->type == OBJ_STRINGBLD)
                        write(STDOUT_FILENO, o->sbld.buf, o->sbld.len);
                    else
                        write(STDOUT_FILENO, "Object@?\n", 9);
                } else {
                    write(STDOUT_FILENO, "null", 4);
                }
            } else if (*sp >= 1) {
                (*sp)--; /* PrintStream ref만 팝 */
            }
            if (is_println) write(STDOUT_FILENO, "\n", 1);
            return 1;
        }
        if (strcmp(name, "flush") == 0 || strcmp(name, "close") == 0) {
            if (*sp >= 1) (*sp)--;
            return 1;
        }
    }

    /* ── java/lang/StringBuilder ─────────────────────────────────── */
    if (strcmp(cls, "java/lang/StringBuilder") == 0) {
        if (strcmp(name, "<init>") == 0) {
            /* new StringBuilder() 후 invokespecial: this는 스택에 있음 */
            if (*sp >= 1) (*sp)--;
            return 1;
        }
        if (strcmp(name, "append") == 0) {
            if (*sp < 2) return 1;
            jval_t arg  = stack[*sp - 1]; (*sp)--;
            jobj_t *sb  = (jobj_t *)stack[*sp - 1].ref; /* this 유지 (체이닝) */
            if (sb && sb->type == OBJ_STRINGBLD) {
                if (arg.tag == TAG_INT) {
                    char tmp[24];
                    int  len = int_to_str(arg.ival, tmp, (int)sizeof(tmp));
                    sbld_append_str(sb, tmp, len);
                } else if (arg.tag == TAG_REF && arg.ref) {
                    jobj_t *s = (jobj_t *)arg.ref;
                    if (s->type == OBJ_STRING)
                        sbld_append_str(sb, s->str.chars, s->str.len);
                }
            }
            /* this를 반환값으로 스택에 유지 */
            return 1;
        }
        if (strcmp(name, "toString") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *sb = (jobj_t *)stack[--(*sp)].ref;
            if (sb && sb->type == OBJ_STRINGBLD) {
                jobj_t *str = obj_string(jvm, sb->sbld.buf, sb->sbld.len);
                stack[(*sp)++] = JVAL_REF(str);
            } else {
                stack[(*sp)++] = JVAL_NULL;
            }
            return 1;
        }
        if (strcmp(name, "length") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *sb = (jobj_t *)stack[--(*sp)].ref;
            int l = (sb && sb->type == OBJ_STRINGBLD) ? sb->sbld.len : 0;
            stack[(*sp)++] = JVAL_INT(l);
            return 1;
        }
        if (strcmp(name, "insert") == 0) {
            /* 단순화: 무시하고 this 반환 */
            if (*sp >= 2) (*sp)--;
            return 1;
        }
    }

    /* ── java/lang/String ────────────────────────────────────────── */
    if (strcmp(cls, "java/lang/String") == 0) {
        if (strcmp(name, "length") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
            int l = (s && s->type == OBJ_STRING) ? s->str.len : 0;
            stack[(*sp)++] = JVAL_INT(l);
            return 1;
        }
        if (strcmp(name, "charAt") == 0) {
            if (*sp < 2) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jval_t idx = stack[--(*sp)];
            jobj_t *s  = (jobj_t *)stack[--(*sp)].ref;
            if (s && s->type == OBJ_STRING &&
                idx.ival >= 0 && idx.ival < s->str.len)
                stack[(*sp)++] = JVAL_INT((unsigned char)s->str.chars[idx.ival]);
            else
                stack[(*sp)++] = JVAL_INT(0);
            return 1;
        }
        if (strcmp(name, "equals") == 0 || strcmp(name, "equalsIgnoreCase") == 0) {
            if (*sp < 2) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *b = (jobj_t *)stack[--(*sp)].ref;
            jobj_t *a = (jobj_t *)stack[--(*sp)].ref;
            int eq = 0;
            if (a && b && a->type == OBJ_STRING && b->type == OBJ_STRING)
                eq = (strcmp(a->str.chars, b->str.chars) == 0) ? 1 : 0;
            else
                eq = (a == b) ? 1 : 0;
            stack[(*sp)++] = JVAL_INT(eq);
            return 1;
        }
        if (strcmp(name, "valueOf") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jval_t v = stack[--(*sp)];
            if (v.tag == TAG_INT) {
                char buf[24];
                int len = int_to_str(v.ival, buf, (int)sizeof(buf));
                stack[(*sp)++] = JVAL_REF(obj_string(jvm, buf, len));
            } else {
                stack[(*sp)++] = v;   /* 이미 String ref */
            }
            return 1;
        }
        if (strcmp(name, "concat") == 0) {
            if (*sp < 2) return 1;
            jobj_t *b = (jobj_t *)stack[--(*sp)].ref;
            jobj_t *a = (jobj_t *)stack[--(*sp)].ref;
            if (a && b && a->type == OBJ_STRING && b->type == OBJ_STRING) {
                int nl = a->str.len + b->str.len;
                jobj_t *r = obj_string(jvm, (char *)0, nl);
                if (r) {
                    memcpy(r->str.chars, a->str.chars, (size_t)a->str.len);
                    memcpy(r->str.chars + a->str.len,
                           b->str.chars, (size_t)b->str.len);
                    r->str.chars[nl] = '\0';
                }
                stack[(*sp)++] = JVAL_REF(r);
            } else {
                stack[(*sp)++] = JVAL_REF(a);
            }
            return 1;
        }
        if (strcmp(name, "substring") == 0) {
            /* substring(int begin) or substring(int begin, int end) */
            if (*sp < 2) return 1;
            int end_given = (*sp >= 3 &&
                             stack[*sp-3].tag == TAG_REF &&
                             stack[*sp-2].tag == TAG_INT &&
                             stack[*sp-1].tag == TAG_INT);
            int end_idx = 0, beg_idx = 0;
            if (end_given) {
                end_idx = stack[--(*sp)].ival;
                beg_idx = stack[--(*sp)].ival;
            } else {
                beg_idx = stack[--(*sp)].ival;
            }
            jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
            if (s && s->type == OBJ_STRING) {
                if (!end_given) end_idx = s->str.len;
                if (beg_idx < 0) beg_idx = 0;
                if (end_idx > s->str.len) end_idx = s->str.len;
                int l = end_idx - beg_idx;
                if (l < 0) l = 0;
                jobj_t *r = obj_string(jvm, s->str.chars + beg_idx, l);
                stack[(*sp)++] = JVAL_REF(r);
            } else {
                stack[(*sp)++] = JVAL_NULL;
            }
            return 1;
        }
        if (strcmp(name, "isEmpty") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(1); return 1; }
            jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
            int e = (!s || s->type != OBJ_STRING || s->str.len == 0) ? 1 : 0;
            stack[(*sp)++] = JVAL_INT(e);
            return 1;
        }
    }

    /* ── java/lang/Integer ───────────────────────────────────────── */
    if (strcmp(cls, "java/lang/Integer") == 0) {
        if (strcmp(name, "parseInt") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
            int32_t v = 0;
            if (s && s->type == OBJ_STRING) v = (int32_t)atoi(s->str.chars);
            stack[(*sp)++] = JVAL_INT(v);
            return 1;
        }
        if (strcmp(name, "toString") == 0 || strcmp(name, "valueOf") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            int32_t v = stack[--(*sp)].ival;
            char buf[24]; int l = int_to_str(v, buf, (int)sizeof(buf));
            stack[(*sp)++] = JVAL_REF(obj_string(jvm, buf, l));
            return 1;
        }
    }

    /* ── java/lang/Math ──────────────────────────────────────────── */
    if (strcmp(cls, "java/lang/Math") == 0) {
        if (strcmp(name, "max") == 0) {
            if (*sp < 2) return 1;
            int32_t b = stack[--(*sp)].ival, a = stack[--(*sp)].ival;
            stack[(*sp)++] = JVAL_INT(a >= b ? a : b);
            return 1;
        }
        if (strcmp(name, "min") == 0) {
            if (*sp < 2) return 1;
            int32_t b = stack[--(*sp)].ival, a = stack[--(*sp)].ival;
            stack[(*sp)++] = JVAL_INT(a <= b ? a : b);
            return 1;
        }
        if (strcmp(name, "abs") == 0) {
            if (*sp < 1) return 1;
            int32_t a = stack[--(*sp)].ival;
            stack[(*sp)++] = JVAL_INT(a < 0 ? -a : a);
            return 1;
        }
    }

    /* ── java/lang/System ────────────────────────────────────────── */
    if (strcmp(cls, "java/lang/System") == 0) {
        if (strcmp(name, "exit") == 0) {
            int32_t code = (*sp >= 1) ? stack[--(*sp)].ival : 0;
            exit(code);
            return 1;
        }
        if (strcmp(name, "currentTimeMillis") == 0) {
            stack[(*sp)++] = JVAL_INT(0);
            return 1;
        }
        if (strcmp(name, "arraycopy") == 0) {
            if (*sp < 5) { while (*sp > 0) (*sp)--; return 1; }
            int32_t  len  = stack[--(*sp)].ival;
            int32_t  dpos = stack[--(*sp)].ival;
            jobj_t  *dst  = (jobj_t *)stack[--(*sp)].ref;
            int32_t  spos = stack[--(*sp)].ival;
            jobj_t  *src  = (jobj_t *)stack[--(*sp)].ref;
            if (src && dst &&
                src->type == OBJ_ARRAY_INT && dst->type == OBJ_ARRAY_INT) {
                for (int32_t k = 0; k < len; k++) {
                    if (spos+k < src->iarr.len && dpos+k < dst->iarr.len)
                        dst->iarr.data[dpos+k] = src->iarr.data[spos+k];
                }
            } else if (src && dst &&
                       src->type == OBJ_ARRAY_REF &&
                       dst->type == OBJ_ARRAY_REF) {
                for (int32_t k = 0; k < len; k++) {
                    if (spos+k < src->rarr.len && dpos+k < dst->rarr.len)
                        dst->rarr.data[dpos+k] = src->rarr.data[spos+k];
                }
            }
            return 1;
        }
    }

    /* ── java/lang/Object ────────────────────────────────────────── */
    if (strcmp(cls, "java/lang/Object") == 0) {
        if (strcmp(name, "<init>") == 0) {
            if (*sp >= 1) (*sp)--;
            return 1;
        }
        if (strcmp(name, "toString") == 0) {
            if (*sp >= 1) {
                jobj_t *o = (jobj_t *)stack[--(*sp)].ref;
                if (o && o->type == OBJ_STRING) {
                    stack[(*sp)++] = JVAL_REF(o);
                } else {
                    jobj_t *s = obj_string(jvm, "Object", 6);
                    stack[(*sp)++] = JVAL_REF(s);
                }
            }
            return 1;
        }
    }

    return 0;  /* 미처리 */
}
