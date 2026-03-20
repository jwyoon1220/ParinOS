//
// src/cpp/cxxabi.cpp — C++ ABI 런타임 지원
//
// freestanding 커널에서 C++ 를 사용하기 위한 최소 런타임:
//  - operator new / delete
//  - 전역 생성자/소멸자 (call_global_ctors)
//  - 순수 가상 함수 스텁
//  - __cxa_atexit 스텁
//

extern "C" {
#include "../std/malloc.h"
#include "../hal/vga.h"
}

#include <stddef.h>
#include <stdint.h>

/* ── operator new / delete ───────────────────────────────────────────────── */

void *operator new(size_t size) {
    return kmalloc(size);
}

void *operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void *ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    kfree(ptr);
}

/* C++14 sized delete */
void operator delete(void *ptr, size_t /*size*/) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr, size_t /*size*/) noexcept {
    kfree(ptr);
}

/* ── 순수 가상 함수 스텁 ─────────────────────────────────────────────────── */
extern "C" void __cxa_pure_virtual(void) {
    klog_error("[C++ ABI] Pure virtual function called — kernel halt\n");
    asm volatile("cli; hlt");
    while (1) {}
}

/* ── __cxa_atexit 스텁 ───────────────────────────────────────────────────── */
extern "C" int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0; /* 커널에서는 no-op */
}

extern "C" void __cxa_finalize(void *) {}

/* ── 전역 생성자 호출 ────────────────────────────────────────────────────── */
// 링커 스크립트에 KEEP(*(SORT(.init_array*))) 및 KEEP(*(.ctors)) 가 있어야 합니다.
typedef void (*ctor_fn_t)(void);

extern "C" {
    extern ctor_fn_t __init_array_start[];
    extern ctor_fn_t __init_array_end[];
}

extern "C" void call_global_ctors(void) {
    for (ctor_fn_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        if (*fn) (*fn)();
    }
}

/* ── 예외 처리 스텁 (freestanding – 예외 비활성화 전제) ──────────────────── */
extern "C" void *__gxx_personality_v0;
void *__gxx_personality_v0 = nullptr;

extern "C" void _Unwind_Resume(void *) {
    while (1) {} /* 커널에서는 도달 불가 */
}
