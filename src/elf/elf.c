//
// Created by jwyoo on 26. 3. 12..
//

#include "elf.h"
#include "../hal/vga.h"
#include "../std/kstd.h"
#include "../mem/mem.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../kernel/kernel_status_manager.h"
#include "../kernel/multitasking.h"
#include "../kernel/tss.h"
#include "../fs/fs.h"
#include "../user/user.h"

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

            // VMM을 이용한 가상 메모리 할당
            uint32_t start_page = phdr.p_vaddr & PAGE_MASK;
            uint32_t end_page = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & PAGE_MASK;
            uint32_t page_count = (end_page - start_page) / PAGE_SIZE;

            // 유저 플래그로 페이지 할당 (PAGE_USER: Ring 3 에서 접근 가능)
            if (page_count > 0) {
                vmm_result_t alloc_result = vmm_alloc_virtual_pages(start_page, page_count, VMM_ALLOC_USER);
                if (alloc_result != VMM_SUCCESS && alloc_result != VMM_ERROR_ALREADY_MAPPED) {
                    kprintf("VMM Allocation failed for ELF segment!\n");
                    return 0;
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
 * (커널 모드(Ring 0)에서 직접 실행 — sys_exec 내부 용)
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

    typedef int (*main_fn_t)(int, const char **);
    main_fn_t entry = (main_fn_t)ep;

    int ret = entry(argc, argv);

    kprintf("Program exited with code %d\n", ret);
    return RUN_SUCCESS;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * enter_ring3 — iret 을 사용하여 Ring 0 → Ring 3 전환을 수행합니다.
 *
 * iret 스택 레이아웃 (특권 레벨 변경 시):
 *   [SS]     ← 유저 데이터 세그먼트 (0x23 = 0x20 | RPL3)
 *   [ESP]    ← 유저 스택 포인터
 *   [EFLAGS] ← 인터럽트 허용(IF) 플래그 포함
 *   [CS]     ← 유저 코드 세그먼트 (0x1B = 0x18 | RPL3)
 *   [EIP]    ← 유저 진입점
 *
 * 이 함수는 절대 반환하지 않습니다.
 * ─────────────────────────────────────────────────────────────────────────────*/
static void __attribute__((noreturn))
enter_ring3(uint32_t entry_point, uint32_t user_esp) {
    __asm__ volatile (
        "cli\n\t"
        /* 유저 데이터 세그먼트로 DS/ES/FS/GS 전환 */
        "mov $0x23, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        /* iret 프레임 구성: SS, ESP, EFLAGS, CS, EIP */
        "push $0x23\n\t"          /* SS:  유저 데이터 선택자 */
        "push %1\n\t"             /* ESP: 유저 스택 포인터  */
        "pushf\n\t"
        "pop %%eax\n\t"
        "or $0x200, %%eax\n\t"   /* IF=1: 인터럽트 허용    */
        "push %%eax\n\t"          /* EFLAGS                  */
        "push $0x1B\n\t"          /* CS:  유저 코드 선택자   */
        "push %0\n\t"             /* EIP: 유저 진입점        */
        "iret\n\t"
        : : "r"(entry_point), "r"(user_esp) : "eax", "memory"
    );
    __builtin_unreachable();
}

/* ─────────────────────────────────────────────────────────────────────────────
 * elf_execute_in_ring3 — ELF 파일을 유저 주소 공간에 로드하고 Ring 3로 진입합니다.
 *
 * 동작 순서:
 *  1) 유저 프로세스용 페이지 디렉토리 생성 (커널 매핑 공유)
 *  2) 새 페이지 디렉토리로 CR3 전환
 *  3) ELF PT_LOAD 세그먼트를 PAGE_USER 플래그로 매핑
 *  4) 유저 스택(USER_STACK_TOP) PAGE_USER 플래그로 할당
 *  5) argc/argv 를 유저 스택에 배치
 *  6) TSS.esp0 를 현재 커널 스택 최상단으로 설정
 *  7) iret 으로 Ring 3 진입 (절대 반환하지 않음)
 *
 * @param filepath   ELF 파일 경로
 * @param argc       인수 개수
 * @param argv       인수 문자열 배열
 * ─────────────────────────────────────────────────────────────────────────────*/
void __attribute__((noreturn))
elf_execute_in_ring3(const char* filepath, int argc, const char **argv) {
    /* 1. 유저 페이지 디렉토리 생성 */
    uint32_t user_pd = vmm_clone_kernel_dir();
    if (user_pd == 0) {
        kprintf("[ELF] 유저 페이지 디렉토리 생성 실패\n");
        kthread_exit();
        __builtin_unreachable();
    }

    /* 2. 유저 페이지 디렉토리로 CR3 전환
     *    이후 모든 vmm_alloc_virtual_pages 는 이 PD 에 반영됨 */
    vmm_switch_page_dir(user_pd);

    /* 현재 스레드 CR3 등록 (스케줄러가 전환 시 복원할 수 있도록) */
    kthread_set_cr3(kthread_id(), user_pd);

    /* 3. ELF 로드 (elf_load_file 이 VMM_ALLOC_USER 로 세그먼트 매핑) */
    File file;
    if (fat_file_open(&file, filepath, FAT_READ) != FAT_ERR_NONE) {
        kprintf("[ELF] 파일 열기 실패: %s\n", filepath);
        vmm_switch_page_dir(vmm_get_boot_dir_phys());
        kthread_exit();
        __builtin_unreachable();
    }

    void* entry_ptr = elf_load_file(&file);
    fat_file_close(&file);

    if (entry_ptr == 0) {
        kprintf("[ELF] ELF 로드 실패: %s\n", filepath);
        vmm_switch_page_dir(vmm_get_boot_dir_phys());
        kthread_exit();
        __builtin_unreachable();
    }

    uint32_t entry_point = (uint32_t)entry_ptr;
    kprintf("[ELF] 유저 프로세스 로드 완료: entry=0x%x\n", entry_point);

    /* 4. 유저 스택 할당 (USER_STACK_TOP 기준 하향 성장, PAGE_USER) */
    uint32_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uint32_t stack_base  = USER_STACK_TOP - USER_STACK_SIZE;

    if (vmm_alloc_virtual_pages(stack_base, stack_pages, VMM_ALLOC_USER) != VMM_SUCCESS) {
        kprintf("[ELF] 유저 스택 할당 실패\n");
        vmm_switch_page_dir(vmm_get_boot_dir_phys());
        kthread_exit();
        __builtin_unreachable();
    }

    /* 5. argc/argv 를 유저 스택에 배치
     *
     *  유저 스택 레이아웃 (높은 주소 → 낮은 주소, cdecl main 호환):
     *    argv[n-1] 문자열 ... argv[0] 문자열  ← 문자열 데이터
     *    NULL                                  ← argv 배열 종결자
     *    ptr_to_argv[n-1] ... ptr_to_argv[0]  ← argv 포인터 배열
     *    &argv[0]                              ← main 의 argv 인수
     *    argc                                  ← main 의 argc 인수
     *    0                                     ← 더미 반환 주소
     *    ← ESP
     */
    uint32_t user_sp = USER_STACK_TOP;

    /* argv 문자열을 스택 상단에 복사 */
    char* arg_ptrs[32];
    int n = (argc > 31) ? 31 : argc;

    for (int i = n - 1; i >= 0; i--) {
        const char* s = argv[i] ? argv[i] : "";
        uint32_t len = 0;
        while (s[len]) len++;
        len++; /* NULL 포함 */
        user_sp -= len;
        /* 스택은 유저 PD 에 매핑되어 있으므로 직접 접근 가능 */
        char* dst = (char*)user_sp;
        for (uint32_t j = 0; j < len; j++) dst[j] = s[j];
        arg_ptrs[i] = dst;
    }

    /* 4바이트 정렬 */
    user_sp &= ~3U;

    /* argv 포인터 배열 (NULL 종결) */
    user_sp -= sizeof(char*);
    *(char**)user_sp = (char*)0;
    for (int i = n - 1; i >= 0; i--) {
        user_sp -= sizeof(char*);
        *(char**)user_sp = arg_ptrs[i];
    }

    char** argv_ptr = (char**)user_sp;

    /* main 인수: argv, argc, 더미 반환 주소 */
    user_sp -= sizeof(char**);
    *(char***)user_sp = argv_ptr;

    user_sp -= sizeof(int);
    *(int*)user_sp = n;

    user_sp -= sizeof(uint32_t);
    *(uint32_t*)user_sp = 0; /* 더미 반환 주소 */

    /* 6. TSS.esp0 를 현재 커널 스택 최상단으로 설정
     *    Ring 3 → Ring 0 전환(인터럽트/시스콜) 시 CPU 가 이 스택을 사용함 */
    kthread_t* cur = kthread_current();
    if (cur && cur->stack) {
        uint32_t kstack_top = (uint32_t)(cur->stack + cur->stack_size);
        tss_set_kernel_stack(kstack_top);
    }

    kprintf("[ELF] Ring 3 진입: entry=0x%x esp=0x%x\n", entry_point, user_sp);

    /* 7. iret 으로 Ring 3 진입 */
    enter_ring3(entry_point, user_sp);
}