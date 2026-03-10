//
// Created by jwyoo on 26. 3. 10..
//

#include "sda.h"

#include "../vga.h"
#include "../std/malloc.h"
#include "../drivers/timer.h"
#include "../std/string.h"
#include "../mem/mem.h"

// Block Device Manager 전역 상태
static block_device_t* device_list_head = NULL;
static uint32_t device_count = 0;
static uint32_t next_device_id = 1;

// =============================================================================
// Block Device Manager 초기화
// =============================================================================

int block_device_manager_init(void) {
    device_list_head = NULL;
    device_count = 0;
    next_device_id = 1;
    
    kprintf("[SDA] Block Device Manager initialized\n");
    return 0;
}

// =============================================================================
// Block Device 등록 및 해제
// =============================================================================

int block_device_register(block_device_t* device) {
    if (!device || !device->ops) {
        kprintf("[SDA] Invalid device or operations\n");
        return BLOCK_ERROR_INVALID_PARAMETER;
    }
    
    // 장치 ID 할당
    device->device_id = next_device_id++;
    
    // 기본값 설정
    if (device->sector_size == 0) {
        device->sector_size = 512; // 기본 섹터 크기
    }
    
    if (device->block_size == 0) {
        device->block_size = device->sector_size;
    }

    uint64_t total_bytes = (uint64_t)device->total_sectors * (uint64_t)device->sector_size;
    device->capacity_mb = (uint32_t)(total_bytes / (1024ULL * 1024ULL));
    
    // 통계 초기화
    memset(&device->stats, 0, sizeof(block_device_stats_t));
    device->stats.last_access_time = tick;
    
    // 리스트에 추가
    device->next = device_list_head;
    device_list_head = device;
    device_count++;
    
    kprintf("[SDA] Registered device: %s (ID: %d, %d MB)\n", 
           device->name, device->device_id, device->capacity_mb);
    
    return BLOCK_SUCCESS;
}

int block_device_unregister(uint32_t device_id) {
    block_device_t** current = &device_list_head;
    
    while (*current) {
        if ((*current)->device_id == device_id) {
            block_device_t* device = *current;
            *current = device->next;
            
            // 드라이버별 정리 작업
            if (device->ops && device->ops->destroy) {
                device->ops->destroy(device);
            }
            
            device_count--;
            kprintf("[SDA] Unregistered device: %s (ID: %d)\n", 
                   device->name, device_id);
            return BLOCK_SUCCESS;
        }
        current = &(*current)->next;
    }
    
    return BLOCK_ERROR_INVALID_DEVICE;
}

// =============================================================================
// Block Device 검색
// =============================================================================

block_device_t* block_device_get(uint32_t device_id) {
    block_device_t* current = device_list_head;
    
    while (current) {
        if (current->device_id == device_id) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

block_device_t* block_device_get_by_name(const char* name) {
    if (!name) return NULL;
    
    block_device_t* current = device_list_head;
    
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

uint32_t block_device_get_count(void) {
    return device_count;
}

block_device_t* block_device_get_list(void) {
    return device_list_head;
}

// =============================================================================
// Block Device 조작 함수들
// =============================================================================

block_result_t block_device_read(uint32_t device_id, uint64_t lba, uint32_t count, void* buffer) {
    if (!buffer || count == 0) {
        return BLOCK_ERROR_INVALID_PARAMETER;
    }
    
    block_device_t* device = block_device_get(device_id);
    if (!device) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    if (!block_device_is_ready(device)) {
        return BLOCK_ERROR_NOT_READY;
    }
    
    if (block_device_validate_lba(device, lba, count) != 0) {
        return BLOCK_ERROR_OUT_OF_BOUNDS;
    }
    
    if (!device->ops || !device->ops->read) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    // 통계 업데이트
    device->stats.read_count++;
    device->stats.read_sectors += count;
    device->stats.last_access_time = tick;
    
    // 실제 읽기 수행
    int result = device->ops->read(device, lba, count, buffer);
    
    if (result != 0) {
        device->stats.read_errors++;
        return BLOCK_ERROR_READ_FAILED;
    }
    
    return BLOCK_SUCCESS;
}

block_result_t block_device_write(uint32_t device_id, uint64_t lba, uint32_t count, void* buffer) {
    if (!buffer || count == 0) {
        return BLOCK_ERROR_INVALID_PARAMETER;
    }
    
    block_device_t* device = block_device_get(device_id);
    if (!device) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    if (!block_device_is_ready(device)) {
        return BLOCK_ERROR_NOT_READY;
    }
    
    if (block_device_validate_lba(device, lba, count) != 0) {
        return BLOCK_ERROR_OUT_OF_BOUNDS;
    }
    
    if (!device->ops || !device->ops->write) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    // 통계 업데이트
    device->stats.write_count++;
    device->stats.write_sectors += count;
    device->stats.last_access_time = tick;
    
    // 실제 쓰기 수행
    int result = device->ops->write(device, lba, count, buffer);
    
    if (result != 0) {
        device->stats.write_errors++;
        return BLOCK_ERROR_WRITE_FAILED;
    }
    
    return BLOCK_SUCCESS;
}

block_result_t block_device_flush(uint32_t device_id) {
    block_device_t* device = block_device_get(device_id);
    if (!device) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    if (device->ops && device->ops->flush) {
        int result = device->ops->flush(device);
        return (result == 0) ? BLOCK_SUCCESS : BLOCK_ERROR_WRITE_FAILED;
    }
    
    return BLOCK_SUCCESS; // 플러시 기능이 없으면 성공으로 간주
}

block_result_t block_device_identify(uint32_t device_id, void* buffer) {
    if (!buffer) {
        return BLOCK_ERROR_INVALID_PARAMETER;
    }
    
    block_device_t* device = block_device_get(device_id);
    if (!device) {
        return BLOCK_ERROR_INVALID_DEVICE;
    }
    
    if (device->ops && device->ops->identify) {
        int result = device->ops->identify(device, buffer);
        return (result == 0) ? BLOCK_SUCCESS : BLOCK_ERROR_READ_FAILED;
    }
    
    return BLOCK_SUCCESS; // IDENTIFY 기능이 없으면 성공으로 간주
}

// =============================================================================
// 유틸리티 함수들
// =============================================================================

const char* block_device_type_to_string(block_device_type_t type) {
    switch (type) {
        case BLOCK_DEVICE_HDD:     return "HDD";
        case BLOCK_DEVICE_SSD:     return "SSD";
        case BLOCK_DEVICE_CDROM:   return "CD/DVD";
        case BLOCK_DEVICE_FLOPPY:  return "Floppy";
        case BLOCK_DEVICE_USB:     return "USB";
        case BLOCK_DEVICE_RAMDISK: return "RAMDisk";
        default:                   return "Unknown";
    }
}

const char* block_device_state_to_string(block_device_state_t state) {
    switch (state) {
        case BLOCK_DEVICE_OFFLINE: return "Offline";
        case BLOCK_DEVICE_READY:   return "Ready";
        case BLOCK_DEVICE_BUSY:    return "Busy";
        case BLOCK_DEVICE_ERROR:   return "Error";
        default:                   return "Unknown";
    }
}

const char* block_result_to_string(block_result_t result) {
    switch (result) {
        case BLOCK_SUCCESS:                    return "Success";
        case BLOCK_ERROR_INVALID_DEVICE:      return "Invalid Device";
        case BLOCK_ERROR_INVALID_PARAMETER:   return "Invalid Parameter";
        case BLOCK_ERROR_NOT_READY:           return "Not Ready";
        case BLOCK_ERROR_READ_FAILED:         return "Read Failed";
        case BLOCK_ERROR_WRITE_FAILED:        return "Write Failed";
        case BLOCK_ERROR_OUT_OF_BOUNDS:       return "Out of Bounds";
        case BLOCK_ERROR_NO_MEMORY:           return "No Memory";
        case BLOCK_ERROR_TIMEOUT:             return "Timeout";
        default:                              return "Unknown Error";
    }
}

// =============================================================================
// 검증 및 보안 함수들
// =============================================================================

int block_device_validate_lba(block_device_t* device, uint64_t lba, uint32_t count) {
    if (!device) return -1;
    
    if (lba >= device->total_sectors) return -1;
    if (lba + count > device->total_sectors) return -1;
    
    return 0;
}

int block_device_is_ready(block_device_t* device) {
    if (!device) return 0;
    return device->state == BLOCK_DEVICE_READY;
}

// =============================================================================
// 정보 출력 함수들
// =============================================================================

void block_device_print_info(block_device_t* device) {
    if (!device) return;
    
    kprintf("\n=== Block Device Info: %s ===\n", device->name);
    kprintf("ID:           %d\n", device->device_id);
    kprintf("Type:         %s\n", block_device_type_to_string(device->type));
    kprintf("State:        %s\n", block_device_state_to_string(device->state));
    kprintf("Capacity:     %d MB (%d sectors)\n", device->capacity_mb, device->total_sectors);
    kprintf("Sector Size:  %d bytes\n", device->sector_size);
    kprintf("Block Size:   %d bytes\n", device->block_size);
    
    if (device->vendor[0] != '\0') {
        kprintf("Vendor:       %s\n", device->vendor);
    }
    if (device->model[0] != '\0') {
        kprintf("Model:        %s\n", device->model);
    }
    if (device->serial[0] != '\0') {
        kprintf("Serial:       %s\n", device->serial);
    }
}

void block_device_print_stats(block_device_t* device) {
    if (!device) return;
    
    kprintf("\n=== Device Statistics: %s ===\n", device->name);
    kprintf("Read Operations:  %d (%d sectors)\n", 
           device->stats.read_count, device->stats.read_sectors);
    kprintf("Write Operations: %d (%d sectors)\n", 
           device->stats.write_count, device->stats.write_sectors);
    kprintf("Read Errors:      %d\n", device->stats.read_errors);
    kprintf("Write Errors:     %d\n", device->stats.write_errors);
    kprintf("Last Access:      %d ticks ago\n", 
           tick - device->stats.last_access_time);
    
    uint64_t total_kb_read = (device->stats.read_sectors * device->sector_size) / 1024;
    uint64_t total_kb_written = (device->stats.write_sectors * device->sector_size) / 1024;
    kprintf("Total Read:       %d KB\n", total_kb_read);
    kprintf("Total Written:    %d KB\n", total_kb_written);
}

void block_device_list_all(void) {
    kprintf("\n=== Block Device List ===\n");
    if (device_count == 0) {
        kprintf("No block devices found.\n");
        return;
    }
    
    kprintf("ID Name     Type    State   Capacity  Sectors\n");
    kprintf("-- -------- ------- ------- --------- --------\n");
    
    block_device_t* current = device_list_head;
    while (current) {
        kprintf("%2d %-8s %-7s %-7s %6d MB %8d\n",
               current->device_id,
               current->name,
               block_device_type_to_string(current->type),
               block_device_state_to_string(current->state),
               current->capacity_mb,
               current->total_sectors);
        current = current->next;
    }
    
    kprintf("\nTotal: %d devices\n", device_count);
}