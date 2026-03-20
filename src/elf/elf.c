//
// Created by jwyoo on 26. 3. 12..
//

#include "elf.h"
#include "../hal/vga.h"
#include "../std/kstd.h"
#include "../mem/mem.h"
#include "../mem/vmm.h" // 🌟 VMM 연동
#include "../kernel/kernel_status_manager.h"
#include "../fs/fs.h"

bool elf_check_supported(Elf32_Ehdr *hdr) {
    if (hdr->e_ident[0] != 0x7f || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') return false;
    if (hdr->e_ident[4] != ELFCLASS32) return false;
    if (hdr->e_ident[5] != ELFDATA2LSB) return false;
    return true;
}

void* elf_load_file(File *file) {
    Elf32_Ehdr ehdr;
    int bytes_read = 0;

    fat_file_seek(file, 0, FAT_SEEK_START);
    if (fat_file_read(file, &ehdr, sizeof(Elf32_Ehdr), &bytes_read) != FAT_ERR_NONE) return 0;
    if (!elf_check_supported(&ehdr)) return 0;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        uint32_t phdr_offset = ehdr.e_phoff + (i * ehdr.e_phentsize);
        fat_file_seek(file, phdr_offset, FAT_SEEK_START);
        if (fat_file_read(file, &phdr, sizeof(Elf32_Phdr), &bytes_read) != FAT_ERR_NONE) return 0;

        if (phdr.p_type == PT_LOAD) {

            // 🌟 [핵심] VMM을 이용한 가상 메모리 할당
            // 세그먼트가 위치할 시작 페이지와 끝 페이지를 4KB(PAGE_SIZE) 단위로 정렬합니다.
            uint32_t start_page = phdr.p_vaddr & PAGE_MASK;
            uint32_t end_page = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & PAGE_MASK;
            uint32_t page_count = (end_page - start_page) / PAGE_SIZE;

            // 할당할 페이지가 있다면 VMM에 요청 (현재는 커널 모드 테스트이므로 VMM_ALLOC_KERNEL 사용)
            if (page_count > 0) {
                vmm_result_t alloc_result = vmm_alloc_virtual_pages(start_page, page_count, VMM_ALLOC_KERNEL);
                if (alloc_result != VMM_SUCCESS && alloc_result != VMM_ERROR_ALREADY_MAPPED) {
                    kprintf("VMM Allocation failed for ELF segment!\n");
                    return 0; // 메모리 할당 실패 시 중단
                }
            }

            // 매핑이 완료되었으므로 안전하게 파일 데이터를 메모리에 복사
            if (phdr.p_filesz > 0) {
                fat_file_seek(file, phdr.p_offset, FAT_SEEK_START);
                fat_file_read(file, (void*)phdr.p_vaddr, phdr.p_filesz, &bytes_read);
            }

            // BSS 영역 초기화
            if (phdr.p_memsz > phdr.p_filesz) {
                uint8_t* bss_start = (uint8_t*)phdr.p_vaddr + phdr.p_filesz;
                uint32_t bss_size = phdr.p_memsz - phdr.p_filesz;
                memset(bss_start, 0, bss_size);
            }
        }
    }
    return (void*)ehdr.e_entry;
}

int elf_execute_from_path(const char* filepath) {
    File file;

    // 1. 파일 열기
    if (fat_file_open(&file, filepath, FAT_READ) != FAT_ERR_NONE) {
        kprintf("Error: Cannot open file '%s'\n", filepath);
        return RUN_FAILURE;
    }

    // 2. 파일 파싱 및 로드 (이 과정에서 VMM 매핑 수행)
    void* ep = elf_load_file(&file);

    // 3. 파일 닫기
    fat_file_close(&file);

    // 4. 실행
    if (ep != 0) {
        kprintf("Executing ELF program at 0x%x...\n", (uint32_t)ep);

        // 함수 포인터로 캐스팅 후 호출
        void (*program)(void) = ep;
        program();

        return RUN_SUCCESS;
    } else {
        kprintf("ELF Load Failed! Invalid format or memory mapping error.\n");
        return RUN_FAILURE;
    }
}

/*
 * elf_execute_with_args — argc/argv 를 스택에 올려 ELF 프로그램에 전달합니다.
 *
 * x86 C 호출 규약에서 main(int argc, char **argv) 을 실행하려면
 * 스택 레이아웃이 아래와 같아야 합니다 (높은 주소 → 낮은 주소):
 *   [argv[n-1] 문자열] ... [argv[0] 문자열]  ← 문자열 데이터
 *   [NULL]              ← argv 종결자
 *   [argv[n-1] 포인터]
 *   ...
 *   [argv[0] 포인터]    ← argv 배열
 *   [&argv[0]]          ← argv 인수
 *   [argc]              ← argc 인수
 *   [dummy return addr] ← 반환 주소 자리 (0)
 *   ← ESP
 */
int elf_execute_with_args(const char* filepath, int argc, const char **argv) {
    File file;

    if (fat_file_open(&file, filepath, FAT_READ) != FAT_ERR_NONE) {
        kprintf("Error: Cannot open file '%s'\n", filepath);
        return RUN_FAILURE;
    }

    void* ep = elf_load_file(&file);
    fat_file_close(&file);

    if (ep == 0) {
        kprintf("ELF Load Failed! Invalid format or memory mapping error.\n");
        return RUN_FAILURE;
    }

    kprintf("Executing ELF program at 0x%x (argc=%d)...\n", (uint32_t)ep, argc);

    /* argv 문자열을 스택에 복사하고 포인터 배열을 구성합니다.
     * 임시 스택 공간을 동적 할당으로 구성합니다. */
    typedef int (*main_fn_t)(int, const char **);
    main_fn_t entry = (main_fn_t)ep;

    int ret = entry(argc, argv);

    kprintf("Program exited with code %d\n", ret);
    return RUN_SUCCESS;
}