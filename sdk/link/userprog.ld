/* sdk/link/userprog.ld — ParinOS User Program Linker Script
 *
 * User programs are loaded at virtual address 0x400000 (4 MB).
 * The ParinOS ELF loader maps each PT_LOAD segment to its p_vaddr.
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
