#include "fpu.h"
#include <stdint.h>

/*
 * fpu_init – 부동소수점 연산 유닛 활성화
 *
 * CR0 비트를 수정하여 x87 FPU를 활성화하고
 * FNINIT 명령으로 FPU 상태를 초기화합니다.
 * stb_truetype 등 부동소수점 연산이 필요한 코드를
 * 실행하기 전에 반드시 호출해야 합니다.
 */
void fpu_init(void) {
    uint32_t cr0;
    uint32_t cr4;

    /* --- x87 FPU 활성화 (CR0) --- */
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2); /* EM=0 */
    cr0 &= ~(1u << 3); /* TS=0 */
    cr0 |= (1u << 1);  /* MP=1 */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    __asm__ volatile("fninit");

    /* --- SSE 활성화 (CR4) --- */
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9);  /* OSFXSR=1: FXSAVE/FXRSTOR 지원 */
    cr4 |= (1u << 10); /* OSXMMEXCPT=1: SIMD 예외 지원 */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}
