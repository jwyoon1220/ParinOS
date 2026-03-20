#ifndef FPU_H
#define FPU_H

/*
 * fpu.h – x87 부동소수점 연산 유닛(FPU) 초기화
 *
 * CR0.EM(bit 2) 클리어  → 하드웨어 FPU 사용
 * CR0.TS(bit 3) 클리어  → Task-Switched 플래그 해제
 * CR0.MP(bit 1) 설정    → WAIT/FWAIT 명령이 #NM을 일으키지 않도록
 * fninit                → FPU 레지스터 초기화
 */
void fpu_init(void);

#endif /* FPU_H */
