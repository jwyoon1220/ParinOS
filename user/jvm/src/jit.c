/*
 * jit.c — x86-32 템플릿 JIT 컴파일러
 *
 * 순수 정수 연산만 사용하는 메서드(레퍼런스·메서드 호출 없음)를
 * x86-32 네이티브 코드로 컴파일한다.
 *
 * 생성 함수 시그니처:
 *   int32_t fn(int32_t *locals)
 *   - EBX = locals 배열 포인터  ([ebp+8] 에서 로드)
 *   - Java operand stack ↔ x86 스택
 *   - 반환값 EAX
 *
 * 지원 opcode: iconst*, bipush, sipush, iload*, istore*, iinc,
 *              iadd, isub, imul, idiv, irem, ineg,
 *              ishl, ishr, iushr, iand, ior, ixor,
 *              pop, dup, swap,
 *              ifeq..ifle, if_icmpeq..if_icmple, goto,
 *              ireturn, return
 */

#include "jvm.h"
#include "string.h"

/* ── 바이트 방출 매크로 ─────────────────────────────────────────────── */
#define EMIT1(b) do { \
    if (jvm->jit_ptr < jvm->jit_end) *jvm->jit_ptr++ = (uint8_t)(b); \
} while(0)
#define EMIT2(a,b)   do { EMIT1(a); EMIT1(b); } while(0)
#define EMIT3(a,b,c) do { EMIT1(a); EMIT1(b); EMIT1(c); } while(0)
#define EMIT4_LE(v)  do { uint32_t _v=(uint32_t)(v); \
    EMIT1(_v&0xff); EMIT1((_v>>8)&0xff); \
    EMIT1((_v>>16)&0xff); EMIT1((_v>>24)&0xff); } while(0)

/* ── 분기 패치 테이블 ───────────────────────────────────────────────── */
#define MAX_PATCHES  128
#define MAX_PC_MAP  4096

typedef struct {
    int      java_target;  /* 분기 목적지 Java PC */
    uint8_t *patch_loc;    /* rel32 오프셋 기록 위치 */
} patch_t;

/* 컴파일 1회당 정적 버퍼 (비재진입, 단일 스레드) */
static uint8_t  *s_pc_map[MAX_PC_MAP];
static patch_t   s_patches[MAX_PATCHES];
static int       s_npatches;

/* ── JCC 코드 (0F 8x 두 번째 바이트) ────────────────────────────────── */
#define JCC_JE   0x84
#define JCC_JNE  0x85
#define JCC_JL   0x8C
#define JCC_JGE  0x8D
#define JCC_JG   0x8F
#define JCC_JLE  0x8E

/* ── 분기 방출 헬퍼 ─────────────────────────────────────────────────── */
static void emit_jcc(jvm_t *jvm, uint8_t jcc2, int java_target) {
    EMIT2(0x0f, jcc2);
    if (s_npatches < MAX_PATCHES) {
        s_patches[s_npatches].java_target = java_target;
        s_patches[s_npatches].patch_loc   = jvm->jit_ptr;
        s_npatches++;
    }
    EMIT4_LE(0);   /* 나중에 패치됨 */
}
static void emit_jmp(jvm_t *jvm, int java_target) {
    EMIT1(0xe9);
    if (s_npatches < MAX_PATCHES) {
        s_patches[s_npatches].java_target = java_target;
        s_patches[s_npatches].patch_loc   = jvm->jit_ptr;
        s_npatches++;
    }
    EMIT4_LE(0);
}

/* ── 로컬 변수 주소 오프셋 (EBX 기준) ──────────────────────────────── */
/* locals[idx] = [ebx + idx*4] */

/* PUSH [EBX+off8] 또는 PUSH [EBX] */
static void emit_iload(jvm_t *jvm, int idx) {
    if (idx == 0) {
        EMIT2(0xff, 0x33);               /* push dword [ebx] */
    } else {
        EMIT3(0xff, 0x73, (uint8_t)(idx * 4)); /* push dword [ebx+disp8] */
    }
}

/* POP EAX; MOV [EBX+off8], EAX */
static void emit_istore(jvm_t *jvm, int idx) {
    EMIT1(0x58);                          /* pop eax */
    if (idx == 0) {
        EMIT2(0x89, 0x03);               /* mov [ebx], eax */
    } else {
        EMIT3(0x89, 0x43, (uint8_t)(idx * 4)); /* mov [ebx+disp8], eax */
    }
}

/* ADD/SUB [EBX+off8], imm8 */
static void emit_iinc(jvm_t *jvm, int idx, int8_t delta) {
    if (idx == 0) {
        EMIT3(0x83, 0x03, (uint8_t)delta);        /* add [ebx], imm8 */
    } else {
        EMIT1(0x83);
        EMIT2(0x43, (uint8_t)(idx * 4));           /* add [ebx+disp8] */
        EMIT1((uint8_t)delta);
    }
}

/* ── jit_can_compile ─────────────────────────────────────────────────── */
int jit_can_compile(method_info_t *method) {
    if (!method->code || method->code_len == 0) return 0;

    const uint8_t *code = method->code;
    int pc  = 0;
    int len = (int)method->code_len;

    while (pc < len) {
        uint8_t op = code[pc++];
        switch (op) {
        /* 0바이트 피연산자 */
        case 0x00:                          /* nop       */
        case 0x02: case 0x03: case 0x04:   /* iconst_m1..2 */
        case 0x05: case 0x06: case 0x07: case 0x08: /* iconst_3..5 */
        case 0x1a: case 0x1b: case 0x1c: case 0x1d: /* iload_0..3 */
        case 0x3b: case 0x3c: case 0x3d: case 0x3e: /* istore_0..3 */
        case 0x57:                          /* pop        */
        case 0x59:                          /* dup        */
        case 0x5f:                          /* swap       */
        case 0x60: case 0x64: case 0x68:   /* iadd isub imul */
        case 0x6c: case 0x70:              /* idiv irem  */
        case 0x74:                          /* ineg       */
        case 0x78: case 0x7a: case 0x7c:  /* ishl ishr iushr */
        case 0x7e: case 0x80: case 0x82:  /* iand ior ixor  */
        case 0xac:                          /* ireturn    */
        case 0xb1:                          /* return     */
            break;

        /* 1바이트 피연산자 */
        case 0x10:                          /* bipush     */
        case 0x15: case 0x36:              /* iload/istore (wide idx는 wide로 처리) */
            if (pc >= len) return 0;
            pc++;
            break;

        /* 2바이트 피연산자 (분기) */
        case 0x99: case 0x9a: case 0x9b:
        case 0x9c: case 0x9d: case 0x9e:  /* ifeq..ifle */
        case 0x9f: case 0xa0: case 0xa1:
        case 0xa2: case 0xa3: case 0xa4:  /* if_icmp*   */
        case 0xa7:                          /* goto       */
        case 0x11:                          /* sipush     */
            if (pc + 1 >= len) return 0;
            pc += 2;
            break;

        /* iinc: 2바이트 */
        case 0x84:
            if (pc + 1 >= len) return 0;
            /* 로컬 인덱스가 31 이하인지 확인 (8bit 오프셋 범위 내) */
            if (code[pc] > 31) return 0;
            pc += 2;
            break;

        default: return 0;   /* 지원 불가 opcode */
        }
    }
    return 1;
}

/* ── jit_compile ─────────────────────────────────────────────────────── */
void jit_compile(jvm_t *jvm, class_info_t *klass, method_info_t *method) {
    (void)klass;
    if (!jit_can_compile(method)) return;

    const uint8_t *code = method->code;
    int clen = (int)method->code_len;

    /* 버퍼 여유 확인: 최악의 경우 opcode 당 ~12바이트 + 프롤로그/에필로그 */
    if (jvm->jit_ptr + 32 + (size_t)clen * 12 > jvm->jit_end) return;

    uint8_t *fn_start = jvm->jit_ptr;
    s_npatches = 0;
    memset(s_pc_map, 0, sizeof(s_pc_map));

    /* ── 프롤로그 ────────────────────────────────────────────────────
     * push ebp          ; 55
     * mov  ebp, esp     ; 89 E5
     * push ebx          ; 53
     * sub  esp, 128     ; 81 EC 80 00 00 00   (Java 스택 공간 확보)
     * mov  ebx, [ebp+8] ; 8B 5D 08
     * ──────────────────────────────────────────────────────────────── */
    EMIT1(0x55);
    EMIT2(0x89, 0xe5);
    EMIT1(0x53);
    EMIT1(0x81); EMIT1(0xec); EMIT4_LE(128);
    EMIT3(0x8b, 0x5d, 0x08);

    /* ── 바이트코드 → x86 변환 루프 ─────────────────────────────── */
    int pc = 0;
    while (pc < clen) {
        if (pc < MAX_PC_MAP) s_pc_map[pc] = jvm->jit_ptr;
        int instr_pc = pc;
        uint8_t op   = code[pc++];

        switch (op) {
        case 0x00: break; /* nop */

        /* iconst_m1..5 → push imm32 */
        case 0x02: EMIT1(0x68); EMIT4_LE((uint32_t)(int32_t)-1); break;
        case 0x03: EMIT2(0x6a, 0x00); break;
        case 0x04: EMIT2(0x6a, 0x01); break;
        case 0x05: EMIT2(0x6a, 0x02); break;
        case 0x06: EMIT2(0x6a, 0x03); break;
        case 0x07: EMIT2(0x6a, 0x04); break;
        case 0x08: EMIT2(0x6a, 0x05); break;

        case 0x10: { /* bipush byte */
            int8_t v = (int8_t)code[pc++];
            EMIT1(0x68); EMIT4_LE((uint32_t)(int32_t)v);
            break;
        }
        case 0x11: { /* sipush short */
            int16_t v = (int16_t)(((unsigned)code[pc]<<8)|code[pc+1]); pc+=2;
            EMIT1(0x68); EMIT4_LE((uint32_t)(int32_t)v);
            break;
        }

        /* iload_0..3 → push [ebx+idx*4] */
        case 0x1a: emit_iload(jvm, 0); break;
        case 0x1b: emit_iload(jvm, 1); break;
        case 0x1c: emit_iload(jvm, 2); break;
        case 0x1d: emit_iload(jvm, 3); break;
        case 0x15: { int idx = code[pc++]; emit_iload(jvm, idx); break; }

        /* istore_0..3 → pop eax; mov [ebx+idx*4], eax */
        case 0x3b: emit_istore(jvm, 0); break;
        case 0x3c: emit_istore(jvm, 1); break;
        case 0x3d: emit_istore(jvm, 2); break;
        case 0x3e: emit_istore(jvm, 3); break;
        case 0x36: { int idx = code[pc++]; emit_istore(jvm, idx); break; }

        /* iinc: add [ebx+idx*4], delta */
        case 0x84: {
            int   idx   = code[pc++];
            int8_t delta = (int8_t)code[pc++];
            emit_iinc(jvm, idx, delta);
            break;
        }

        /* ── 산술 ──────────────────────────────────────────────── */
        /* iadd: pop eax; add [esp], eax */
        case 0x60:
            EMIT1(0x58);
            EMIT3(0x01, 0x04, 0x24);   /* add [esp], eax */
            break;
        /* isub: pop ecx; pop eax; sub eax,ecx; push eax */
        case 0x64:
            EMIT1(0x59);               /* pop ecx  */
            EMIT1(0x58);               /* pop eax  */
            EMIT2(0x29, 0xc8);         /* sub eax, ecx */
            EMIT1(0x50);               /* push eax */
            break;
        /* imul: pop ecx; pop eax; imul eax,ecx; push eax */
        case 0x68:
            EMIT1(0x59);
            EMIT1(0x58);
            EMIT3(0x0f, 0xaf, 0xc1);   /* imul eax, ecx */
            EMIT1(0x50);
            break;
        /* idiv: pop ecx; pop eax; cdq; idiv ecx; push eax */
        case 0x6c:
            EMIT1(0x59);
            EMIT1(0x58);
            EMIT1(0x99);               /* cdq */
            EMIT2(0xf7, 0xf9);         /* idiv ecx */
            EMIT1(0x50);               /* push eax (몫) */
            break;
        /* irem: pop ecx; pop eax; cdq; idiv ecx; push edx */
        case 0x70:
            EMIT1(0x59);
            EMIT1(0x58);
            EMIT1(0x99);
            EMIT2(0xf7, 0xf9);
            EMIT1(0x52);               /* push edx (나머지) */
            break;
        /* ineg: neg dword [esp] */
        case 0x74:
            EMIT3(0xf7, 0x1c, 0x24);   /* neg [esp] */
            break;
        /* ishl: pop ecx; shl [esp], cl */
        case 0x78:
            EMIT1(0x59);
            EMIT3(0xd3, 0x24, 0x24);   /* shl [esp], cl */
            break;
        /* ishr: pop ecx; sar [esp], cl */
        case 0x7a:
            EMIT1(0x59);
            EMIT3(0xd3, 0x3c, 0x24);   /* sar [esp], cl */
            break;
        /* iushr: pop ecx; shr [esp], cl */
        case 0x7c:
            EMIT1(0x59);
            EMIT3(0xd3, 0x2c, 0x24);   /* shr [esp], cl */
            break;
        /* iand: pop ecx; and [esp], ecx */
        case 0x7e:
            EMIT1(0x59);
            EMIT3(0x21, 0x0c, 0x24);   /* and [esp], ecx */
            break;
        /* ior: pop ecx; or [esp], ecx */
        case 0x80:
            EMIT1(0x59);
            EMIT3(0x09, 0x0c, 0x24);   /* or [esp], ecx */
            break;
        /* ixor: pop ecx; xor [esp], ecx */
        case 0x82:
            EMIT1(0x59);
            EMIT3(0x31, 0x0c, 0x24);   /* xor [esp], ecx */
            break;

        /* ── 스택 조작 ──────────────────────────────────────────── */
        case 0x57: EMIT1(0x58); break; /* pop: pop eax (버림) */
        case 0x59:                     /* dup: push dword [esp] */
            EMIT3(0xff, 0x34, 0x24);
            break;
        case 0x5f:                     /* swap */
            EMIT1(0x58);               /* pop eax  */
            EMIT1(0x59);               /* pop ecx  */
            EMIT1(0x50);               /* push eax */
            EMIT1(0x51);               /* push ecx */
            break;

        /* ── 분기 ─────────────────────────────────────────────────
         * ifeq..ifle: pop eax; test eax,eax; Jcc target
         * if_icmp*: pop ecx; pop eax; cmp eax,ecx; Jcc target
         * ─────────────────────────────────────────────────────── */
#define EMIT_IF_ZERO(jcc) do { \
    int16_t off = (int16_t)(((unsigned)code[pc]<<8)|code[pc+1]); pc+=2; \
    EMIT1(0x58);               /* pop eax            */ \
    EMIT2(0x85, 0xc0);         /* test eax, eax      */ \
    emit_jcc(jvm, (jcc), instr_pc + (int)off); \
} while(0)

#define EMIT_IF_ICMP(jcc) do { \
    int16_t off = (int16_t)(((unsigned)code[pc]<<8)|code[pc+1]); pc+=2; \
    EMIT1(0x59);               /* pop ecx (value2)   */ \
    EMIT1(0x58);               /* pop eax (value1)   */ \
    EMIT2(0x39, 0xc8);         /* cmp eax, ecx       */ \
    emit_jcc(jvm, (jcc), instr_pc + (int)off); \
} while(0)

        case 0x99: EMIT_IF_ZERO(JCC_JE);  break;
        case 0x9a: EMIT_IF_ZERO(JCC_JNE); break;
        case 0x9b: EMIT_IF_ZERO(JCC_JL);  break;
        case 0x9c: EMIT_IF_ZERO(JCC_JGE); break;
        case 0x9d: EMIT_IF_ZERO(JCC_JG);  break;
        case 0x9e: EMIT_IF_ZERO(JCC_JLE); break;

        case 0x9f: EMIT_IF_ICMP(JCC_JE);  break;
        case 0xa0: EMIT_IF_ICMP(JCC_JNE); break;
        case 0xa1: EMIT_IF_ICMP(JCC_JL);  break;
        case 0xa2: EMIT_IF_ICMP(JCC_JGE); break;
        case 0xa3: EMIT_IF_ICMP(JCC_JG);  break;
        case 0xa4: EMIT_IF_ICMP(JCC_JLE); break;

        case 0xa7: { /* goto: jmp rel32 */
            int16_t off = (int16_t)(((unsigned)code[pc]<<8)|code[pc+1]); pc+=2;
            emit_jmp(jvm, instr_pc + (int)off);
            break;
        }

        /* ── 반환 ─────────────────────────────────────────────────
         * ireturn: pop eax; lea esp,[ebp-4]; pop ebx; pop ebp; ret
         * return:  xor eax,eax; <same epilogue>
         * ──────────────────────────────────────────────────────── */
        case 0xac: /* ireturn */
            EMIT1(0x58);               /* pop eax (return value)   */
            EMIT3(0x8d, 0x65, 0xfc);   /* lea esp, [ebp-4]         */
            EMIT1(0x5b);               /* pop ebx                  */
            EMIT1(0x5d);               /* pop ebp                  */
            EMIT1(0xc3);               /* ret                      */
            break;
        case 0xb1: /* return (void) */
            EMIT2(0x31, 0xc0);         /* xor eax, eax             */
            EMIT3(0x8d, 0x65, 0xfc);
            EMIT1(0x5b);
            EMIT1(0x5d);
            EMIT1(0xc3);
            break;

        default:
            /* jit_can_compile 에서 걸러졌으므로 여기 오면 안 됨 */
            goto compile_fail;
        }
    }

    /* ── 분기 패치 (2패스) ──────────────────────────────────────── */
    for (int i = 0; i < s_npatches; i++) {
        int jt = s_patches[i].java_target;
        if (jt < 0 || jt >= MAX_PC_MAP || !s_pc_map[jt]) continue;
        uint8_t *after = s_patches[i].patch_loc + 4;
        int32_t  rel   = (int32_t)(s_pc_map[jt] - after);
        uint8_t *loc   = s_patches[i].patch_loc;
        loc[0] = (uint8_t)( (uint32_t)rel        & 0xff);
        loc[1] = (uint8_t)(((uint32_t)rel >>  8) & 0xff);
        loc[2] = (uint8_t)(((uint32_t)rel >> 16) & 0xff);
        loc[3] = (uint8_t)(((uint32_t)rel >> 24) & 0xff);
    }

    method->jit_code = (void *)fn_start;
    return;

compile_fail:
    /* 컴파일 실패 시 생성된 코드 폐기 (버퍼 포인터 되감기) */
    jvm->jit_ptr = fn_start;
}

/* ── jit_call ─────────────────────────────────────────────────────────── */
jval_t jit_call(jvm_t *jvm, method_info_t *method, jval_t *args, int argc) {
    (void)jvm;
    typedef int32_t (*jit_fn_t)(int32_t *);

    int32_t locals[MAX_LOCALS];
    memset(locals, 0, sizeof(locals));
    int copy = (argc < MAX_LOCALS) ? argc : MAX_LOCALS;
    for (int i = 0; i < copy; i++) locals[i] = args[i].ival;

    jit_fn_t fn = (jit_fn_t)method->jit_code;
    int32_t  result = fn(locals);
    return JVAL_INT(result);
}
