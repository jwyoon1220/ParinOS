/*
 * heap.c — JVM 객체 힙
 *
 * 범프 포인터(bump-pointer) 방식 단순 할당기.
 * GC 없음 - 단기 실행 프로그램 용도.
 */

#include "jvm.h"
#include "stdlib.h"
#include "string.h"

/* ── heap_init ───────────────────────────────────────────────────────── */
void heap_init(jvm_t *jvm, size_t heap_sz, size_t jit_sz) {
    jvm->heap     = (uint8_t *)malloc(heap_sz);
    jvm->heap_ptr = jvm->heap;
    jvm->heap_end = jvm->heap ? jvm->heap + heap_sz : jvm->heap;

    jvm->jit_buf  = (uint8_t *)malloc(jit_sz);
    jvm->jit_ptr  = jvm->jit_buf;
    jvm->jit_end  = jvm->jit_buf ? jvm->jit_buf + jit_sz : jvm->jit_buf;
}

/* ── heap_alloc: 4바이트 정렬 범프 할당 ─────────────────────────────── */
void *heap_alloc(jvm_t *jvm, size_t sz) {
    sz = (sz + 3u) & ~3u;
    if (!jvm->heap || jvm->heap_ptr + sz > jvm->heap_end) {
        return (void *)0;
    }
    void *p = jvm->heap_ptr;
    jvm->heap_ptr += sz;
    memset(p, 0, sz);
    return p;
}

/* ── obj_string ──────────────────────────────────────────────────────── */
jobj_t *obj_string(jvm_t *jvm, const char *s, int len) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type = OBJ_STRING;
    char *buf = (char *)heap_alloc(jvm, (size_t)(len + 1));
    if (!buf) return (jobj_t *)0;
    if (s && len > 0) memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    o->str.chars = buf;
    o->str.len   = len;
    return o;
}

/* ── obj_stringbld ───────────────────────────────────────────────────── */
jobj_t *obj_stringbld(jvm_t *jvm) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type = OBJ_STRINGBLD;
    o->sbld.buf = (char *)heap_alloc(jvm, 256);
    o->sbld.cap = 256;
    o->sbld.len = 0;
    if (o->sbld.buf) o->sbld.buf[0] = '\0';
    return o;
}

/* ── obj_array_int ───────────────────────────────────────────────────── */
jobj_t *obj_array_int(jvm_t *jvm, int len) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type = OBJ_ARRAY_INT;
    o->iarr.data = (int32_t *)heap_alloc(jvm, (size_t)len * sizeof(int32_t));
    o->iarr.len  = len;
    return o;
}

/* ── obj_array_ref ───────────────────────────────────────────────────── */
jobj_t *obj_array_ref(jvm_t *jvm, int len) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type = OBJ_ARRAY_REF;
    o->rarr.data = (jobj_t **)heap_alloc(jvm,
                        (size_t)len * sizeof(jobj_t *));
    o->rarr.len  = len;
    return o;
}

/* ── obj_instance ────────────────────────────────────────────────────── */
jobj_t *obj_instance(jvm_t *jvm, class_info_t *klass) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type = OBJ_INSTANCE;
    o->inst.klass = klass;
    if (klass && klass->field_count > 0) {
        o->inst.fields = (jval_t *)heap_alloc(jvm,
                            (size_t)klass->field_count * sizeof(jval_t));
    } else {
        o->inst.fields = (jval_t *)0;
    }
    return o;
}

/* ── obj_socket ──────────────────────────────────────────────────────── */
jobj_t *obj_socket(jvm_t *jvm) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type          = OBJ_SOCKET;
    o->sock.sfd       = -1;
    o->sock.connected = 0;
    o->sock.is_server = 0;
    return o;
}

/* ── obj_inetaddr ────────────────────────────────────────────────────── */
jobj_t *obj_inetaddr(jvm_t *jvm, uint32_t addr, const char *host) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type      = OBJ_INETADDR;
    o->iaddr.addr = addr;
    if (host) {
        int i;
        for (i = 0; i < 63 && host[i]; i++) o->iaddr.host[i] = host[i];
        o->iaddr.host[i] = '\0';
    } else {
        o->iaddr.host[0] = '\0';
    }
    return o;
}

/* ── obj_sock_stream ─────────────────────────────────────────────────── */
jobj_t *obj_sock_stream(jvm_t *jvm, int sfd, int is_input) {
    jobj_t *o = (jobj_t *)heap_alloc(jvm, sizeof(jobj_t));
    if (!o) return (jobj_t *)0;
    o->type         = OBJ_SOCK_STREAM;
    o->sstrm.sfd      = sfd;
    o->sstrm.is_input = is_input;
    return o;
}
