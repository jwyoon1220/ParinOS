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
- **Advanced Heap Allocator (kmalloc)**:
    - **이중 연결 리스트(Doubly Linked List)** 구조 채택.
    - **First-Fit** 알고리즘 및 **블록 분할(Splitting)** 구현.
    - **즉각적 병합(Immediate Coalescing)** 로직을 통해 $O(1)$ 속도로 파편화 방지.
    - 힙 부족 시 자동 페이지 확장 로직 포함.

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

### 6. 멀티태스킹 (Multitasking)
- **Context Switching**: IRQ0(타이머 인터럽트) 기반 프리엠티브 컨텍스트 스위칭 구현.
- **Round-Robin 스케줄러**: 10ms 타임 슬라이스 기반 커널 스레드/프로세스 스케줄링 (최대 32 스레드, 16 프로세스).
- **ELF 로더**: ELF32 실행 파일 파싱, VMM을 통한 세그먼트 로딩 및 실행 지원.

### 7. 인터랙티브 쉘 (Interactive Shell)
| 명령어 | 설명 |
|---|---|
| `help` | 사용 가능한 명령어 목록 출력 |
| `clear` | 화면 지우기 |
| `md <addr>` | 지정 주소의 메모리 덤프 출력 |
| `date` / `time` | 현재 날짜/시간 출력 (RTC) |
| `uptime` | PIT 틱 기반 시스템 가동 시간 출력 |
| `free` | 힙 메모리 사용량/잔여량 표시 |
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

## 📅 향후 로드맵 (Upcoming Tasks)

### 1단계: 사용자 모드 (User Mode)
- [ ] **링 3(Ring 3) 진입**: GDT에 Ring 3 코드/데이터 디스크립터 추가 및 `iret`을 통한 사용자 모드 전환.
- [ ] **System Call**: 소프트웨어 인터럽트(`int 0x80`)를 통한 사용자 프로그램의 커널 기능 호출.
- [ ] **프로세스 격리**: 사용자 프로세스 간 메모리 보호 및 Ring 0/Ring 3 권한 분리.

### 2단계: 파일 시스템 추상화
- [ ] **VFS(Virtual File System) 추상화**: 하드디스크, 램디스크 등 서로 다른 저장 매체를 `open()`, `read()`, `write()` 같은 통일된 인터페이스로 접근하도록 설계.

### 3단계: 스케줄링 고도화
- [ ] **우선순위 스케줄링**: 현재 Round-Robin에 우선순위 기반 스케줄링 추가.
- [ ] **IPC (Inter-Process Communication)**: 프로세스 간 통신 메커니즘 (파이프 등) 구현.

---

## 개발 환경
- **C17**
- **i686-linux-gnu-gcc** / **i686-linux-gnu-ld**
- **NASM** (https://nasm.us)
- **QEMU** (에뮬레이터, `make run`으로 실행)
