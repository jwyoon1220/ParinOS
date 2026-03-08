//
// Created by jwyoo on 26. 3. 8..
//

#ifndef PARINOS_MEM_H
#define PARINOS_MEM_H

#include <stdint.h>
#include <stddef.h>

// 메모리를 특정 값으로 채움 (주로 0으로 초기화할 때 사용)
void* memset(void* dest, int ch, size_t count);

// 메모리 영역 복사 (영역이 겹치지 않을 때 사용, 속도가 빠름)
void* memcpy(void* dest, const void* src, size_t count);

// 메모리 영역 복사 (영역이 겹쳐도 안전하게 복사)
void* memmove(void* dest, const void* src, size_t count);

// 두 메모리 영역의 내용을 비교
int memcmp(const void* lhs, const void* rhs, size_t count);

#endif //PARINOS_MEM_H