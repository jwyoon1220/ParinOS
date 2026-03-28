/* userprog.ld — 유저 프로그램 링커 스크립트
 *
 * 유저 프로그램은 가상 주소 0x400000 (4MB) 에 로드됩니다.
 * ParinOS ELF 로더가 PT_LOAD 세그먼트를 p_vaddr 에 매핑합니다.
 */

OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
    . = 0x00400000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
