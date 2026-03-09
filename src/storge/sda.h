//
// Created by jwyoo on 26. 3. 10..
//

#ifndef PARINOS_SDA_H
#define PARINOS_SDA_H

#include <stdint.h>
#include <stddef.h>

#define MAX_BLOCK_DEVICES   16
#define BLOCK_DEVICE_NAME_LEN 32

// Block Device 타입
typedef enum {
    BLOCK_DEVICE_UNKNOWN = 0,
    BLOCK_DEVICE_HDD = 1,       // 하드 디스크
    BLOCK_DEVICE_SSD = 2,       // SSD
    BLOCK_DEVICE_CDROM = 3,     // CD/DVD
    BLOCK_DEVICE_FLOPPY = 4,    // 플로피 디스크
    BLOCK_DEVICE_USB = 5,       // USB 저장장치
    BLOCK_DEVICE_RAMDISK = 6    // RAM 디스크
} block_device_type_t;

// Block Device 상태
typedef enum {
    BLOCK_DEVICE_OFFLINE = 0,
    BLOCK_DEVICE_READY = 1,
    BLOCK_DEVICE_BUSY = 2,
    BLOCK_DEVICE_ERROR = 3
} block_device_state_t;

// Block Device 결과 코드
typedef enum {
    BLOCK_SUCCESS = 0,
    BLOCK_ERROR_INVALID_DEVICE = -1,
    BLOCK_ERROR_INVALID_PARAMETER = -2,
    BLOCK_ERROR_NOT_READY = -3,
    BLOCK_ERROR_READ_FAILED = -4,
    BLOCK_ERROR_WRITE_FAILED = -5,
    BLOCK_ERROR_OUT_OF_BOUNDS = -6,
    BLOCK_ERROR_NO_MEMORY = -7,
    BLOCK_ERROR_TIMEOUT = -8
} block_result_t;

// 전방 선언
struct block_device;

// Block Device 함수 포인터
typedef struct {
    int (*read)(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
    int (*write)(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
    int (*flush)(struct block_device* dev);  // 캐시된 데이터 플러시
    int (*identify)(struct block_device* dev, void* buffer);  // 장치 정보 읽기
    void (*destroy)(struct block_device* dev);  // 장치 제거 시 호출
} block_device_ops_t;

// Block Device 통계
typedef struct {
    uint64_t read_count;        // 읽기 요청 횟수
    uint64_t write_count;       // 쓰기 요청 횟수
    uint64_t read_sectors;      // 읽은 총 섹터 수
    uint64_t write_sectors;     // 쓴 총 섹터 수
    uint64_t read_errors;       // 읽기 오류 횟수
    uint64_t write_errors;      // 쓰기 오류 횟수
    uint32_t last_access_time;  // 마지막 접근 시간 (tick)
} block_device_stats_t;

// Block Device 구조체 (핵심)
typedef struct block_device {
    // 기본 정보
    char name[BLOCK_DEVICE_NAME_LEN];   // 장치 이름 (예: "hda", "sda0")
    uint32_t device_id;                 // 고유 장치 ID
    block_device_type_t type;           // 장치 타입
    block_device_state_t state;         // 현재 상태

    // 기하학적 정보
    uint64_t total_sectors;             // 총 섹터 수
    uint32_t sector_size;               // 섹터 크기 (보통 512)
    uint32_t block_size;                // 논리 블록 크기
    uint64_t capacity_mb;               // 용량 (MB)

    // 하드웨어 정보
    char vendor[16];                    // 제조사
    char model[32];                     // 모델명
    char serial[32];                    // 시리얼 번호

    // 함수 포인터 (드라이버별 구현)
    block_device_ops_t* ops;

    // 드라이버별 개인 데이터
    void* private_data;                 // AHCI, IDE 등의 장치별 데이터

    // 통계 및 모니터링
    block_device_stats_t stats;

    // 링크드 리스트를 위한 포인터
    struct block_device* next;
} block_device_t;

// Block Device Manager 함수들
int block_device_manager_init(void);
int block_device_register(block_device_t* device);
int block_device_unregister(uint32_t device_id);
block_device_t* block_device_get(uint32_t device_id);
block_device_t* block_device_get_by_name(const char* name);
uint32_t block_device_get_count(void);
block_device_t* block_device_get_list(void);

// Block Device 조작 함수들
block_result_t block_device_read(uint32_t device_id, uint64_t lba, uint32_t count, void* buffer);
block_result_t block_device_write(uint32_t device_id, uint64_t lba, uint32_t count, void* buffer);
block_result_t block_device_flush(uint32_t device_id);
block_result_t block_device_identify(uint32_t device_id, void* buffer);

// 유틸리티 함수들
const char* block_device_type_to_string(block_device_type_t type);
const char* block_device_state_to_string(block_device_state_t state);
const char* block_result_to_string(block_result_t result);

// 정보 출력 함수들
void block_device_print_info(block_device_t* device);
void block_device_print_stats(block_device_t* device);
void block_device_list_all(void);

// 검증 및 보안 함수들
int block_device_validate_lba(block_device_t* device, uint64_t lba, uint32_t count);
int block_device_is_ready(block_device_t* device);

#endif //PARINOS_SDA_H