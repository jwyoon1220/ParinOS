#include "tss.h"
#include "../mem/mem.h"

// 전역 TSS 인스턴스 (GDT 엔트리가 이 구조체의 주소를 가리킴)
tss_entry_t tss;

void tss_init(void) {
    memset(&tss, 0, sizeof(tss_entry_t));

    tss.ss0        = 0x10;              // 커널 데이터 세그먼트 (Ring 0 스택 세그먼트)
    tss.esp0       = 0;                 // 커널 스택 포인터: 유저 스레드 생성 시 갱신됨
    tss.iomap_base = sizeof(tss_entry_t); // I/O 권한 비트맵 없음 (TSS 끝을 가리킴)
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
