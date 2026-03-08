//
// Created by jwyoo on 26. 3. 8..
//

#ifndef PARINOS_VMM_H
#define PARINOS_VMM_H

#include <stdint.h>
#include "mem.h"
// 페이지 속성 플래그
#define PAGE_PRESENT  0x01   // 페이지가 메모리에 존재함
#define PAGE_RW       0x02   // 읽기/쓰기 가능
#define PAGE_USER     0x04   // 유저 모드 접근 가능

// 가상 주소 변환 매크로
#define PAGE_SIZE     4096
#define PAGE_MASK     0xFFFFF000 // 하위 12비트(플래그) 제거용 마스크
#define PD_INDEX(a)   (((a) >> 22) & 0x3FF) // 최상위 10비트 (페이지 디렉토리 인덱스)
#define PT_INDEX(a)   (((a) >> 12) & 0x3FF) // 중간 10비트 (페이지 테이블 인덱스)
#define TEMP_PT_VADDR 0x01000000

// VMM 핵심 함수 프로토타입
void init_vmm();
void vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);
void* vmm_get_phys(uint32_t vaddr);

#endif // PARINOS_VMM_H