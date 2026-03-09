/* 출력 포맷은 32비트 ELF (나중에 binary로 변환) */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start) /* kernel_entry.asm의 진입점 이름 */

SECTIONS
{
    /* 커널은 부트로더에 의해 0x1000 (4KB) 지점에 로드됨 */
    . = 0x10000;

    /* 커널의 시작 주소를 나타내는 심볼 */
    _kernel_start = .;

    /* 1. 실행 코드 섹션 */
    .text : AT(ADDR(.text))
    {
        *(.text)
    }

    /* 2. 읽기 전용 데이터 (상수 등) */
    .rodata : AT(ADDR(.rodata))
    {
        *(.rodata)
    }

    /* 3. 읽기/쓰기 데이터 (초기화된 변수) */
    .data : AT(ADDR(.data))
    {
        *(.data)
    }

    /* 4. BSS (초기화되지 않은 변수, 0으로 채워짐) */
    .bss : AT(ADDR(.bss))
    {
        *(.COMMON)
        *(.bss)
    }

    /* 커널의 끝 주소를 나타내는 심볼 */
    _kernel_end = .;
}