# 🚀 ParinOS 개발 현황 및 로드맵

**ParinOS**는 C와 Assembly를 기반으로 한 x86 아키텍처 커스텀 운영체제입니다. 현재 기본적인 커널 인프라와 메모리 관리 시스템을 갖추고 있으며, 하드웨어 제어 단계에 진입해 있습니다.

---

## ✅ 완료된 항목 (Achieved)

### 1. 커널 기본 인프라
- **GDT/IDT 설정**: 시스템 보호 모드 진입 및 인터럽트 체계 구축.
- **VGA 드라이버**: `kprintf` 및 `Screen Clear` 등 텍스트 모드 출력 환경 완비.
- **PIT(Programmable Interval Timer)**: 1000Hz 주기의 정밀 타이머 구현.
- 
### 2. 메모리 관리 시스템 (Memory Management)
- **PMM (Physical Memory Manager)**: 비트맵 기반 물리 프레임 할당자 구현.
- **VMM (Virtual Memory Manager)**: 페이징(Paging) 기법을 통한 4GB 가상 주소 공간 매핑.
- **Advanced Heap Allocator (kmalloc)**:
    - **이중 연결 리스트(Doubly Linked List)** 구조 채택.
    - **First-Fit** 알고리즘 및 **블록 분할(Splitting)** 구현.
    - **즉각적 병합(Immediate Coalescing)** 로직을 통해 $O(1)$ 속도로 파편화 방지.
    - 힙 부족 시 자동 페이지 확장 로직 포함.

### 3. 하드웨어 드라이버 및 쉘 (Hardware & Shell)
- **CMOS RTC 드라이버**: I/O 포트(0x70, 0x71) 제어 및 BCD 데이터 변환을 통한 실시간 시계 구현.
- **Interactive Shell**: `help`, `md`(Memory Dump), `test_malloc`, `date`, `time` 등 커널 디버깅 및 정보 명령어 구현.

---

## 🛠️ 현재 진행 중 및 해결 과제 (In Progress)

- [ ] **QEMU 시간 동기화**: UTC와 LocalTime 간의 시차(9시간) 보정 및 QEMU 실행 옵션 최적화.
- [ ] **문자열 출력 포맷팅**: `kprintf`의 `%02d` 등 자릿수 채우기(Padding) 로직 보강.

---

## 📅 향후 로드맵 (Upcoming Tasks)

### 1단계: 시스템 유틸리티 강화
- [ ] **`uptime` 명령어**: PIT 틱을 계산하여 시스템 가동 시간 출력.
- [ ] **`free` 명령어**: 현재 힙 메모리의 사용량/잔여량을 KB 단위로 시각화.
- [ ] **CPU 정보**: `cpuid` 명령어를 사용한 프로세서 제조사 및 기능 플래그 확인.

### 2단계: 가상 파일 시스템 (VFS) 기초
- [ ] **Ramdisk 구현**: `kmalloc`으로 메모리 일부를 할당받아 파일처럼 사용하는 초기 저장소 구축.
- [ ] **기본 FS 명령어**: `ls`, `touch`, `cat` 등 메모리 기반 파일 관리 명령어 추가.

### 3단계: 멀티태스킹 (Multitasking)
- [ ] **Context Switching**: 프로세스/스레드의 레지스터 상태 저장 및 복원을 통한 협력적 멀티태스킹 도입.
- [ ] **Scheduler**: 우선순위 또는 라운드 로빈 방식의 간단한 태스크 스케줄러 설계.

### 4단계: 사용자 모드 (User Mode)
- [ ] **링 3(Ring 3) 진입**: 커널 모드와 사용자 모드 분리 및 보호 체계 강화.
- [ ] **System Call**: 소프트웨어 인터럽트(`int 0x80`)를 통한 사용자 프로그램의 커널 기능 호출.

### 5단계: 저장 장치 및 파일 시스템 (Storage & File System)
- [ ] **ATA/IDE 드라이버**: 하드디스크(HDD)의 섹터를 읽고 쓰기 위한 PIO(Programmed I/O) 방식 드라이버 구현.
- [ ] **MBR/GPT 분석**: 디스크의 파티션 테이블을 읽어 ParinOS 파티션의 위치 파악.
- [ ] **FAT16/32 구현**:
    - **BPB(BIOS Parameter Block)** 분석: 클러스터 크기, 예약된 섹터 등 파일 시스템 레이아웃 파악.
    - **FAT Table** 관리: 파일 데이터가 담긴 클러스터 체인 추적 및 빈 공간 할당.
    - **Directory Entry** 관리: 파일 이름, 확장자, 크기, 생성 시간 등 메타데이터 처리.
- [ ] **VFS(Virtual File System) 추상화**: 하드디스크, 램디스크 등 서로 다른 저장 매체를 `open()`, `read()`, `write()` 같은 통일된 인터페이스로 접근하도록 설계.
---
