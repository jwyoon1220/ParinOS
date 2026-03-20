//
// Created by jwyoo on 26. 3. 10..
//

#include "ahci_adaptor.h"
#include "../drivers/ahci.h"
#include "../hal/vga.h"
#include "../std/malloc.h"
#include "../std/string.h"

// =============================================================================
// AHCI Block Device Operations
// =============================================================================

static int ahci_block_read(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->private_data) return -1;

    ahci_block_device_data_t* data = (ahci_block_device_data_t*)dev->private_data;
    return ahci_read_sectors(data->ahci_device, lba, count, buffer) ? 0 : -1;
}

static int ahci_block_write(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->private_data) return -1;

    ahci_block_device_data_t* data = (ahci_block_device_data_t*)dev->private_data;
    return ahci_write_sectors(data->ahci_device, lba, count, buffer) ? 0 : -1;
}

static int ahci_block_identify(struct block_device* dev, void* buffer) {
    if (!dev || !dev->private_data) return -1;

    ahci_block_device_data_t* data = (ahci_block_device_data_t*)dev->private_data;
    return ahci_identify(data->ahci_device, buffer) ? 0 : -1;
}

static void ahci_block_destroy(struct block_device* dev) {
    if (dev && dev->private_data) {
        kfree(dev->private_data);
        dev->private_data = NULL;
    }
}

// AHCI Block Device Operations 테이블
static block_device_ops_t ahci_block_ops = {
    .read = ahci_block_read,
    .write = ahci_block_write,
    .flush = NULL, // AHCI에서는 자동으로 플러시됨
    .identify = ahci_block_identify,
    .destroy = ahci_block_destroy
};

// =============================================================================
// AHCI 어댑터 초기화 및 장치 등록
// =============================================================================

int ahci_adapter_init(void) {
    kprintf("[AHCI-Adapter] Initializing AHCI Block Device Adapter\n");
    return 0;
}

block_device_t* ahci_adapter_create_device(ahci_device_t* ahci_dev, uint32_t port) {
    if (!ahci_dev) return NULL;

    // Block Device 구조체 할당
    block_device_t* block_dev = (block_device_t*)kmalloc(sizeof(block_device_t));
    if (!block_dev) {
        kprintf("[AHCI-Adapter] Failed to allocate block device\n");
        return NULL;
    }

    // AHCI 개인 데이터 할당
    ahci_block_device_data_t* data = (ahci_block_device_data_t*)kmalloc(sizeof(ahci_block_device_data_t));
    if (!data) {
        kprintf("[AHCI-Adapter] Failed to allocate private data\n");
        kfree(block_dev);
        return NULL;
    }

    // 개인 데이터 설정
    data->ahci_device = ahci_dev;
    data->ahci_port = port;

    // Block Device 초기화
    memset(block_dev, 0, sizeof(block_device_t));

    // 기본 정보 설정
    snprintf(block_dev->name, BLOCK_DEVICE_NAME_LEN, "sda%d", port);

    switch (ahci_dev->type) {
        case AHCI_DEV_SATA:
            block_dev->type = BLOCK_DEVICE_HDD; // 기본값, 나중에 SSD 감지 로직 추가 가능
            break;
        case AHCI_DEV_SATAPI:
            block_dev->type = BLOCK_DEVICE_CDROM;
            break;
        default:
            block_dev->type = BLOCK_DEVICE_UNKNOWN;
            break;
    }

    block_dev->state = BLOCK_DEVICE_READY;

    block_dev->total_sectors = ahci_dev->total_sectors;  // 하드코딩 제거
    block_dev->sector_size = 512;
    block_dev->block_size = 512;

    // 제조사 정보 (나중에 IDENTIFY에서 파싱)
    strcpy(block_dev->vendor, "Unknown");
    strcpy(block_dev->model, "AHCI SATA Device");
    strcpy(block_dev->serial, "N/A");

    // 함수 포인터와 개인 데이터 설정
    block_dev->ops = &ahci_block_ops;
    block_dev->private_data = data;

    return block_dev;
}

int ahci_adapter_register_devices(void) {
    uint32_t ahci_count = ahci_get_device_count();
    if (ahci_count == 0) {
        kprintf("[AHCI-Adapter] No AHCI devices found\n");
        return 0;
    }

    kprintf("[AHCI-Adapter] Registering %d AHCI devices\n", ahci_count);

    for (uint32_t i = 0; i < ahci_count; i++) {
        ahci_device_t* ahci_dev = ahci_get_device(i);
        if (!ahci_dev) continue;

        block_device_t* block_dev = ahci_adapter_create_device(ahci_dev, i);
        if (!block_dev) {
            kprintf("[AHCI-Adapter] Failed to create block device for port %d\n", i);
            continue;
        }

        int result = block_device_register(block_dev);
        if (result != BLOCK_SUCCESS) {
            kprintf("[AHCI-Adapter] Failed to register block device: %s\n",
                   block_result_to_string(result));
            ahci_block_destroy(block_dev);
            kfree(block_dev);
            continue;
        }

        kprintf("[AHCI-Adapter] Registered %s successfully\n", block_dev->name);
    }

    return 0;
}