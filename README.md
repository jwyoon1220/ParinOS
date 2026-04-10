# 🚀 ParinOS 개발 현황 및 로드맵

**ParinOS**는 C와 Assembly를 기반으로 한 x86 아키텍처 커스텀 운영체제입니다. 현재 기본적인 커널 인프라, 메모리 관리, 저장 장치 및 파일시스템을 갖추고 있으며, 프리엠티브 멀티태스킹 단계에 진입해 있습니다.

---

## ✅ 완료된 항목 (Achieved)

### 1. 부트로더 (2단계 부팅)
- **Stage 1 (boot.asm)**: MBR 부트로더 — 실모드 초기화, ATA 디스크 로딩, 보호 모드 전환.
- **Stage 2 (loader.c)**: 2단계 로더 — A20 게이트 활성화, 커널 이미지 디스크 로딩, 커널 진입.

### 2. 커널 기본 인프라
- **GDT/IDT 설정**: 시스템 보호 모드 진입 및 인터럽트 체계 구축.
- **VGA 드라이버**: `kprintf` 및 화면 지우기 등 80×25 텍스트 모드 출력 환경 완비.
- **PIT (Programmable Interval Timer)**: 1000Hz 주기의 정밀 타이머 구현.
- **시리얼 포트 드라이버**: COM1 시리얼 통신 (38400 baud) 구현.

### 3. 메모리 관리 시스템 (Memory Management)
- **PMM (Physical Memory Manager)**: 비트맵 기반 물리 프레임 할당자 구현.
- **VMM (Virtual Memory Manager)**: 페이징(Paging) 기법을 통한 32비트 4GB 가상 주소 공간 매핑, 페이지 폴트 자동 처리.
- **TLSF 힙 할당자 (kmalloc)** — *O(1) Two-Level Segregated Fit*:
    - `malloc` / `free` / `realloc` 모두 **최악의 경우 O(1)** 시간 복잡도 보장.
    - 두 단계 비트맵(FL·SL)으로 적합 블록을 단일 비트 스캔(`bsr`/`bsf`)으로 탐색.
    - 물리·가상 인접 블록 즉각 병합(coalescing)으로 단편화 억제.
    - 힙 부족 시 PMM/VMM을 통해 4페이지(16 KB) 단위로 자동 확장.
    - **메모리 누수 제거**: 좀비 스레드 스택을 `process_cleanup_pool` 에 등록,
      스케줄러가 컨텍스트 전환 직후 안전하게 `kfree()` 처리.

### 4. 하드웨어 드라이버
- **CMOS RTC 드라이버**: I/O 포트(0x70, 0x71) 제어 및 BCD 데이터 변환을 통한 실시간 시계 구현.
- **PCI 드라이버**: PCI 버스 스캔 및 장치 열거(vendor/device ID) 기능 구현.
- **AHCI 드라이버**: SATA 장치 감지 및 DMA 기반 섹터 읽기/쓰기 구현.

### 5. 저장 장치 추상화 및 파일 시스템
- **블록 디바이스 관리자**: AHCI를 제네릭 블록 디바이스로 등록하는 추상화 계층 구현.
- **FAT32 파일 시스템**:
    - **BPB(BIOS Parameter Block)** 분석: 클러스터 크기, 예약된 섹터 등 레이아웃 파악.
    - **FAT Table** 관리: 클러스터 체인 추적 및 빈 공간 할당.
    - **Directory Entry** 관리: 파일 이름(LFN 포함), 크기, 생성 시간 등 메타데이터 처리.
    - 파일 읽기/쓰기/탐색 지원.
    - [FAT32를 구현](https://github.com/strawberryhacker/fat32) 해주신 [@strawberryhacker](https://github.com/strawberryhacker)님께 감사드립니다.

### 6. 멀티태스킹 및 리소스 정리 (Multitasking & Resource Cleanup)
- **Context Switching**: IRQ0(타이머 인터럽트) 기반 프리엠티브 컨텍스트 스위칭 구현.
- **Round-Robin 스케줄러**: 10ms 타임 슬라이스 기반 커널 스레드/프로세스 스케줄링 (최대 32 스레드, 16 프로세스).
- **ELF 로더**: ELF32 실행 파일 파싱, VMM을 통한 세그먼트 로딩 및 실행 지원.
- **프로세스 정리 풀 (process_cleanup_pool)**:
    - `kthread_exit()` / `kprocess_exit()` 에서 종료 시 스택 포인터를 정리 풀에 등록.
    - `scheduler_tick()` 이 컨텍스트 전환 완료 후 `cleanup_dead_threads()` 를 호출,
      이전 스레드의 스택을 `kfree()` 하고 TCB 슬롯을 `KTHREAD_UNUSED` 로 초기화.
    - `kprocess_exit()` 는 PCB 슬롯을 즉시 반환, 모든 소속 스레드 스택을 풀에 일괄 등록.
    - **IRQ 스택 프레임 보호**: 현재 IRQ 가 올라온 스레드(`old_tid`)의 스택은
      `iret` 이 완료될 때까지 해제를 한 틱 유예하여 스택 손상을 방지.

### 7. 아키텍처 — 유저 모드 셸 + 커널 모드 스케줄러

```
┌─────────────────────────────────────────────────────┐
│                    Ring 0 (커널)                     │
│  ┌──────────┐  ┌──────────────────────────────────┐  │
│  │  kernel  │  │  scheduler_tick() + cleanup pool  │  │
│  │ (kmain)  │  │  → 컨텍스트 전환 후 좀비 스택 해제  │  │
│  └────┬─────┘  └──────────────────────────────────┘  │
│       │ kcreate_thread("shell")                       │
└───────┼─────────────────────────────────────────────-┘
        │ iret (Ring 3)
┌───────▼─────────────────────────────────────────────┐
│                    Ring 3 (유저)                     │
│  /bin/shell — 명령어 파싱, 프로그램 실행              │
│    └─ exec /bin/<program> → 새 프로세스 생성          │
│         └─ 종료 시 kprocess_exit() → 정리 풀 등록     │
└─────────────────────────────────────────────────────┘
```

- **커널 셸 제거**: `kernel.c` 의 `launch_shell()` 에서 커널 셸(`shell_init()`) 폴백 제거.
  `/bin/shell` 유저 프로세스가 항상 터미널 역할을 담당합니다.
- **프로그램만 Ring 3**: 셸 자체를 포함한 모든 사용자 프로그램은 Ring 3(유저 모드)에서 실행됩니다.

### 8. 인터랙티브 쉘 (Interactive Shell)
| 명령어 | 설명 |
|---|---|
| `help` | 사용 가능한 명령어 목록 출력 |
| `clear` | 화면 지우기 |
| `md <addr>` | 지정 주소의 메모리 덤프 출력 |
| `date` / `time` | 현재 날짜/시간 출력 (RTC) |
| `uptime` | PIT 틱 기반 시스템 가동 시간 출력 |
| `free` | TLSF 힙 메모리 사용량/잔여량 표시 |
| `cpuinfo` | CPUID를 사용한 CPU 제조사 및 기능 플래그 출력 |
| `pci_info` | PCI 장치 목록 출력 |
| `vmm_stat` | 가상 메모리 통계 출력 |
| `task_view` | 현재 실행 중인 태스크 목록 출력 |
| `fs` | 파일 시스템 정보 출력 |
| `ls` | 루트 디렉토리 파일 목록 출력 |
| `cat <path>` | 파일 내용 출력 (리다이렉션 지원) |
| `echo <text>` | 텍스트 출력 (파일 리다이렉션 지원) |
| `run <path>` | ELF 실행 파일 로드 및 실행 |
| `panic` | 의도적 커널 패닉 (테스트용) |
| `reboot` | 시스템 재부팅 |

---

## 📅 로드맵 현황 (Roadmap Status)

### ✅ 1단계: 사용자 모드 (User Mode) — **완료**
- [x] **GDT Ring 3 디스크립터**: 유저 코드(`0x1B`)/데이터(`0x23`) 세그먼트 추가, TSS 디스크립터(`0x28`) 등록 및 `ltr` 실행.
- [x] **TSS (Task State Segment)**: `tss_init` / `tss_set_kernel_stack` 구현. 스케줄러 전환 시마다 `TSS.esp0` 갱신.
- [x] **Ring 3 진입(iret)**: `elf_execute_in_ring3()` — 새 페이지 디렉토리 생성, ELF 세그먼트를 PAGE_USER 로 매핑, 유저 스택 구성 후 `iret`으로 Ring 3 전환.
- [x] **System Call (sysenter + int 0x80)**: 두 ABI 모두 지원. 유저 프로그램은 `sysenter` 사용(user/lib/syscall.asm).
- [x] **syscall_dispatch**: SYS_EXIT, SYS_EXEC, SYS_READ, SYS_WRITE, SYS_OPEN, SYS_CLOSE, SYS_LSEEK, SYS_GETPID, SYS_YIELD, SYS_UNLINK, SYS_MKDIR, SYS_OPENDIR, SYS_READDIR, SYS_CLOSEDIR 구현.
- [x] **프로세스 격리**: 프로세스별 독립 페이지 디렉토리(CR3) 생성. 스케줄러 전환 시 CR3 + MSR_SYSENTER_ESP 갱신.
- [x] **유저 스페이스 폴트 처리**: 유저 모드 페이지 폴트 시 커널 패닉 대신 프로세스 종료.

### ✅ 2단계: 파일 시스템 추상화 (VFS) — **완료**
- [x] **VFS 추상화 계층**: `vfs_open / vfs_read / vfs_write / vfs_close` 통일 인터페이스.
- [x] **FAT32 → VFS 어댑터**: `vfs_fat.c` — 기존 FAT32 드라이버를 VFS 노드로 래핑.
- [x] **syscall → VFS 연동**: `sys_open/read/write/close/lseek/unlink/mkdir` 가 VFS를 통해 FAT32로 라우팅.

### ✅ 3단계: 메모리 누수 해결 및 O(1) 할당자 — **완료**
- [x] **TLSF 힙 할당자**: O(1) Two-Level Segregated Fit 알고리즘으로 `kmalloc` / `kfree` 대체.
- [x] **process_cleanup_pool**: 좀비 스레드 스택 추적 및 지연 해제 메커니즘 구현.
- [x] **kthread_exit() 수정**: 스택을 정리 풀에 등록 후 hlt 대기.
- [x] **kprocess_exit() 수정**: 모든 소속 스레드 스택을 풀에 등록, PCB 즉시 반환.
- [x] **scheduler_tick() 수정**: 컨텍스트 전환 후 `cleanup_dead_threads(old_tid)` 호출.
- [x] **커널 셸 폴백 제거**: `launch_shell()` 에서 `shell_init()` 폴백 삭제, 유저 셸 전용 구조.

### 🚧 4단계: 스케줄링 고도화 (진행 예정)
- [ ] **우선순위 스케줄링**: 현재 Round-Robin에 우선순위 기반 스케줄링 추가.
- [ ] **IPC (Inter-Process Communication)**: 파이프, 공유 메모리 등.
- [ ] **fork**: 프로세스 복제 (현재는 exec 중심 모델).

---

## 🧪 테스트 (Manual QEMU Test)

```bash
# 빌드
make all

# QEMU 실행
make run
```

부팅 후 Ring 3 유저 셸이 자동 실행됩니다. 셸에서 다음 명령을 시도하세요:

```sh
ls                  # 루트 디렉터리 목록 (VFS → FAT32)
cat /readme.txt     # 파일 읽기
hello               # Ring 3 데모 프로그램 (VFS 시스콜 데모)
echo test > /t.txt  # 파일 쓰기
mkdir /testdir      # 디렉터리 생성
```

---

## 개발 환경
- **C17** / **C++17**
- **i686-linux-gnu-gcc** / **i686-linux-gnu-ld** / **i686-linux-gnu-g++**
- **NASM** (https://nasm.us)
- **QEMU** (에뮬레이터, `make run`으로 실행)
