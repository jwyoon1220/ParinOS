/*
 * natives.c — Java 네이티브 메서드 디스패처
 *
 * .class 파일 없이 C로 직접 구현되는 java 표준 라이브러리 메서드들:
 *   java/io/PrintStream, java/lang/StringBuilder,
 *   java/lang/String, java/lang/Integer, java/lang/Math,
 *   java/lang/System,
 *   java/net/Socket, java/net/ServerSocket, java/net/InetAddress,
 *   java/io/InputStream, java/io/OutputStream (socket-backed)
 */

#include "jvm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "socket.h"

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

    /* ── java/net/InetAddress ────────────────────────────────────────
     *
     * InetAddress.getByName(String host) → InetAddress (static)
     * InetAddress.getHostAddress()       → String
     * InetAddress.getAddress()           → byte[]
     * InetAddress.isLoopbackAddress()    → boolean
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/net/InetAddress") == 0) {
        if (strcmp(name, "getByName") == 0) {
            /* static: pop String host */
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *hstr = (jobj_t *)stack[--(*sp)].ref;
            const char *hname = (hstr && hstr->type == OBJ_STRING)
                                ? hstr->str.chars : "127.0.0.1";
            uint32_t addr = 0;
            gethostbyname(hname, &addr);
            jobj_t *ia = obj_inetaddr(jvm, addr, hname);
            stack[(*sp)++] = JVAL_REF(ia);
            return 1;
        }
        if (strcmp(name, "getHostAddress") == 0 ||
            strcmp(name, "getHostName") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *ia = (jobj_t *)stack[--(*sp)].ref;
            if (ia && ia->type == OBJ_INETADDR && ia->iaddr.host[0]) {
                jobj_t *s = obj_string(jvm, ia->iaddr.host,
                                       (int)strlen(ia->iaddr.host));
                stack[(*sp)++] = JVAL_REF(s);
            } else {
                jobj_t *s = obj_string(jvm, "0.0.0.0", 7);
                stack[(*sp)++] = JVAL_REF(s);
            }
            return 1;
        }
        if (strcmp(name, "isLoopbackAddress") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *ia = (jobj_t *)stack[--(*sp)].ref;
            int lo = (ia && ia->type == OBJ_INETADDR &&
                      ia->iaddr.addr == 0x0100007fU) ? 1 : 0;
            stack[(*sp)++] = JVAL_INT(lo);
            return 1;
        }
        if (strcmp(name, "getAddress") == 0) {
            /* Returns byte[4] of IPv4 address */
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *ia  = (jobj_t *)stack[--(*sp)].ref;
            jobj_t *arr = obj_array_int(jvm, 4);
            if (arr && arr->iarr.data && ia && ia->type == OBJ_INETADDR) {
                uint32_t a = ia->iaddr.addr;
                arr->iarr.data[0] = (int32_t)(a        & 0xff);
                arr->iarr.data[1] = (int32_t)((a >> 8) & 0xff);
                arr->iarr.data[2] = (int32_t)((a>>16)  & 0xff);
                arr->iarr.data[3] = (int32_t)((a>>24)  & 0xff);
            }
            stack[(*sp)++] = JVAL_REF(arr);
            return 1;
        }
    }

    /* ── java/net/Socket ────────────────────────────────────────────
     *
     * Constructors:
     *   Socket()                           — unconnected
     *   Socket(String host, int port)      — connect immediately
     *   Socket(InetAddress addr, int port) — connect immediately
     *
     * Instance methods:
     *   connect(host:String, port:int)
     *   getInputStream()  → InputStream
     *   getOutputStream() → OutputStream
     *   close()
     *   isConnected()     → boolean
     *   getRemoteSocketAddress() → String (human-readable)
     *   setSoTimeout(int)
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/net/Socket") == 0) {
        if (strcmp(name, "<init>") == 0) {
            /* The 'new Socket' bytecode pushes a fresh OBJ_SOCKET.
             * We inspect the stack to see which constructor variant:
             *   0 args (after this): unconnected socket
             *   2 args (this, host:String, port:int)
             *   2 args (this, addr:InetAddress, port:int)
             */
            if (*sp >= 3) {
                /* Variant: (String/InetAddress, int) */
                int32_t  port    = stack[--(*sp)].ival;
                jobj_t  *hostref = (jobj_t *)stack[--(*sp)].ref;
                jobj_t  *sockobj = (jobj_t *)stack[--(*sp)].ref;

                if (sockobj && sockobj->type == OBJ_SOCKET) {
                    uint32_t addr = 0;
                    if (hostref) {
                        if (hostref->type == OBJ_STRING) {
                            gethostbyname(hostref->str.chars, &addr);
                        } else if (hostref->type == OBJ_INETADDR) {
                            addr = hostref->iaddr.addr;
                        }
                    }
                    int sfd = socket(AF_INET, SOCK_STREAM, 0);
                    if (sfd >= 0) {
                        connect(sfd, addr, (uint16_t)port);
                        sockobj->sock.sfd       = sfd;
                        sockobj->sock.connected = 1;
                    }
                }
            } else if (*sp >= 1) {
                /* Variant: () — unconnected; just pop 'this' */
                (*sp)--;
            }
            return 1;
        }
        if (strcmp(name, "connect") == 0) {
            /* connect(String host, int port) or connect(InetAddress, int) */
            if (*sp < 3) return 1;
            int32_t  port    = stack[--(*sp)].ival;
            jobj_t  *hostref = (jobj_t *)stack[--(*sp)].ref;
            jobj_t  *sockobj = (jobj_t *)stack[--(*sp)].ref;
            if (sockobj && sockobj->type == OBJ_SOCKET) {
                uint32_t addr = 0;
                if (hostref) {
                    if (hostref->type == OBJ_STRING)
                        gethostbyname(hostref->str.chars, &addr);
                    else if (hostref->type == OBJ_INETADDR)
                        addr = hostref->iaddr.addr;
                }
                if (sockobj->sock.sfd < 0) {
                    int sfd = socket(AF_INET, SOCK_STREAM, 0);
                    sockobj->sock.sfd = sfd;
                }
                if (sockobj->sock.sfd >= 0) {
                    connect(sockobj->sock.sfd, addr, (uint16_t)port);
                    sockobj->sock.connected = 1;
                }
            }
            return 1;
        }
        if (strcmp(name, "getInputStream") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *sockobj = (jobj_t *)stack[--(*sp)].ref;
            int sfd = (sockobj && sockobj->type == OBJ_SOCKET)
                      ? sockobj->sock.sfd : -1;
            stack[(*sp)++] = JVAL_REF(obj_sock_stream(jvm, sfd, 1));
            return 1;
        }
        if (strcmp(name, "getOutputStream") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *sockobj = (jobj_t *)stack[--(*sp)].ref;
            int sfd = (sockobj && sockobj->type == OBJ_SOCKET)
                      ? sockobj->sock.sfd : -1;
            stack[(*sp)++] = JVAL_REF(obj_sock_stream(jvm, sfd, 0));
            return 1;
        }
        if (strcmp(name, "close") == 0) {
            if (*sp >= 1) {
                jobj_t *sockobj = (jobj_t *)stack[--(*sp)].ref;
                if (sockobj && sockobj->type == OBJ_SOCKET &&
                    sockobj->sock.sfd >= 0) {
                    closesocket(sockobj->sock.sfd);
                    sockobj->sock.sfd       = -1;
                    sockobj->sock.connected = 0;
                }
            }
            return 1;
        }
        if (strcmp(name, "isConnected") == 0 ||
            strcmp(name, "isClosed") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_INT(0); return 1; }
            jobj_t *sockobj = (jobj_t *)stack[--(*sp)].ref;
            int result = 0;
            if (sockobj && sockobj->type == OBJ_SOCKET) {
                result = (strcmp(name, "isConnected") == 0)
                         ? sockobj->sock.connected
                         : (sockobj->sock.sfd < 0 ? 1 : 0);
            }
            stack[(*sp)++] = JVAL_INT(result);
            return 1;
        }
        if (strcmp(name, "setSoTimeout") == 0 ||
            strcmp(name, "setTcpNoDelay") == 0 ||
            strcmp(name, "setKeepAlive") == 0  ||
            strcmp(name, "setReuseAddress") == 0) {
            /* Options: accept and ignore (no real stack) */
            if (*sp >= 2) { (*sp)--; (*sp)--; }
            return 1;
        }
        if (strcmp(name, "getRemoteSocketAddress") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *sockobj = (jobj_t *)stack[--(*sp)].ref;
            (void)sockobj;
            jobj_t *s = obj_string(jvm, "unknown:0", 9);
            stack[(*sp)++] = JVAL_REF(s);
            return 1;
        }
    }

    /* ── java/net/ServerSocket ──────────────────────────────────────
     *
     * ServerSocket(int port)
     * accept() → Socket
     * close()
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/net/ServerSocket") == 0) {
        if (strcmp(name, "<init>") == 0) {
            if (*sp >= 2) {
                int32_t  port    = stack[--(*sp)].ival;
                jobj_t  *sockobj = (jobj_t *)stack[--(*sp)].ref;
                if (sockobj && sockobj->type == OBJ_SOCKET) {
                    int sfd = socket(AF_INET, SOCK_STREAM, 0);
                    if (sfd >= 0) {
                        bind(sfd, INADDR_ANY, (uint16_t)port);
                        listen(sfd, 5);
                        sockobj->sock.sfd       = sfd;
                        sockobj->sock.is_server = 1;
                    }
                }
            } else if (*sp >= 1) {
                (*sp)--;
            }
            return 1;
        }
        if (strcmp(name, "accept") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *server = (jobj_t *)stack[--(*sp)].ref;
            if (server && server->type == OBJ_SOCKET &&
                server->sock.sfd >= 0) {
                uint32_t client_addr = 0;
                uint16_t client_port = 0;
                int csfd = accept(server->sock.sfd,
                                  &client_addr, &client_port);
                jobj_t *csock = obj_socket(jvm);
                if (csock && csfd >= 0) {
                    csock->sock.sfd       = csfd;
                    csock->sock.connected = 1;
                }
                stack[(*sp)++] = JVAL_REF(csock);
            } else {
                stack[(*sp)++] = JVAL_NULL;
            }
            return 1;
        }
        if (strcmp(name, "close") == 0) {
            if (*sp >= 1) {
                jobj_t *server = (jobj_t *)stack[--(*sp)].ref;
                if (server && server->type == OBJ_SOCKET &&
                    server->sock.sfd >= 0) {
                    closesocket(server->sock.sfd);
                    server->sock.sfd = -1;
                }
            }
            return 1;
        }
        if (strcmp(name, "setSoTimeout") == 0 ||
            strcmp(name, "setReuseAddress") == 0) {
            if (*sp >= 2) { (*sp)--; (*sp)--; }
            return 1;
        }
    }

    /* ── java/io/InputStream (socket-backed) ────────────────────────
     *
     * read()          → int (one byte or -1)
     * read(byte[])    → int (bytes read or -1)
     * read(byte[],int,int) → int
     * close()
     * available()     → int (always 0)
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/io/InputStream") == 0 ||
        strcmp(cls, "java/net/SocketInputStream") == 0) {
        /* All variants share the same stream object at top/near-top */
        jobj_t *strm = (*sp >= 1) ? (jobj_t *)stack[*sp - 1].ref : (jobj_t *)0;
        int is_strm = (strm && strm->type == OBJ_SOCK_STREAM &&
                       strm->sstrm.is_input);

        if (strcmp(name, "read") == 0) {
            if (!is_strm) {
                /* Not a socket stream — pop what we have, return -1 */
                if (*sp >= 1) (*sp)--;
                stack[(*sp)++] = JVAL_INT(-1);
                return 1;
            }
            int sfd = strm->sstrm.sfd;
            /* Determine variant from descriptor */
            if (!desc || strcmp(desc, "()I") == 0) {
                /* read() → int */
                (*sp)--;
                uint8_t byte_buf[1];
                int n = (sfd >= 0) ? recv(sfd, byte_buf, 1) : -1;
                stack[(*sp)++] = JVAL_INT(n == 1 ? (int32_t)byte_buf[0] : -1);
            } else if (*sp >= 4 &&
                       strcmp(desc, "([BII)I") == 0) {
                /* read(byte[], int offset, int len) */
                int32_t  rlen   = stack[--(*sp)].ival;
                int32_t  off    = stack[--(*sp)].ival;
                jobj_t  *barr   = (jobj_t *)stack[--(*sp)].ref;
                (*sp)--;  /* pop stream */
                if (!barr || barr->type != OBJ_ARRAY_INT ||
                    off < 0 || rlen <= 0 || off + rlen > barr->iarr.len) {
                    stack[(*sp)++] = JVAL_INT(-1);
                } else {
                    /* Temp buffer on stack for recv */
                    uint8_t tmp[256];
                    int chunk = (rlen < 256) ? rlen : 256;
                    int n = (sfd >= 0) ? recv(sfd, tmp, (uint32_t)chunk) : -1;
                    if (n > 0) {
                        for (int k = 0; k < n; k++)
                            barr->iarr.data[off + k] = (int32_t)tmp[k];
                    }
                    stack[(*sp)++] = JVAL_INT(n);
                }
            } else if (*sp >= 2 &&
                       strcmp(desc, "([B)I") == 0) {
                /* read(byte[]) */
                jobj_t *barr = (jobj_t *)stack[--(*sp)].ref;
                (*sp)--;  /* pop stream */
                if (!barr || barr->type != OBJ_ARRAY_INT) {
                    stack[(*sp)++] = JVAL_INT(-1);
                } else {
                    int rlen = barr->iarr.len;
                    uint8_t tmp[256];
                    int chunk = (rlen < 256) ? rlen : 256;
                    int n = (sfd >= 0) ? recv(sfd, tmp, (uint32_t)chunk) : -1;
                    if (n > 0) {
                        for (int k = 0; k < n; k++)
                            barr->iarr.data[k] = (int32_t)tmp[k];
                    }
                    stack[(*sp)++] = JVAL_INT(n);
                }
            } else {
                /* Unrecognised variant — return -1 */
                if (*sp >= 1) (*sp)--;
                stack[(*sp)++] = JVAL_INT(-1);
            }
            return 1;
        }
        if (strcmp(name, "close") == 0) {
            if (*sp >= 1) {
                jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
                if (s && s->type == OBJ_SOCK_STREAM && s->sstrm.sfd >= 0)
                    closesocket(s->sstrm.sfd);
            }
            return 1;
        }
        if (strcmp(name, "available") == 0) {
            if (*sp >= 1) (*sp)--;
            stack[(*sp)++] = JVAL_INT(0);
            return 1;
        }
    }

    /* ── java/io/OutputStream (socket-backed) ───────────────────────
     *
     * write(int b)
     * write(byte[])
     * write(byte[], int offset, int len)
     * flush() / close()
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/io/OutputStream") == 0 ||
        strcmp(cls, "java/net/SocketOutputStream") == 0) {
        jobj_t *strm = (*sp >= 1) ? (jobj_t *)stack[*sp - 1].ref : (jobj_t *)0;
        int is_strm = (strm && strm->type == OBJ_SOCK_STREAM &&
                       !strm->sstrm.is_input);

        if (strcmp(name, "write") == 0) {
            if (!is_strm) {
                /* Drain args off stack */
                while (*sp > 0) { (*sp)--; }
                return 1;
            }
            int sfd = strm->sstrm.sfd;
            if (!desc || strcmp(desc, "(I)V") == 0) {
                /* write(int b) */
                if (*sp >= 2) {
                    uint8_t b = (uint8_t)stack[--(*sp)].ival;
                    (*sp)--;  /* pop stream */
                    if (sfd >= 0) send(sfd, &b, 1);
                }
            } else if (*sp >= 4 &&
                       strcmp(desc, "([BII)V") == 0) {
                /* write(byte[], int offset, int len) */
                int32_t wlen = stack[--(*sp)].ival;
                int32_t off  = stack[--(*sp)].ival;
                jobj_t *barr = (jobj_t *)stack[--(*sp)].ref;
                (*sp)--;  /* pop stream */
                if (barr && barr->type == OBJ_ARRAY_INT &&
                    off >= 0 && wlen > 0 && off + wlen <= barr->iarr.len &&
                    sfd >= 0) {
                    uint8_t tmp[256];
                    int32_t sent = 0;
                    while (sent < wlen) {
                        int32_t chunk = wlen - sent;
                        if (chunk > 256) chunk = 256;
                        for (int32_t k = 0; k < chunk; k++)
                            tmp[k] = (uint8_t)barr->iarr.data[off + sent + k];
                        send(sfd, tmp, (uint32_t)chunk);
                        sent += chunk;
                    }
                }
            } else if (*sp >= 2 &&
                       strcmp(desc, "([B)V") == 0) {
                /* write(byte[]) */
                jobj_t *barr = (jobj_t *)stack[--(*sp)].ref;
                (*sp)--;  /* pop stream */
                if (barr && barr->type == OBJ_ARRAY_INT && sfd >= 0) {
                    uint8_t tmp[256];
                    int32_t total = barr->iarr.len, sent = 0;
                    while (sent < total) {
                        int32_t chunk = total - sent;
                        if (chunk > 256) chunk = 256;
                        for (int32_t k = 0; k < chunk; k++)
                            tmp[k] = (uint8_t)barr->iarr.data[sent + k];
                        send(sfd, tmp, (uint32_t)chunk);
                        sent += chunk;
                    }
                }
            } else {
                while (*sp > 0) (*sp)--;
            }
            return 1;
        }
        if (strcmp(name, "flush") == 0) {
            if (*sp >= 1) (*sp)--;   /* flush is a no-op (no buffering) */
            return 1;
        }
        if (strcmp(name, "close") == 0) {
            if (*sp >= 1) {
                jobj_t *s = (jobj_t *)stack[--(*sp)].ref;
                if (s && s->type == OBJ_SOCK_STREAM && s->sstrm.sfd >= 0)
                    closesocket(s->sstrm.sfd);
            }
            return 1;
        }
    }

    /* ── java/io/BufferedReader (socket InputStream 래핑) ───────────
     *
     * BufferedReader(Reader r)  — <init>
     * readLine()                → String | null
     * close()
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/io/BufferedReader") == 0 ||
        strcmp(cls, "java/io/InputStreamReader") == 0) {
        if (strcmp(name, "<init>") == 0) {
            /* Wrap: pop arg (InputStream/Reader), keep this as stream proxy */
            if (*sp >= 2) { (*sp)--; (*sp)--; }
            return 1;
        }
        if (strcmp(name, "readLine") == 0) {
            if (*sp < 1) { stack[(*sp)++] = JVAL_NULL; return 1; }
            jobj_t *rdr = (jobj_t *)stack[--(*sp)].ref;
            if (rdr && rdr->type == OBJ_SOCK_STREAM &&
                rdr->sstrm.is_input && rdr->sstrm.sfd >= 0) {
                char line[512];
                int idx = 0;
                while (idx < 511) {
                    uint8_t b = 0;
                    int n = recv(rdr->sstrm.sfd, &b, 1);
                    if (n <= 0) break;
                    if (b == '\n') break;
                    if (b != '\r') line[idx++] = (char)b;
                }
                if (idx == 0) {
                    stack[(*sp)++] = JVAL_NULL;
                } else {
                    jobj_t *s = obj_string(jvm, line, idx);
                    stack[(*sp)++] = JVAL_REF(s);
                }
            } else {
                stack[(*sp)++] = JVAL_NULL;
            }
            return 1;
        }
        if (strcmp(name, "close") == 0) {
            if (*sp >= 1) (*sp)--;
            return 1;
        }
    }

    /* ── java/io/PrintWriter / java/io/BufferedWriter (socket output) 
     *
     * PrintWriter(OutputStream)
     * println(String) / print(String) / write(String)
     * flush() / close()
     * ──────────────────────────────────────────────────────────────── */
    if (strcmp(cls, "java/io/PrintWriter") == 0 ||
        strcmp(cls, "java/io/BufferedWriter") == 0 ||
        strcmp(cls, "java/io/OutputStreamWriter") == 0) {
        if (strcmp(name, "<init>") == 0) {
            if (*sp >= 2) { (*sp)--; (*sp)--; }
            return 1;
        }
        int is_pln = (strcmp(name, "println") == 0);
        int is_prt = (strcmp(name, "print")   == 0);
        int is_wrt = (strcmp(name, "write")   == 0);
        if (is_pln || is_prt || is_wrt) {
            if (*sp < 2) { if (*sp >= 1) (*sp)--; return 1; }
            jobj_t *sobj = (jobj_t *)stack[*sp - 2].ref;
            jobj_t *arg  = (jobj_t *)stack[*sp - 1].ref;
            (*sp) -= 2;
            int sfd = -1;
            if (sobj && sobj->type == OBJ_SOCK_STREAM)
                sfd = sobj->sstrm.sfd;
            if (sfd >= 0 && arg && arg->type == OBJ_STRING) {
                send(sfd, arg->str.chars, (uint32_t)arg->str.len);
                if (is_pln) {
                    const char *nl = "\r\n";
                    send(sfd, nl, 2);
                }
            }
            return 1;
        }
        if (strcmp(name, "flush") == 0 || strcmp(name, "close") == 0) {
            if (*sp >= 1) (*sp)--;
            return 1;
        }
    }

    return 0;  /* 미처리 */
}
