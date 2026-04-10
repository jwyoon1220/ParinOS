//
// Created by jwyoon on 26. 3. 11..
//

#ifndef PARINOS_MULTITASKING_H
#define PARINOS_MULTITASKING_H

#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────────────────────────────────────
//  상수 및 기본값
// ─────────────────────────────────────────────────────────────────────────────
#define KTHREAD_MAX          32   // 동시에 존재할 수 있는 최대 스레드 수
#define KPROCESS_MAX         16   // 동시에 존재할 수 있는 최대 프로세스 수
#define DEFAULT_STACK_SIZE      8192 // 기본 스레드 스택 크기 (8KB)
#define MAX_THREADS_PER_PROCESS    8  // 프로세스당 최대 스레드 수
#define IRQ0_INT_NUM              32  // PIT 타이머 인터럽트 번호 (IRQ0 → IDT 32번)

// ─────────────────────────────────────────────────────────────────────────────
//  프로세스 정리 풀 (process cleanup pool)
//
//  문제: kthread_exit() / kprocess_exit() 는 현재 자신의 스택 위에서 실행
//  중이므로 즉시 스택을 해제할 수 없다.
//
//  해결: 해제해야 할 스택 포인터와 스레드 ID를 이 풀에 추가해두고,
//        scheduler_tick() 에서 컨텍스트 전환 완료 후 안전하게 해제한다.
//        해제를 요청한 스레드(old_tid)의 스택은 타이머 IRQ 스택 프레임이
//        아직 올라와 있으므로 다음 틱에서만 해제한다.
// ─────────────────────────────────────────────────────────────────────────────
#define CLEANUP_POOL_MAX  KTHREAD_MAX  // 정리 풀 최대 항목 수

/**
 * 좀비 스레드의 스택 해제 요청을 담는 엔트리.
 * scheduler_tick() 이 cleanup_dead_threads() 를 호출할 때 처리된다.
 */
typedef struct {
    uint8_t  *stack; // 해제할 스택 포인터 (NULL 이면 빈 슬롯)
    uint32_t  tid;   // 스레드 슬롯 ID (해제 후 KTHREAD_UNUSED 로 리셋)
} process_cleanup_entry_t;
// kthread_sleep / sleep 은 timer 를 1000Hz (1ms/tick)로 초기화한 경우에 정확합니다.

// ─────────────────────────────────────────────────────────────────────────────
//  스레드 상태 (Java/Kotlin 의 Thread.State 와 유사)
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    KTHREAD_UNUSED   = 0, // 슬롯이 비어 있음
    KTHREAD_READY    = 1, // 실행 준비 완료
    KTHREAD_RUNNING  = 2, // 현재 실행 중
    KTHREAD_SLEEPING = 3, // sleep() 호출로 대기 중
    KTHREAD_ZOMBIE   = 4  // 종료됨, 슬롯 정리 대기
} kthread_state_t;

// ─────────────────────────────────────────────────────────────────────────────
//  프로세스 상태
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    KPROCESS_UNUSED  = 0, // 슬롯이 비어 있음
    KPROCESS_RUNNING = 1, // 실행 중
    KPROCESS_ZOMBIE  = 2  // 종료됨
} kprocess_state_t;

// ─────────────────────────────────────────────────────────────────────────────
//  스레드 제어 블록 (TCB)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t        id;               // 고유 스레드 ID
    uint32_t        pid;              // 소속 프로세스 ID
    char            name[32];         // 스레드 이름 (디버깅용)
    kthread_state_t state;            // 현재 상태
    uint32_t        esp;              // 저장된 스택 포인터 (컨텍스트 전환 시)
    uint8_t*        stack;            // 커널 스택 기저 주소 (kmalloc으로 할당)
    uint32_t        stack_size;       // 커널 스택 크기
    uint32_t        sleep_until_tick; // 이 틱 이후에 깨어남 (SLEEPING 상태 전용)
    uint32_t        cr3;              // 페이지 디렉토리 물리 주소 (0 = 부트 PD 사용)
} kthread_t;

// ─────────────────────────────────────────────────────────────────────────────
//  프로세스 제어 블록 (PCB)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t         id;             // 고유 프로세스 ID
    char             name[32];       // 프로세스 이름
    kprocess_state_t state;          // 현재 상태
    uint32_t         thread_ids[MAX_THREADS_PER_PROCESS]; // 소속 스레드 ID 목록
    uint32_t         thread_count;   // 소속 스레드 수
} kprocess_t;

// ─────────────────────────────────────────────────────────────────────────────
//  멀티태스킹 초기화
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 멀티태스킹 시스템을 초기화합니다.
 * sti 이후, 메인 커널 루프 진입 전에 호출하세요.
 */
void init_multitasking(void);

// ─────────────────────────────────────────────────────────────────────────────
//  스레드 API  (Java: Thread / Kotlin: coroutine 스타일로 친숙하게)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 새 커널 스레드를 생성합니다.
 *
 * @param name        스레드 이름 (디버깅용, NULL 허용)
 * @param entry       스레드 진입점 함수
 * @param stack_size  스택 크기 (0 이면 DEFAULT_STACK_SIZE 사용)
 * @return 성공 시 스레드 ID (>= 0), 실패 시 -1
 *
 * Java: new Thread(entry).start()
 * Kotlin: thread(name = name) { entry() }
 */
int kcreate_thread(const char* name, void (*entry)(void), uint32_t stack_size);

/**
 * 현재 스레드를 종료합니다.
 * 스레드 함수가 반환될 때 자동으로 호출됩니다.
 *
 * Java: Thread.currentThread().interrupt() / return from run()
 */
void kthread_exit(void);

/**
 * 현재 스레드를 지정된 시간(ms)만큼 재웁니다.
 *
 * @param ms  대기할 시간 (밀리초, 타이머 틱 기준)
 *
 * Java: Thread.sleep(ms)
 */
void kthread_sleep(uint32_t ms);

/**
 * 현재 실행 중인 스레드의 TCB 포인터를 반환합니다.
 *
 * Java: Thread.currentThread()
 */
kthread_t* kthread_current(void);

/**
 * 현재 실행 중인 스레드의 ID를 반환합니다.
 */
int kthread_id(void);

// ─────────────────────────────────────────────────────────────────────────────
//  프로세스 API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 새 커널 프로세스를 생성합니다 (메인 스레드 1개 포함).
 *
 * @param name   프로세스 이름
 * @param entry  메인 스레드 진입점
 * @return 성공 시 프로세스 ID (>= 0), 실패 시 -1
 */
int kcreate_process(const char* name, void (*entry)(void));

/**
 * 현재 프로세스 및 소속 스레드를 모두 종료합니다.
 */
void kprocess_exit(void);

/**
 * 현재 프로세스의 PCB 포인터를 반환합니다.
 */
kprocess_t* kprocess_current(void);

/**
 * 현재 프로세스 ID를 반환합니다.
 */
int kprocess_id(void);

// ─────────────────────────────────────────────────────────────────────────────
//  편의 API  (Java/Kotlin 개발자 친화적 단축 함수)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 이름 없는 백그라운드 스레드를 즉시 시작합니다.
 *
 * @param func  실행할 함수
 * @return 스레드 ID (실패 시 -1)
 *
 * Kotlin: GlobalScope.launch { func() }
 * Java:   CompletableFuture.runAsync(func)
 */
int runAsync(void (*func)(void));

/**
 * 이름이 있는 백그라운드 스레드를 즉시 시작합니다.
 *
 * @param name  스레드 이름
 * @param func  실행할 함수
 * @return 스레드 ID (실패 시 -1)
 */
int runAsync_named(const char* name, void (*func)(void));

/**
 * CPU를 양보하고 다음 타이머 인터럽트까지 대기합니다 (협력적 양보).
 *
 * Java: Thread.yield()
 */
void kschedule(void);

/**
 * 스레드의 페이지 디렉토리(CR3)를 설정합니다.
 * elf_execute_in_ring3() 등에서 유저 프로세스 PD를 등록할 때 사용합니다.
 *
 * @param tid  대상 스레드 ID
 * @param cr3  페이지 디렉토리 물리 주소 (0 = 커널 부트 PD)
 */
void kthread_set_cr3(int tid, uint32_t cr3);

// ─────────────────────────────────────────────────────────────────────────────
//  어셈블리(IRQ0 핸들러)에서 호출되는 내부 함수
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 타이머 IRQ0 발생 시 어셈블리에서 호출됩니다.
 * 틱 증가, EOI 전송, 컨텍스트 전환을 수행합니다.
 *
 * @param current_esp  현재 스레드의 저장된 스택 포인터
 * @return             다음에 복원할 스택 포인터 (컨텍스트 전환 시 변경됨)
 */
uint32_t scheduler_tick(uint32_t current_esp);

void dump_multitasking_info(void);


#endif // PARINOS_MULTITASKING_H
