/*
 * jvm.h — ParinOS Tiny JVM 공통 타입 정의
 *
 * Java 8 스타일 .class 파일(모듈 없음)을 해석하는
 * 인터프리터 + x86-32 템플릿 JIT 컴파일러의 공유 헤더.
 */

#ifndef JVM_H
#define JVM_H

#include <stdint.h>
#include <stddef.h>

/* ── 값 태그 ─────────────────────────────────────────────────────────── */
#define TAG_INT  0   /* int, boolean, byte, short, char */
#define TAG_REF  1   /* 힙 객체 참조 (NULL = Java null) */

/* ── Java 값 (operand stack / local variable 슬롯) ───────────────────── */
typedef struct {
    int32_t  ival;
    void    *ref;
    uint8_t  tag;
} jval_t;

static inline jval_t jval_int(int32_t n) {
    jval_t v; v.ival = n; v.ref = (void *)0; v.tag = TAG_INT; return v;
}
static inline jval_t jval_ref(void *p) {
    jval_t v; v.ival = 0; v.ref = p; v.tag = TAG_REF; return v;
}
#define JVAL_INT(n)  jval_int(n)
#define JVAL_REF(p)  jval_ref(p)
#define JVAL_NULL    jval_ref((void *)0)

/* ── 힙 객체 종류 ─────────────────────────────────────────────────────── */
#define OBJ_STRING      1
#define OBJ_ARRAY_INT   2
#define OBJ_ARRAY_REF   3
#define OBJ_INSTANCE    4
#define OBJ_PRINTSTREAM 5
#define OBJ_STRINGBLD   6
#define OBJ_SOCKET      7   /* java.net.Socket / java.net.ServerSocket */
#define OBJ_INETADDR    8   /* java.net.InetAddress                    */
#define OBJ_SOCK_STREAM 9   /* java.io.{Input,Output}Stream via socket */

struct class_info;

typedef struct jobj {
    uint8_t  type;
    union {
        struct { char    *chars; int len; }          str;   /* OBJ_STRING     */
        struct { int32_t *data;  int len; }          iarr;  /* OBJ_ARRAY_INT  */
        struct { struct jobj **data; int len; }      rarr;  /* OBJ_ARRAY_REF  */
        struct {                                             /* OBJ_INSTANCE   */
            struct class_info *klass;
            jval_t            *fields;
        } inst;
        struct { char *buf; int len; int cap; }      sbld;  /* OBJ_STRINGBLD  */
        struct {                                            /* OBJ_SOCKET      */
            int sfd;        /* kernel socket FD (< 0 = not yet open) */
            int connected;  /* 1 = connected/accepted                */
            int is_server;  /* 1 = ServerSocket                      */
        } sock;
        struct { uint32_t addr; char host[64]; }    iaddr; /* OBJ_INETADDR    */
        struct {                                            /* OBJ_SOCK_STREAM */
            int sfd;       /* kernel socket FD                        */
            int is_input;  /* 1 = InputStream, 0 = OutputStream       */
        } sstrm;
    };
} jobj_t;

/* ── 상수 풀 엔트리 태그 ──────────────────────────────────────────────── */
#define CP_UTF8      1
#define CP_INTEGER   3
#define CP_CLASS     7
#define CP_STRING    8
#define CP_FIELDREF  9
#define CP_METHODREF 10
#define CP_IFACEREF  11
#define CP_NAMETYPE  12

typedef struct {
    uint8_t  tag;
    union {
        char    *utf8;
        int32_t  ival;
        uint16_t class_idx;
        uint16_t string_idx;
        struct { uint16_t cls; uint16_t nat; } ref;
        struct { uint16_t name; uint16_t desc; } nat;
    };
} cp_entry_t;

/* ── 메서드 정보 ──────────────────────────────────────────────────────── */
#define ACC_STATIC    0x0008
#define ACC_NATIVE    0x0100
#define JIT_THRESHOLD 100   /* 인터프리터 호출 횟수 → JIT 컴파일 트리거 */

typedef struct method_info {
    char    *name;
    char    *descriptor;
    uint16_t access_flags;
    uint8_t *code;        /* Code 속성 내 바이트코드 포인터 (raw 버퍼 참조) */
    uint16_t code_len;
    uint16_t max_stack;
    uint16_t max_locals;
    /* JIT */
    uint32_t call_count;
    void    *jit_code;    /* NULL = 미컴파일 */
} method_info_t;

/* ── 필드 정보 ───────────────────────────────────────────────────────── */
typedef struct {
    char    *name;
    char    *descriptor;
    uint16_t access_flags;
    jval_t   static_val;
} field_info_t;

/* ── 클래스 ──────────────────────────────────────────────────────────── */
#define MAX_CP       512
#define MAX_METHODS   64
#define MAX_FIELDS    64

typedef struct class_info {
    char          *name;
    cp_entry_t     cp[MAX_CP];
    int            cp_count;
    method_info_t  methods[MAX_METHODS];
    int            method_count;
    field_info_t   fields[MAX_FIELDS];
    int            field_count;
    uint8_t       *raw;   /* 원본 .class 데이터 (owned) */
} class_info_t;

/* ── 인터프리터 프레임 ────────────────────────────────────────────────── */
#define MAX_STACK   64
#define MAX_LOCALS  64

typedef struct frame {
    class_info_t  *klass;
    method_info_t *method;
    jval_t         locals[MAX_LOCALS];
    jval_t         stack[MAX_STACK];
    int            sp;
    int            pc;
} frame_t;

/* ── 클래스 로더 ──────────────────────────────────────────────────────── */
#define MAX_CLASSES 32

typedef struct {
    class_info_t *classes[MAX_CLASSES];
    int           count;
    char          classpath[256];
} classloader_t;

/* ── JVM 전역 상태 ────────────────────────────────────────────────────── */
typedef struct {
    classloader_t  loader;
    jobj_t        *system_out;   /* java.io.PrintStream 싱글턴 */
    uint8_t       *heap;
    uint8_t       *heap_ptr;
    uint8_t       *heap_end;
    uint8_t       *jit_buf;      /* 실행 가능 JIT 코드 버퍼 */
    uint8_t       *jit_ptr;
    uint8_t       *jit_end;
} jvm_t;

/* ── 전방 선언 ───────────────────────────────────────────────────────── */

/* heap.c */
void    heap_init(jvm_t *jvm, size_t heap_sz, size_t jit_sz);
void   *heap_alloc(jvm_t *jvm, size_t sz);
jobj_t *obj_string(jvm_t *jvm, const char *s, int len);
jobj_t *obj_stringbld(jvm_t *jvm);
jobj_t *obj_array_int(jvm_t *jvm, int len);
jobj_t *obj_array_ref(jvm_t *jvm, int len);
jobj_t *obj_instance(jvm_t *jvm, class_info_t *klass);
jobj_t *obj_socket(jvm_t *jvm);
jobj_t *obj_inetaddr(jvm_t *jvm, uint32_t addr, const char *host);
jobj_t *obj_sock_stream(jvm_t *jvm, int sfd, int is_input);

/* classfile.c */
class_info_t  *classfile_load(jvm_t *jvm, const char *path);
class_info_t  *classloader_resolve(jvm_t *jvm, const char *name);
const char    *cp_utf8(class_info_t *klass, int idx);
method_info_t *class_find_method(class_info_t *klass,
                                  const char *name, const char *desc);
field_info_t  *class_find_field(class_info_t *klass, const char *name);

/* interp.c */
jval_t interp_exec(jvm_t *jvm, class_info_t *klass, method_info_t *method,
                   jval_t *args, int argc);

/* jit.c */
int    jit_can_compile(method_info_t *method);
void   jit_compile(jvm_t *jvm, class_info_t *klass, method_info_t *method);
jval_t jit_call(jvm_t *jvm, method_info_t *method, jval_t *args, int argc);

/* natives.c */
int native_invoke(jvm_t *jvm, const char *cls, const char *name,
                  const char *desc, jval_t *stack, int *sp);

/* main.c */
extern jvm_t g_jvm;

#endif /* JVM_H */
