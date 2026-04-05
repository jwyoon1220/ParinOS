//
// Created by jwyoon on 26. 3. 11..
//
// ParinOS 커널 멀티태스킹 구현
// ─ 선점형 라운드 로빈 스케줄러 (타이머 IRQ0 기반)
// ─ 커널/유저 모드 스레드 지원 (Ring 0 / Ring 3)
//

#include "multitasking.h"
#include "../std/malloc.h"
#include "../std/kstring.h"
#include "../hal/vga.h"
#include "../hal/io.h"
#include "../drivers/timer.h"
#include "../mem/mem.h"
#include "../mem/vmm.h"
#include "tss.h"

// SYSENTER ESP MSR (유저 스레드 전환 시 갱신)
#define MSR_SYSENTER_ESP 0x176

static inline void wrmsr_mt(uint32_t msr, uint32_t val) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(val), "d"(0));
}

// ─────────────────────────────────────────────────────────────────────────────
//  내부 전역 변수
// ─────────────────────────────────────────────────────────────────────────────

static kthread_t  threads[KTHREAD_MAX];
static kprocess_t processes[KPROCESS_MAX];

static uint32_t current_tid    = 0; // 현재 실행 중인 스레드의 인덱스
static uint32_t current_pid    = 0; // 현재 실행 중인 프로세스의 인덱스

// 멀티태스킹 활성화 플래그 (0 = 비활성, 1 = 활성)
static volatile int multitasking_enabled = 0;

// 스케줄링할 때 건너뛸 틱 수 (타이머가 너무 자주 스위치하지 않도록)
#define SCHED_INTERVAL 10 // 10ms마다 스위치
static uint32_t sched_counter = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  내부 헬퍼
// ─────────────────────────────────────────────────────────────────────────────

// 스레드 이름 복사 (NULL 안전)
static void copy_name(char* dest, const char* src, size_t max) {
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    size_t i;
    for (i = 0; i < max - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
//  초기 스택 프레임 설정
//
//  irq0_handler 가 복원하는 스택 레이아웃 (낮은 주소 → 높은 주소):
//   [DS]         ← ESP 가 가리키는 위치 (pop eax; mov ds, ax)
//   [EDI]        ← popa 가 순서대로 복원: EDI, ESI, EBP, (ESP 무시), EBX, EDX, ECX, EAX
//   [ESI]
//   [EBP]
//   [ESP_dummy]  ← popa 가 무시함
//   [EBX]
//   [EDX]
//   [ECX]
//   [EAX]
//   [int_no]     ← add esp, 8 로 건너뜀
//   [err_code]
//   [EIP]        ← iret 이 복원: EIP, CS, EFLAGS
//   [CS]
//   [EFLAGS]
//   [kthread_exit] ← entry() 가 ret 할 때의 반환 주소
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t setup_initial_stack(uint8_t* stack_base,
                                    uint32_t stack_size,
                                    void (*entry)(void)) {
    // 스택은 위에서 아래로 자라므로 최상단부터 시작
    uint32_t* sp = (uint32_t*)(stack_base + stack_size);

    // 1. entry() 가 반환될 때의 복귀 주소 → kthread_exit 자동 호출
    *(--sp) = (uint32_t)kthread_exit;

    // 2. CPU 가 iret 시 복원하는 3개 필드 (커널 모드, 같은 특권 수준)
    *(--sp) = 0x00000202;         // EFLAGS: IF=1 (인터럽트 허용), IOPL=0
    *(--sp) = 0x08;               // CS: 커널 코드 세그먼트
    *(--sp) = (uint32_t)entry;    // EIP: 진입점

    // 3. irq0_handler 가 push 하는 더미 필드 (add esp,8 로 정리됨)
    *(--sp) = IRQ0_INT_NUM;  // int_no (IRQ0 인터럽트 번호)
    *(--sp) = 0;   // err_code (더미)

    // 4. pusha 순서와 동일하게 레지스터 저장
    //    pusha push 순서: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    //    (스택은 아래로 자라므로 EDI 가 가장 낮은 주소에 위치)
    //    popa 복원 순서: EDI, ESI, EBP, (ESP 건너뜀), EBX, EDX, ECX, EAX
    *(--sp) = 0; // EAX
    *(--sp) = 0; // ECX
    *(--sp) = 0; // EDX
    *(--sp) = 0; // EBX
    *(--sp) = 0; // ESP (popa 가 무시)
    *(--sp) = 0; // EBP
    *(--sp) = 0; // ESI
    *(--sp) = 0; // EDI

    // 5. DS (irq0_handler 가 pop eax; mov ds, ax 로 복원)
    *(--sp) = 0x10; // 커널 데이터 세그먼트

    return (uint32_t)sp; // 저장할 ESP 값
}

// ─────────────────────────────────────────────────────────────────────────────
//  프로세스 슬롯 할당 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
static int alloc_process_slot(void) {
    for (int i = 0; i < KPROCESS_MAX; i++) {
        if (processes[i].state == KPROCESS_UNUSED) {
            return i;
        }
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  스레드 슬롯 할당 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
static int alloc_thread_slot(void) {
    for (int i = 0; i < KTHREAD_MAX; i++) {
        if (threads[i].state == KTHREAD_UNUSED) {
            return i;
        }
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  init_multitasking
// ─────────────────────────────────────────────────────────────────────────────
void init_multitasking(void) {
    // 모든 슬롯을 0으로 초기화 (name 필드 포함)
    memset(threads,   0, sizeof(threads));
    memset(processes, 0, sizeof(processes));
    // state 는 0 = KTHREAD_UNUSED / KPROCESS_UNUSED 이므로 위의 memset으로 충분

    // 커널 메인 프로세스 (PID 1, "init") 등록
    // Linux 관례: PID 1은 항상 init 프로세스
    processes[0].id           = 1;
    processes[0].state        = KPROCESS_RUNNING;
    processes[0].thread_count = 1;
    processes[0].thread_ids[0] = 0;
    copy_name(processes[0].name, "init", sizeof(processes[0].name));

    // 커널 메인 스레드 (TID 0) 등록
    // 실제 ESP 는 첫 번째 타이머 인터럽트에서 저장됨
    threads[0].id               = 0;
    threads[0].pid              = 1;
    threads[0].state            = KTHREAD_RUNNING;
    threads[0].esp              = 0; // 첫 번째 스위치 시 자동으로 채워짐
    threads[0].stack            = NULL; // 부트 스택 (kmalloc 으로 할당하지 않음)
    threads[0].stack_size       = 0;
    threads[0].sleep_until_tick = 0;
    copy_name(threads[0].name, "init", sizeof(threads[0].name));

    current_tid = 0;
    current_pid = 0;
    sched_counter  = 0;

    multitasking_enabled = 1;

    kprintf("[SCHED] Multitasking enabled (max threads=%d, max processes=%d)\n",
            KTHREAD_MAX, KPROCESS_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
//  kcreate_thread
// ─────────────────────────────────────────────────────────────────────────────
int kcreate_thread(const char* name, void (*entry)(void), uint32_t stack_size) {
    if (entry == NULL) return -1;
    if (stack_size == 0) stack_size = DEFAULT_STACK_SIZE;

    // 인터럽트 잠시 비활성화 (스레드 테이블 수정 원자성 보장)
    __asm__("cli");

    int slot = alloc_thread_slot();
    if (slot < 0) {
        __asm__("sti");
        kprintf("[SCHED] kcreate_thread: Out of Thread Slot\n");
        return -1;
    }

    // 스택 할당
    uint8_t* stack = (uint8_t*)kmalloc(stack_size);
    if (stack == NULL) {
        __asm__("sti");
        kprintf("[SCHED] kcreate_thread: Out Of Stack Memory\n");
        return -1;
    }

    // 초기 컨텍스트 프레임 설정
    uint32_t initial_esp = setup_initial_stack(stack, stack_size, entry);

    // TCB 채우기
    threads[slot].id               = (uint32_t)slot;
    threads[slot].pid              = current_pid;
    threads[slot].state            = KTHREAD_READY;
    threads[slot].esp              = initial_esp;
    threads[slot].stack            = stack;
    threads[slot].stack_size       = stack_size;
    threads[slot].sleep_until_tick = 0;
    copy_name(threads[slot].name, name, sizeof(threads[slot].name));

    // 부모 프로세스에 스레드 등록
    kprocess_t* proc = &processes[current_pid];
    if (proc->thread_count < MAX_THREADS_PER_PROCESS) {
        proc->thread_ids[proc->thread_count++] = (uint32_t)slot;
    }

    __asm__("sti");

    kprintf("[SCHED] Thread '%s' (tid=%d) created\n",
            threads[slot].name, slot);
    return slot;
}

// ─────────────────────────────────────────────────────────────────────────────
//  kthread_exit
// ─────────────────────────────────────────────────────────────────────────────
void kthread_exit(void) {
    __asm__("cli");

    threads[current_tid].state = KTHREAD_ZOMBIE;
    // 스택은 여기서 해제하지 않음 (현재 이 스택 위에서 실행 중이므로)
    // 다음 스케줄러 호출 시 이 스레드는 선택되지 않음

    __asm__("sti");

    // 다음 타이머 인터럽트가 발생할 때까지 hlt 로 대기
    // 인터럽트 발생 → scheduler_tick 에서 다른 스레드로 전환됨
    while (1) {
        __asm__("hlt");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  kthread_sleep
// ─────────────────────────────────────────────────────────────────────────────
void kthread_sleep(uint32_t ms) {
    if (ms == 0) return;
    // 참고: tick 은 init_timer(1000) 기준 1ms 단위입니다.
    __asm__("cli");
    threads[current_tid].state            = KTHREAD_SLEEPING;
    threads[current_tid].sleep_until_tick = tick + ms;
    __asm__("sti");

    // 잠들 때까지 hlt 반복
    while (threads[current_tid].state == KTHREAD_SLEEPING) {
        __asm__("hlt");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  kthread_current / kthread_id
// ─────────────────────────────────────────────────────────────────────────────
kthread_t* kthread_current(void) {
    return &threads[current_tid];
}

int kthread_id(void) {
    return (int)current_tid;
}

// ─────────────────────────────────────────────────────────────────────────────
//  kcreate_process
// ─────────────────────────────────────────────────────────────────────────────
int kcreate_process(const char* name, void (*entry)(void)) {
    if (entry == NULL) return -1;

    __asm__("cli");

    int pslot = alloc_process_slot();
    if (pslot < 0) {
        __asm__("sti");
        kprintf("[SCHED] kcreate_process: 프로세스 슬롯 부족!\n");
        return -1;
    }

    // PCB 초기화
    processes[pslot].id           = (uint32_t)pslot;
    processes[pslot].state        = KPROCESS_RUNNING;
    processes[pslot].thread_count = 0;
    copy_name(processes[pslot].name, name, sizeof(processes[pslot].name));

    // 메인 스레드 슬롯 찾기
    int tslot = alloc_thread_slot();
    if (tslot < 0) {
        processes[pslot].state = KPROCESS_UNUSED;
        __asm__("sti");
        kprintf("[SCHED] kcreate_process: Out of Thread Slot!\n");
        return -1;
    }

    // 스택 할당
    uint8_t* stack = (uint8_t*)kmalloc(DEFAULT_STACK_SIZE);
    if (stack == NULL) {
        processes[pslot].state = KPROCESS_UNUSED;
        __asm__("sti");
        kprintf("[SCHED] kcreate_process: Out Of Stack Memory!\n");
        return -1;
    }

    uint32_t initial_esp = setup_initial_stack(stack, DEFAULT_STACK_SIZE, entry);

    // TCB 채우기
    threads[tslot].id               = (uint32_t)tslot;
    threads[tslot].pid              = (uint32_t)pslot;
    threads[tslot].state            = KTHREAD_READY;
    threads[tslot].esp              = initial_esp;
    threads[tslot].stack            = stack;
    threads[tslot].stack_size       = DEFAULT_STACK_SIZE;
    threads[tslot].sleep_until_tick = 0;
    copy_name(threads[tslot].name, name, sizeof(threads[tslot].name));

    // 프로세스에 메인 스레드 등록
    processes[pslot].thread_ids[0] = (uint32_t)tslot;
    processes[pslot].thread_count  = 1;

    __asm__("sti");

    kprintf("[SCHED] Process '%s' (pid=%d, main_tid=%d) created\n",
            processes[pslot].name, pslot, tslot);
    return pslot;
}

// ─────────────────────────────────────────────────────────────────────────────
//  kprocess_exit
// ─────────────────────────────────────────────────────────────────────────────
void kprocess_exit(void) {
    __asm__("cli");

    kprocess_t* proc = &processes[current_pid];
    // 소속 스레드를 모두 ZOMBIE 로 표시
    for (uint32_t i = 0; i < proc->thread_count; i++) {
        uint32_t tid = proc->thread_ids[i];
        if (tid < KTHREAD_MAX) {
            threads[tid].state = KTHREAD_ZOMBIE;
        } else {
            kprintf("[SCHED] kprocess_exit: invalid tid=%d (data corruption?)", tid);
        }
    }
    proc->state = KPROCESS_ZOMBIE;

    __asm__("sti");

    while (1) {
        __asm__("hlt");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  kprocess_current / kprocess_id
// ─────────────────────────────────────────────────────────────────────────────
kprocess_t* kprocess_current(void) {
    return &processes[current_pid];
}

int kprocess_id(void) {
    return (int)current_pid;
}

// ─────────────────────────────────────────────────────────────────────────────
//  runAsync / runAsync_named
// ─────────────────────────────────────────────────────────────────────────────
int runAsync(void (*func)(void)) {
    return kcreate_thread(NULL, func, DEFAULT_STACK_SIZE);
}

int runAsync_named(const char* name, void (*func)(void)) {
    return kcreate_thread(name, func, DEFAULT_STACK_SIZE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  kschedule  (협력적 양보)
// ─────────────────────────────────────────────────────────────────────────────
void kschedule(void) {
    // 다음 타이머 인터럽트를 기다려 스케줄러가 전환하도록 함
    __asm__("sti; hlt");
}

// ─────────────────────────────────────────────────────────────────────────────
//  scheduler_tick  ← irq0_handler 에서 호출됨
//
//  반환값: 다음에 복원할 ESP
//   - 전환 없음 → current_esp 그대로 반환
//   - 전환 있음 → 다음 스레드의 저장된 ESP 반환
// ─────────────────────────────────────────────────────────────────────────────
uint32_t scheduler_tick(uint32_t current_esp) {
    // 1. 틱 증가 (timer.h 에서 extern volatile uint32_t tick)
    tick++;

    // 2. PIC 마스터에 EOI 전송 (타이머 IRQ0)
    outb(0x20, 0x20);

    // 3. 멀티태스킹 비활성 또는 단일 스레드면 즉시 반환
    if (!multitasking_enabled) {
        return current_esp;
    }

    // 4. 스케줄링 주기 조절 (매 SCHED_INTERVAL 틱마다 전환)
    sched_counter++;
    if (sched_counter < SCHED_INTERVAL) {
        return current_esp;
    }
    sched_counter = 0;

    // 5. 슬리핑 스레드 깨우기
    for (int i = 0; i < KTHREAD_MAX; i++) {
        if (threads[i].state == KTHREAD_SLEEPING &&
            tick >= threads[i].sleep_until_tick) {
            threads[i].state = KTHREAD_READY;
        }
    }

    // 6. 현재 스레드 ESP 저장
    threads[current_tid].esp = current_esp;

    // 7. 라운드 로빈: 다음 실행 가능한 스레드 탐색
    uint32_t next = (current_tid + 1) % KTHREAD_MAX;
    int searched = 0;
    while (searched < KTHREAD_MAX) {
        kthread_state_t s = threads[next].state;
        if (s == KTHREAD_READY || s == KTHREAD_RUNNING) {
            break;
        }
        next = (next + 1) % KTHREAD_MAX;
        searched++;
    }

    // 실행 가능한 스레드가 없으면 현재 스레드 유지
    if (searched >= KTHREAD_MAX) {
        return current_esp;
    }

    // 8. 동일 스레드면 전환 없이 반환
    if (next == current_tid) {
        threads[current_tid].state = KTHREAD_RUNNING;
        return current_esp;
    }

    // 9. 상태 전환
    if (threads[current_tid].state == KTHREAD_RUNNING) {
        threads[current_tid].state = KTHREAD_READY;
    }
    threads[next].state = KTHREAD_RUNNING;
    current_tid = next;
    current_pid = threads[current_tid].pid;

    // 10. 페이지 디렉토리(CR3) 전환
    //     cr3 == 0 이면 부트(커널) 페이지 디렉토리 사용
    {
        uint32_t next_cr3 = threads[next].cr3;
        if (next_cr3 == 0) next_cr3 = vmm_get_boot_dir_phys();

        uint32_t cur_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cur_cr3));
        if (cur_cr3 != next_cr3) {
            vmm_switch_page_dir(next_cr3);
        }
    }

    // 11. TSS.esp0 와 SYSENTER MSR 를 새 스레드의 커널 스택 최상단으로 갱신
    //     유저 스레드에서 인터럽트/시스콜 발생 시 이 스택으로 전환됨
    if (threads[next].stack != NULL) {
        uint32_t kstack_top = (uint32_t)(threads[next].stack + threads[next].stack_size);
        tss_set_kernel_stack(kstack_top);
        wrmsr_mt(MSR_SYSENTER_ESP, kstack_top);
    }

    // 12. 새 스레드의 ESP 반환 → irq0_handler 가 mov esp, eax 로 전환
    return threads[current_tid].esp;
}

// ─────────────────────────────────────────────────────────────────────────────
//  kthread_set_cr3
// ─────────────────────────────────────────────────────────────────────────────
void kthread_set_cr3(int tid, uint32_t cr3) {
    if (tid < 0 || tid >= KTHREAD_MAX) return;
    threads[tid].cr3 = cr3;
}

void dump_multitasking_info(void) {
    kprintf("\n--- Task Viewer ---\n");

    for (int p = 0; p < KPROCESS_MAX; p++) {
        if (processes[p].state == KPROCESS_UNUSED) continue;

        const char* p_state = (processes[p].state == KPROCESS_RUNNING) ? "RUNNING" : "ZOMBIE";

        kprintf("[%d] %s (Status: %s)\n", processes[p].id, processes[p].name, p_state);

        for (uint32_t i = 0; i < processes[p].thread_count; i++) {
            uint32_t tid = processes[p].thread_ids[i];
            kthread_t* t = &threads[tid];

            const char* t_state;
            switch(t->state) {
                case KTHREAD_READY:    t_state = "READY";    break;
                case KTHREAD_RUNNING:  t_state = "RUNNING";  break;
                case KTHREAD_SLEEPING: t_state = "SLEEPING"; break;
                case KTHREAD_ZOMBIE:   t_state = "ZOMBIE";   break;
                default:               t_state = "UNUSED";   break;
            }

            const char* branch = (i == processes[p].thread_count - 1) ? " `---" : " |---";

            // 포인터(%p)와 너비 지정(%d, %s)을 적극 활용
            kprintf("%s [TID:%d] %s | %s | Stack: %p\n",
                    branch, t->id, t->name[0] ? t->name : "(null)", t_state, t->esp);
        }
    }
    kprintf("-----------------------------------\n");
}
