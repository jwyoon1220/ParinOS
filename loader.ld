/* 2단계 C 로더 링커 스크립트
 * 로더는 1단계 부트로더(boot.asm)에 의해 0x10000에 로드된다.
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_loader_entry)

SECTIONS
{
    /* 로더는 0x10000 (64KB) 지점에 로드됨 */
    . = 0x10000;

    _loader_start = .;

    /* 실행 코드 */
    .text : AT(ADDR(.text))
    {
        *(.text)
    }

    /* 읽기 전용 데이터 */
    .rodata : AT(ADDR(.rodata))
    {
        *(.rodata)
    }

    /* 읽기/쓰기 데이터 */
    .data : AT(ADDR(.data))
    {
        *(.data)
    }

    /* 초기화되지 않은 데이터 */
    .bss : AT(ADDR(.bss))
    {
        *(.COMMON)
        *(.bss)
    }

    _loader_end = .;
}
