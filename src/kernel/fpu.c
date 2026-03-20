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

    /* CR0 읽기 */
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    /* CR0.EM(bit 2) 클리어: FPU 에뮬레이션 비활성화 */
    cr0 &= ~(1u << 2);

    /* CR0.TS(bit 3) 클리어: Task-Switched 플래그 해제 */
    cr0 &= ~(1u << 3);

    /* CR0.MP(bit 1) 설정: 보조 처리기 모니터링 활성화 */
    cr0 |= (1u << 1);

    /* CR0 쓰기 */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    /* FPU 상태 초기화 (예외 마스킹 포함) */
    __asm__ volatile("fninit");
}
