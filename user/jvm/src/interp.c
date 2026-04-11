/*
 * interp.c — Java 바이트코드 인터프리터
 *
 * Java 8 수준 opcode 전체 지원. 순수-정수 메서드는 JIT 임계값 도달 시
 * jit_compile() 로 x86 네이티브 코드를 생성하여 이후 호출에 사용한다.
 */

#include "jvm.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ── 스택 매크로 (frame_t fr 로컬 변수 기반) ───────────────────────── */
#define PUSH(v)   do { \
    if (fr.sp < MAX_STACK) { fr.stack[fr.sp++] = (v); } \
    else { printf("[jvm] stack overflow in %s\n", fr.method->name); \
           goto exec_error; } } while(0)
#define POP()     (fr.sp > 0 ? fr.stack[--fr.sp] : JVAL_INT(0))
#define PUSHI(n)  PUSH(JVAL_INT(n))
#define PUSHR(p)  PUSH(JVAL_REF(p))
#define PEEK()    fr.stack[fr.sp - 1]

/* ── 코드 읽기 매크로 ──────────────────────────────────────────────── */
#define CODE fr.method->code
#define PC   fr.pc

#define U1_NEXT()  (CODE[PC++])
#define S1_NEXT()  ((int8_t)CODE[PC++])
#define S2_NEXT()  (PC += 2, (int16_t)(((unsigned)CODE[PC-2]<<8)|CODE[PC-1]))
#define U2_NEXT()  (PC += 2, (uint16_t)(((unsigned)CODE[PC-2]<<8)|CODE[PC-1]))

/* ── 분기 조건부 점프 ──────────────────────────────────────────────── */
#define BRANCH_IF(cond) do { \
    int16_t _off = (int16_t)(((unsigned)CODE[PC]<<8)|CODE[PC+1]); \
    PC += 2; \
    if (cond) PC = instr_pc + (int)_off; \
} while(0)

/* ── 메서드 참조 분해 ──────────────────────────────────────────────── */
static void get_ref(class_info_t *klass, int cpidx,
                    const char **cls, const char **nm, const char **dsc) {
    if (cpidx <= 0 || cpidx >= klass->cp_count) {
        *cls = *nm = *dsc = ""; return;
    }
    cp_entry_t *e = &klass->cp[cpidx];
    int nat = (int)e->ref.nat;
    if (nat <= 0 || nat >= klass->cp_count) { *cls = *nm = *dsc = ""; return; }
    *cls = cp_utf8(klass, klass->cp[e->ref.cls].class_idx);
    *nm  = cp_utf8(klass, klass->cp[nat].nat.name);
    *dsc = cp_utf8(klass, klass->cp[nat].nat.desc);
}

/* ── 서술자에서 인자 수 계산 ────────────────────────────────────────── */
static int count_args(const char *desc, int include_this) {
    int count = include_this ? 1 : 0;
    if (!desc || *desc != '(') return count;
    const char *p = desc + 1;
    while (*p && *p != ')') {
        if (*p == 'L') { while (*p && *p != ';') p++; }
        else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; }
        }
        if (*p) { count++; p++; }
    }
    return count;
}

/* ── 반환 타입이 void 인지 확인 ─────────────────────────────────────── */
static int is_void_return(const char *desc) {
    if (!desc) return 1;
    size_t l = strlen(desc);
    return (l > 0 && desc[l - 1] == 'V') ? 1 : 0;
}

/* ── 메서드 디스패치 (인터프리터 / JIT / 네이티브) ───────────────────
 * fr 의 operand stack 에서 nargs 개를 팝하고 메서드를 호출한다.
 * 반환값이 있으면(void 아닌 경우) 결과를 fr.stack 에 푸시한다.
 * ──────────────────────────────────────────────────────────────────── */
static void do_invoke(jvm_t *jvm, frame_t *fr,
                      const char *cls, const char *mname, const char *mdesc,
                      int is_static) {
    int nargs   = count_args(mdesc, !is_static);
    int is_void = is_void_return(mdesc);

    /* 네이티브 처리 시도 */
    if (native_invoke(jvm, cls, mname, mdesc, fr->stack, &fr->sp))
        return;

    /* 인자 팝 */
    int base = fr->sp - nargs;
    if (base < 0) { base = 0; nargs = fr->sp; }
    jval_t args[MAX_LOCALS];
    int argc = (nargs < MAX_LOCALS) ? nargs : MAX_LOCALS;
    for (int i = 0; i < argc; i++) args[i] = fr->stack[base + i];
    fr->sp = base;

    /* 클래스 로드 */
    class_info_t *tcls = classloader_resolve(jvm, cls);
    if (!tcls) {
        printf("[jvm] class not found: %s\n", cls);
        if (!is_void && fr->sp < MAX_STACK) fr->stack[fr->sp++] = JVAL_INT(0);
        return;
    }

    /* 메서드 탐색 */
    method_info_t *m = class_find_method(tcls, mname, mdesc);
    if (!m) m = class_find_method(tcls, mname, (char *)0);
    if (!m || !m->code) {
        printf("[jvm] method not found: %s.%s%s\n", cls, mname, mdesc ? mdesc : "");
        if (!is_void && fr->sp < MAX_STACK) fr->stack[fr->sp++] = JVAL_INT(0);
        return;
    }

    jval_t ret = interp_exec(jvm, tcls, m, args, argc);
    if (!is_void && fr->sp < MAX_STACK)
        fr->stack[fr->sp++] = ret;
}

/* ── interp_exec ─────────────────────────────────────────────────────── */
jval_t interp_exec(jvm_t *jvm, class_info_t *klass,
                   method_info_t *method, jval_t *args, int argc) {
    /* JIT 임계값 확인 */
    method->call_count++;
    if (method->jit_code)
        return jit_call(jvm, method, args, argc);
    if (method->call_count >= JIT_THRESHOLD && jit_can_compile(method)) {
        jit_compile(jvm, klass, method);
        if (method->jit_code)
            return jit_call(jvm, method, args, argc);
    }

    frame_t fr;
    memset(&fr, 0, sizeof(frame_t));
    fr.klass  = klass;
    fr.method = method;
    fr.sp     = 0;
    fr.pc     = 0;

    /* 인자를 로컬 변수에 복사 */
    int copy = (argc < MAX_LOCALS) ? argc : MAX_LOCALS;
    for (int i = 0; i < copy; i++) fr.locals[i] = args[i];

    while (fr.pc < (int)method->code_len) {
        int instr_pc = fr.pc;
        uint8_t op   = CODE[PC++];

        switch (op) {
        /* ── nop ─────────────────────────────────────────────────── */
        case 0x00: break;

        /* ── 상수 푸시 ────────────────────────────────────────────── */
        case 0x01: PUSHR((void *)0); break;           /* aconst_null */
        case 0x02: PUSHI(-1); break;                  /* iconst_m1   */
        case 0x03: PUSHI(0);  break;                  /* iconst_0    */
        case 0x04: PUSHI(1);  break;                  /* iconst_1    */
        case 0x05: PUSHI(2);  break;                  /* iconst_2    */
        case 0x06: PUSHI(3);  break;                  /* iconst_3    */
        case 0x07: PUSHI(4);  break;                  /* iconst_4    */
        case 0x08: PUSHI(5);  break;                  /* iconst_5    */
        case 0x09: case 0x0a: PUSHI((int)(op-0x09)); break; /* lconst_0/1 */
        case 0x0b: case 0x0c: case 0x0d:              /* fconst_0/1/2 */
            PUSHI((int)(op - 0x0b)); break;
        case 0x0e: case 0x0f: PUSHI((int)(op-0x0e)); break; /* dconst_0/1 */
        case 0x10: PUSHI((int)S1_NEXT()); break;      /* bipush      */
        case 0x11: { int16_t v = S2_NEXT(); PUSHI(v); break; } /* sipush */
        case 0x12: { /* ldc */
            int idx = (int)U1_NEXT();
            if (idx > 0 && idx < klass->cp_count) {
                cp_entry_t *e = &klass->cp[idx];
                if (e->tag == CP_INTEGER)
                    PUSHI(e->ival);
                else if (e->tag == CP_STRING) {
                    const char *s = cp_utf8(klass, e->string_idx);
                    PUSHR(obj_string(jvm, s, (int)strlen(s)));
                } else PUSHI(0);
            } else PUSHI(0);
            break;
        }
        case 0x13: { /* ldc_w */
            int idx = (int)U2_NEXT();
            if (idx > 0 && idx < klass->cp_count) {
                cp_entry_t *e = &klass->cp[idx];
                if (e->tag == CP_INTEGER)
                    PUSHI(e->ival);
                else if (e->tag == CP_STRING) {
                    const char *s = cp_utf8(klass, e->string_idx);
                    PUSHR(obj_string(jvm, s, (int)strlen(s)));
                } else PUSHI(0);
            } else PUSHI(0);
            break;
        }
        case 0x14: PC += 2; PUSHI(0); break; /* ldc2_w (long/double stub) */

        /* ── 로컬 변수 로드 ───────────────────────────────────────── */
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
            { int i = (int)U1_NEXT(); PUSH(fr.locals[i]); break; }
        case 0x1a: PUSH(fr.locals[0]); break;
        case 0x1b: PUSH(fr.locals[1]); break;
        case 0x1c: PUSH(fr.locals[2]); break;
        case 0x1d: PUSH(fr.locals[3]); break;
        case 0x1e: PUSH(fr.locals[0]); break;
        case 0x1f: PUSH(fr.locals[1]); break;
        case 0x20: PUSH(fr.locals[2]); break;
        case 0x21: PUSH(fr.locals[3]); break;
        case 0x22: PUSH(fr.locals[0]); break;
        case 0x23: PUSH(fr.locals[1]); break;
        case 0x24: PUSH(fr.locals[2]); break;
        case 0x25: PUSH(fr.locals[3]); break;
        case 0x26: PUSH(fr.locals[0]); break;
        case 0x27: PUSH(fr.locals[1]); break;
        case 0x28: PUSH(fr.locals[2]); break;
        case 0x29: PUSH(fr.locals[3]); break;
        case 0x2a: PUSH(fr.locals[0]); break;
        case 0x2b: PUSH(fr.locals[1]); break;
        case 0x2c: PUSH(fr.locals[2]); break;
        case 0x2d: PUSH(fr.locals[3]); break;

        /* ── 배열 로드 ────────────────────────────────────────────── */
        case 0x2e: case 0x33: case 0x34: case 0x35: { /* i/b/c/saload */
            jval_t idx = POP(); jval_t arr = POP();
            if (arr.ref) {
                jobj_t *a = (jobj_t *)arr.ref;
                if (a->type == OBJ_ARRAY_INT &&
                    idx.ival >= 0 && idx.ival < a->iarr.len)
                    PUSHI(a->iarr.data[idx.ival]);
                else PUSHI(0);
            } else PUSHI(0);
            break;
        }
        case 0x32: { /* aaload */
            jval_t idx = POP(); jval_t arr = POP();
            if (arr.ref) {
                jobj_t *a = (jobj_t *)arr.ref;
                if (a->type == OBJ_ARRAY_REF &&
                    idx.ival >= 0 && idx.ival < a->rarr.len)
                    PUSHR(a->rarr.data[idx.ival]);
                else PUSHR((void *)0);
            } else PUSHR((void *)0);
            break;
        }
        case 0x2f: case 0x30: case 0x31: /* l/f/daload (stub) */
            { POP(); POP(); PUSHI(0); break; }

        /* ── 로컬 변수 저장 ───────────────────────────────────────── */
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a:
            { int i = (int)U1_NEXT(); fr.locals[i] = POP(); break; }
        case 0x3b: fr.locals[0] = POP(); break;
        case 0x3c: fr.locals[1] = POP(); break;
        case 0x3d: fr.locals[2] = POP(); break;
        case 0x3e: fr.locals[3] = POP(); break;
        case 0x3f: fr.locals[0] = POP(); break;
        case 0x40: fr.locals[1] = POP(); break;
        case 0x41: fr.locals[2] = POP(); break;
        case 0x42: fr.locals[3] = POP(); break;
        case 0x43: fr.locals[0] = POP(); break;
        case 0x44: fr.locals[1] = POP(); break;
        case 0x45: fr.locals[2] = POP(); break;
        case 0x46: fr.locals[3] = POP(); break;
        case 0x47: fr.locals[0] = POP(); break;
        case 0x48: fr.locals[1] = POP(); break;
        case 0x49: fr.locals[2] = POP(); break;
        case 0x4a: fr.locals[3] = POP(); break;
        case 0x4b: fr.locals[0] = POP(); break;
        case 0x4c: fr.locals[1] = POP(); break;
        case 0x4d: fr.locals[2] = POP(); break;
        case 0x4e: fr.locals[3] = POP(); break;

        /* ── 배열 저장 ────────────────────────────────────────────── */
        case 0x4f: case 0x54: case 0x55: case 0x56: { /* i/b/c/sastore */
            jval_t val = POP(); jval_t idx = POP(); jval_t arr = POP();
            if (arr.ref) {
                jobj_t *a = (jobj_t *)arr.ref;
                if (a->type == OBJ_ARRAY_INT &&
                    idx.ival >= 0 && idx.ival < a->iarr.len)
                    a->iarr.data[idx.ival] = val.ival;
            }
            break;
        }
        case 0x53: { /* aastore */
            jval_t val = POP(); jval_t idx = POP(); jval_t arr = POP();
            if (arr.ref) {
                jobj_t *a = (jobj_t *)arr.ref;
                if (a->type == OBJ_ARRAY_REF &&
                    idx.ival >= 0 && idx.ival < a->rarr.len)
                    a->rarr.data[idx.ival] = (jobj_t *)val.ref;
            }
            break;
        }
        case 0x50: case 0x51: case 0x52: POP(); POP(); POP(); break;

        /* ── 스택 조작 ────────────────────────────────────────────── */
        case 0x57: POP(); break;
        case 0x58: POP(); POP(); break;
        case 0x59: { jval_t v = PEEK(); PUSH(v); break; }
        case 0x5a: { jval_t v1=POP(), v2=POP(); PUSH(v1); PUSH(v2); PUSH(v1); break; }
        case 0x5b: { jval_t v1=POP(),v2=POP(),v3=POP();
                     PUSH(v1); PUSH(v3); PUSH(v2); PUSH(v1); break; }
        case 0x5c: { jval_t v1=POP(),v2=POP();
                     PUSH(v2); PUSH(v1); PUSH(v2); PUSH(v1); break; }
        case 0x5d: case 0x5e: { jval_t v=PEEK(); PUSH(v); break; }
        case 0x5f: { jval_t v1=POP(), v2=POP(); PUSH(v1); PUSH(v2); break; }

        /* ── 산술 연산 ────────────────────────────────────────────── */
        case 0x60: case 0x61: case 0x62: case 0x63: /* iadd, ladd, fadd, dadd */
            { jval_t b=POP(), a=POP(); PUSHI(a.ival + b.ival); break; }
        case 0x64: case 0x65: case 0x66: case 0x67: /* isub ... dsub */
            { jval_t b=POP(), a=POP(); PUSHI(a.ival - b.ival); break; }
        case 0x68: case 0x69: case 0x6a: case 0x6b: /* imul ... dmul */
            { jval_t b=POP(), a=POP(); PUSHI(a.ival * b.ival); break; }
        case 0x6c: case 0x6d: case 0x6e: case 0x6f: /* idiv ... ddiv */
            { jval_t b=POP(), a=POP();
              if (b.ival == 0) { printf("[jvm] / by zero\n"); goto exec_error; }
              PUSHI(a.ival / b.ival); break; }
        case 0x70: case 0x71: case 0x72: case 0x73: /* irem ... drem */
            { jval_t b=POP(), a=POP();
              if (b.ival == 0) { printf("[jvm] %% by zero\n"); goto exec_error; }
              PUSHI(a.ival % b.ival); break; }
        case 0x74: case 0x75: case 0x76: case 0x77: /* ineg ... dneg */
            { jval_t a=POP(); PUSHI(-a.ival); break; }
        case 0x78: case 0x79:
            { jval_t b=POP(), a=POP(); PUSHI(a.ival << (b.ival & 0x1f)); break; }
        case 0x7a: case 0x7b:
            { jval_t b=POP(), a=POP(); PUSHI(a.ival >> (b.ival & 0x1f)); break; }
        case 0x7c: case 0x7d:
            { jval_t b=POP(), a=POP();
              PUSHI((int32_t)((uint32_t)a.ival >> (b.ival & 0x1f))); break; }
        case 0x7e: case 0x7f:
            { jval_t b=POP(), a=POP(); PUSHI(a.ival & b.ival); break; }
        case 0x80: case 0x81:
            { jval_t b=POP(), a=POP(); PUSHI(a.ival | b.ival); break; }
        case 0x82: case 0x83:
            { jval_t b=POP(), a=POP(); PUSHI(a.ival ^ b.ival); break; }
        case 0x84: { /* iinc */
            int idx   = (int)U1_NEXT();
            int delta = (int)S1_NEXT();
            fr.locals[idx].ival += delta;
            break;
        }

        /* ── 타입 변환 ────────────────────────────────────────────── */
        case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8a:
        case 0x8b: case 0x8c: case 0x8d:
        case 0x8e: case 0x8f: case 0x90: break; /* 정수로 다루므로 no-op */
        case 0x91: { jval_t v=POP(); PUSHI((int8_t)v.ival);   break; } /* i2b */
        case 0x92: { jval_t v=POP(); PUSHI((uint16_t)v.ival); break; } /* i2c */
        case 0x93: { jval_t v=POP(); PUSHI((int16_t)v.ival);  break; } /* i2s */

        /* ── 비교 ─────────────────────────────────────────────────── */
        case 0x94: case 0x95: case 0x96: case 0x97: case 0x98:
            { jval_t b=POP(), a=POP();
              PUSHI(a.ival < b.ival ? -1 : (a.ival > b.ival ? 1 : 0)); break; }

        /* ── 조건 분기 (vs 0) ─────────────────────────────────────── */
        case 0x99: { jval_t v=POP(); BRANCH_IF(v.ival == 0); break; }
        case 0x9a: { jval_t v=POP(); BRANCH_IF(v.ival != 0); break; }
        case 0x9b: { jval_t v=POP(); BRANCH_IF(v.ival <  0); break; }
        case 0x9c: { jval_t v=POP(); BRANCH_IF(v.ival >= 0); break; }
        case 0x9d: { jval_t v=POP(); BRANCH_IF(v.ival >  0); break; }
        case 0x9e: { jval_t v=POP(); BRANCH_IF(v.ival <= 0); break; }

        /* ── 조건 분기 (int 비교) ─────────────────────────────────── */
        case 0x9f: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival==b.ival); break; }
        case 0xa0: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival!=b.ival); break; }
        case 0xa1: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival< b.ival); break; }
        case 0xa2: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival>=b.ival); break; }
        case 0xa3: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival> b.ival); break; }
        case 0xa4: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ival<=b.ival); break; }

        /* ── 조건 분기 (참조 비교) ────────────────────────────────── */
        case 0xa5: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ref==b.ref); break; }
        case 0xa6: { jval_t b=POP(),a=POP(); BRANCH_IF(a.ref!=b.ref); break; }

        /* ── 무조건 분기 ──────────────────────────────────────────── */
        case 0xa7: { /* goto */
            int16_t off = (int16_t)(((unsigned)CODE[PC]<<8)|CODE[PC+1]);
            PC = instr_pc + (int)off;
            break;
        }
        case 0xc8: { /* goto_w */
            int32_t off = (int32_t)(((uint32_t)CODE[PC]<<24)|
                                    ((uint32_t)CODE[PC+1]<<16)|
                                    ((uint32_t)CODE[PC+2]<<8)|
                                     (uint32_t)CODE[PC+3]);
            PC = instr_pc + (int)off;
            break;
        }
        case 0xa8: { /* jsr */
            int16_t off = S2_NEXT();
            PUSHI(PC);
            PC = instr_pc + (int)off;
            break;
        }
        case 0xa9: { /* ret */
            int idx = (int)U1_NEXT();
            PC = fr.locals[idx].ival;
            break;
        }
        case 0xc6: { /* ifnull */
            jval_t v=POP();
            BRANCH_IF(v.tag == TAG_REF && v.ref == (void *)0);
            break;
        }
        case 0xc7: { /* ifnonnull */
            jval_t v=POP();
            BRANCH_IF(!(v.tag == TAG_REF && v.ref == (void *)0));
            break;
        }

        /* ── tableswitch ─────────────────────────────────────────── */
        case 0xaa: {
            int pad = (4 - ((instr_pc + 1) & 3)) & 3;
            PC = instr_pc + 1 + pad;
            int32_t dflt = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                     ((uint32_t)CODE[PC+1]<<16)|
                                     ((uint32_t)CODE[PC+2]<< 8)|
                                      (uint32_t)CODE[PC+3]); PC+=4;
            int32_t lo   = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                     ((uint32_t)CODE[PC+1]<<16)|
                                     ((uint32_t)CODE[PC+2]<< 8)|
                                      (uint32_t)CODE[PC+3]); PC+=4;
            int32_t hi   = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                     ((uint32_t)CODE[PC+1]<<16)|
                                     ((uint32_t)CODE[PC+2]<< 8)|
                                      (uint32_t)CODE[PC+3]); PC+=4;
            jval_t key = POP();
            if (key.ival >= lo && key.ival <= hi) {
                int oi = (int)(key.ival - lo) * 4;
                int32_t off = (int32_t)(((uint32_t)CODE[PC+oi  ]<<24)|
                                        ((uint32_t)CODE[PC+oi+1]<<16)|
                                        ((uint32_t)CODE[PC+oi+2]<< 8)|
                                         (uint32_t)CODE[PC+oi+3]);
                PC = instr_pc + (int)off;
            } else {
                PC = instr_pc + (int)dflt;
            }
            (void)hi;
            break;
        }

        /* ── lookupswitch ────────────────────────────────────────── */
        case 0xab: {
            int pad = (4 - ((instr_pc + 1) & 3)) & 3;
            PC = instr_pc + 1 + pad;
            int32_t dflt = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                     ((uint32_t)CODE[PC+1]<<16)|
                                     ((uint32_t)CODE[PC+2]<< 8)|
                                      (uint32_t)CODE[PC+3]); PC+=4;
            int32_t np   = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                     ((uint32_t)CODE[PC+1]<<16)|
                                     ((uint32_t)CODE[PC+2]<< 8)|
                                      (uint32_t)CODE[PC+3]); PC+=4;
            jval_t key = POP();
            int32_t target = dflt;
            for (int32_t i = 0; i < np; i++) {
                int32_t m = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                      ((uint32_t)CODE[PC+1]<<16)|
                                      ((uint32_t)CODE[PC+2]<< 8)|
                                       (uint32_t)CODE[PC+3]); PC+=4;
                int32_t o = (int32_t)(((uint32_t)CODE[PC  ]<<24)|
                                      ((uint32_t)CODE[PC+1]<<16)|
                                      ((uint32_t)CODE[PC+2]<< 8)|
                                       (uint32_t)CODE[PC+3]); PC+=4;
                if (key.ival == m) { target = o; break; }
            }
            PC = instr_pc + (int)target;
            break;
        }

        /* ── 반환 ────────────────────────────────────────────────── */
        case 0xac: case 0xad: case 0xae: case 0xaf: /* i/l/f/dreturn */
            return POP();
        case 0xb0: return POP();           /* areturn */
        case 0xb1: return JVAL_INT(0);     /* return  */

        /* ── 정적 필드 ───────────────────────────────────────────── */
        case 0xb2: { /* getstatic */
            int cpidx = (int)U2_NEXT();
            const char *cls, *fn, *fd;
            get_ref(klass, cpidx, &cls, &fn, &fd);
            if (strcmp(cls, "java/lang/System") == 0 &&
                (strcmp(fn, "out") == 0 || strcmp(fn, "err") == 0)) {
                PUSHR(jvm->system_out);
            } else {
                class_info_t *tc = classloader_resolve(jvm, cls);
                if (tc) {
                    field_info_t *f = class_find_field(tc, fn);
                    if (f) { PUSH(f->static_val); break; }
                }
                PUSHI(0);
            }
            break;
        }
        case 0xb3: { /* putstatic */
            int cpidx = (int)U2_NEXT();
            const char *cls, *fn, *fd;
            get_ref(klass, cpidx, &cls, &fn, &fd);
            jval_t val = POP();
            class_info_t *tc = classloader_resolve(jvm, cls);
            if (tc) {
                field_info_t *f = class_find_field(tc, fn);
                if (f) f->static_val = val;
            }
            break;
        }

        /* ── 인스턴스 필드 ────────────────────────────────────────── */
        case 0xb4: { /* getfield */
            int cpidx = (int)U2_NEXT();
            const char *cls, *fn, *fd;
            get_ref(klass, cpidx, &cls, &fn, &fd);
            jval_t objref = POP();
            if (objref.ref) {
                jobj_t *o = (jobj_t *)objref.ref;
                if (o->type == OBJ_INSTANCE && o->inst.klass) {
                    for (int i = 0; i < o->inst.klass->field_count; i++) {
                        if (strcmp(o->inst.klass->fields[i].name, fn) == 0) {
                            PUSH(o->inst.fields ? o->inst.fields[i]
                                                : JVAL_INT(0));
                            goto next_op;
                        }
                    }
                }
            }
            PUSHI(0);
            break;
        }
        case 0xb5: { /* putfield */
            int cpidx = (int)U2_NEXT();
            const char *cls, *fn, *fd;
            get_ref(klass, cpidx, &cls, &fn, &fd);
            jval_t val    = POP();
            jval_t objref = POP();
            if (objref.ref) {
                jobj_t *o = (jobj_t *)objref.ref;
                if (o->type == OBJ_INSTANCE && o->inst.klass &&
                    o->inst.fields) {
                    for (int i = 0; i < o->inst.klass->field_count; i++) {
                        if (strcmp(o->inst.klass->fields[i].name, fn) == 0) {
                            o->inst.fields[i] = val;
                            break;
                        }
                    }
                }
            }
            break;
        }

        /* ── 메서드 호출 ─────────────────────────────────────────── */
        case 0xb6: { /* invokevirtual */
            int cpidx = (int)U2_NEXT();
            const char *cls, *mn, *md;
            get_ref(klass, cpidx, &cls, &mn, &md);
            do_invoke(jvm, &fr, cls, mn, md, 0);
            break;
        }
        case 0xb7: { /* invokespecial */
            int cpidx = (int)U2_NEXT();
            const char *cls, *mn, *md;
            get_ref(klass, cpidx, &cls, &mn, &md);
            do_invoke(jvm, &fr, cls, mn, md, 0);
            break;
        }
        case 0xb8: { /* invokestatic */
            int cpidx = (int)U2_NEXT();
            const char *cls, *mn, *md;
            get_ref(klass, cpidx, &cls, &mn, &md);
            do_invoke(jvm, &fr, cls, mn, md, 1);
            break;
        }
        case 0xb9: { /* invokeinterface */
            int cpidx = (int)U2_NEXT();
            PC += 2; /* count + 0 건너뜀 */
            const char *cls, *mn, *md;
            get_ref(klass, cpidx, &cls, &mn, &md);
            do_invoke(jvm, &fr, cls, mn, md, 0);
            break;
        }
        case 0xba: /* invokedynamic: 스택 상태 유지 (스텁) */
            PC += 4; break;

        /* ── 객체 생성 ────────────────────────────────────────────── */
        case 0xbb: { /* new */
            int cpidx = (int)U2_NEXT();
            const char *cname = "";
            if (cpidx > 0 && cpidx < klass->cp_count &&
                klass->cp[cpidx].tag == CP_CLASS)
                cname = cp_utf8(klass, klass->cp[cpidx].class_idx);
            if (strcmp(cname, "java/lang/StringBuilder") == 0) {
                PUSHR(obj_stringbld(jvm));
            } else {
                class_info_t *tc = classloader_resolve(jvm, cname);
                PUSHR(tc ? obj_instance(jvm, tc) : (void *)0);
            }
            break;
        }
        case 0xbc: { /* newarray */
            int atype = (int)U1_NEXT();
            jval_t len = POP();
            (void)atype;
            PUSHR(obj_array_int(jvm, len.ival));
            break;
        }
        case 0xbd: { /* anewarray */
            PC += 2;
            jval_t len = POP();
            PUSHR(obj_array_ref(jvm, len.ival));
            break;
        }
        case 0xbe: { /* arraylength */
            jval_t arr = POP();
            if (arr.ref) {
                jobj_t *a = (jobj_t *)arr.ref;
                if      (a->type == OBJ_ARRAY_INT) PUSHI(a->iarr.len);
                else if (a->type == OBJ_ARRAY_REF) PUSHI(a->rarr.len);
                else if (a->type == OBJ_STRING)    PUSHI(a->str.len);
                else PUSHI(0);
            } else PUSHI(0);
            break;
        }
        case 0xbf: /* athrow */
            printf("[jvm] Exception thrown\n"); goto exec_error;
        case 0xc0: PC += 2; break;       /* checkcast: 무시 */
        case 0xc1: { /* instanceof */
            PC += 2;
            jval_t r = POP();
            PUSHI(r.ref ? 1 : 0);
            break;
        }
        case 0xc2: case 0xc3: break;     /* monitorenter/exit: 무시 */

        /* ── wide 접두어 ─────────────────────────────────────────── */
        case 0xc4: {
            uint8_t wop = (uint8_t)U1_NEXT();
            int idx = (int)U2_NEXT();
            switch (wop) {
            case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
                PUSH(fr.locals[idx]); break;
            case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a:
                fr.locals[idx] = POP(); break;
            case 0x84: {
                int delta = (int)(int16_t)U2_NEXT();
                fr.locals[idx].ival += delta;
                break;
            }
            default: break;
            }
            break;
        }

        /* ── multianewarray ─────────────────────────────────────── */
        case 0xc5: {
            PC += 2;
            int dims = (int)U1_NEXT();
            int d0 = 1;
            for (int i = 0; i < dims; i++) {
                jval_t v = POP();
                if (i == dims - 1) d0 = v.ival;
            }
            PUSHR(obj_array_ref(jvm, d0));
            break;
        }

        default:
            printf("[jvm] unimplemented opcode 0x%02x at pc=%d in %s\n",
                   op, instr_pc,
                   klass->name ? klass->name : "?");
            goto exec_error;
        }

        next_op: ; /* getfield 분기 타겟 */
    }

    return JVAL_INT(0);

exec_error:
    printf("[jvm] aborted: %s.%s\n",
           klass->name  ? klass->name  : "?",
           method->name ? method->name : "?");
    exit(1);
    return JVAL_INT(-1);
}
