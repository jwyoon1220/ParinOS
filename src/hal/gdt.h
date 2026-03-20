#ifndef GDT_H
#define GDT_H

#pragma pack(push, 1)

#include <stdint.h>

// GDT 엔트리 하나를 나타내는 구조체 (8바이트)
struct gdt_entry_struct {
    uint16_t limit_low;   // Limit의 하위 16비트
    uint16_t base_low;    // Base의 하위 16비트
    uint8_t  base_middle; // Base의 중간 8비트
    uint8_t  access;      // 접근 권한 (Access Byte)
    uint8_t  granularity; // Limit의 상위 4비트 + 플래그 4비트
    uint8_t  base_high;   // Base의 상위 8비트
};
// (__attribute__((packed)) 부분은 삭제했습니다)
typedef struct gdt_entry_struct gdt_entry_t;

// CPU의 GDTR 레지스터에 로드될 포인터 구조체 (6바이트)
struct gdt_ptr_struct {
    uint16_t limit;       // GDT 전체 크기 - 1
    uint32_t base;        // GDT의 시작 주소
};
typedef struct gdt_ptr_struct gdt_ptr_t;

// 🌟 압축(packed) 지시 끝! 원래 상태로 복구
#pragma pack(pop)

// GDT 초기화 함수
void init_gdt();

#endif