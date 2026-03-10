//
// Created by jwyoo on 26. 3. 10..
//

#include "ahci.h"
#include "../vga.h"
#include "../drivers/timer.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"

#define AHCI_MAX_DEVICES 8
#define SATA_SIG_ATA    0x00000101  // SATA 드라이브
#define SATA_SIG_ATAPI  0xEB140101  // SATAPI 드라이브

static ahci_device_t ahci_devices[AHCI_MAX_DEVICES];
static uint32_t ahci_device_count = 0;
static hba_mem_t* ahci_hba_mem = NULL;
static pci_device_t* ahci_pci_device = NULL;

// AHCI 장치 타입 확인
ahci_device_type_t ahci_check_type(hba_port_t* port) {
    uint32_t ssts = port->ssts;

    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != 3) // 장치가 연결되지 않음
        return AHCI_DEV_NULL;
    if (ipm != 1) // 활성 상태가 아님
        return AHCI_DEV_NULL;

    switch (port->sig) {
        case AHCI_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case AHCI_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case AHCI_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_SATA;
    }
}

// 포트 명령어 처리 시작
void ahci_start_cmd(hba_port_t* port) {
    // FIS 수신이 실행 중이 아닐 때까지 대기
    while (port->cmd & AHCI_PORT_CMD_CR);

    // FIS 수신 활성화
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

// 포트 명령어 처리 중지
void ahci_stop_cmd(hba_port_t* port) {
    // 명령어 목록 실행 중지
    port->cmd &= ~AHCI_PORT_CMD_ST;

    // FIS 수신 중지
    port->cmd &= ~AHCI_PORT_CMD_FRE;

    // 명령어 목록과 FIS 수신이 모두 중지될 때까지 대기
    while (1) {
        if (port->cmd & AHCI_PORT_CMD_FR)
            continue;
        if (port->cmd & AHCI_PORT_CMD_CR)
            continue;
        break;
    }
}

// 사용 가능한 명령어 슬롯 찾기
int ahci_find_cmdslot(hba_port_t* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    kprintf("Cannot find free command list entry\n");
    return -1;
}

// AHCI 포트 초기화
static int ahci_init_port(hba_port_t* port, uint8_t port_num) {
    ahci_device_type_t type = ahci_check_type(port);
    if (type == AHCI_DEV_NULL)
        return 0;

    if (ahci_device_count >= AHCI_MAX_DEVICES) {
        kprintf("[AHCI] Maximum device count reached\n");
        return 0;
    }

    ahci_device_t* device = &ahci_devices[ahci_device_count];
    device->hba_mem = ahci_hba_mem;
    device->port = port;
    device->port_num = port_num;
    device->type = type;
    device->total_sectors = 0; // 초기화

    // 1. 포트 명령어 처리 중지 (설정 변경을 위해)
    ahci_stop_cmd(port);

    // 2. 명령어 목록(Command List) 메모리 할당 (1KB 정렬 필요)
    uint32_t cmd_list_phys = (uint32_t)pmm_alloc_frame();
    if (cmd_list_phys == 0) {
        kprintf("[AHCI] Failed to allocate command list\n");
        return 0;
    }
    device->cmd_list = (uint8_t*)cmd_list_phys;
    memset(device->cmd_list, 0, 4096);

    // 3. FIS 수신 베이스 메모리 할당 (256바이트 정렬 필요)
    uint32_t fis_base_phys = (uint32_t)pmm_alloc_frame();
    if (fis_base_phys == 0) {
        kprintf("[AHCI] Failed to allocate FIS base\n");
        pmm_free_frame((void*)cmd_list_phys);
        return 0;
    }
    device->fis_base = (uint8_t*)fis_base_phys;
    memset(device->fis_base, 0, 4096);

    // 4. HBA 포트 레지스터에 주소 설정
    port->clb = cmd_list_phys;
    port->clbu = 0;
    port->fb = fis_base_phys;
    port->fbu = 0;

    // 5. 명령어 테이블(Command Table) 할당 (각 슬롯당 하나씩)
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)device->cmd_list;
    for (int i = 0; i < 32; i++) {
        cmd_header[i].prdtl = 8; // PRDT 엔트리 개수
        uint32_t cmd_tbl_phys = (uint32_t)pmm_alloc_frame();
        if (cmd_tbl_phys == 0) {
            kprintf("[AHCI] Failed to allocate command table %d\n", i);
            // 이전에 할당된 메모리들 해제 필요
            return 0;
        }
        device->cmd_tables[i] = (uint8_t*)cmd_tbl_phys;
        memset(device->cmd_tables[i], 0, 4096);
        cmd_header[i].ctba = cmd_tbl_phys;
        cmd_header[i].ctbau = 0;
    }

    // 6. 포트 명령어 처리 다시 시작
    ahci_start_cmd(port);

    // 7. [핵심] IDENTIFY 명령으로 장치 상세 정보 및 용량 가져오기
    uint16_t* identify_buf = (uint16_t*)pmm_alloc_frame();
    if (identify_buf) {
        memset(identify_buf, 0, 4096);
        if (ahci_identify(device, identify_buf)) {
            // LBA28 섹터 수 (Word 60-61)
            uint32_t sectors_lba28 = *((uint32_t*)&identify_buf[60]);

            // LBA48 지원 여부 확인 (Word 83의 비트 10)
            if (identify_buf[83] & (1 << 10)) {
                // LBA48 섹터 수 (Word 100-103, 64비트)
                device->total_sectors = *((uint64_t*)&identify_buf[100]);
            } else {
                device->total_sectors = (uint64_t)sectors_lba28;
            }

            kprintf("[AHCI] Port %d: Detected %d sectors (%d MB)\n",
                   port_num,
                   (uint32_t)device->total_sectors,
                   (uint32_t)(device->total_sectors * 512 / 1024 / 1024));
        }
        pmm_free_frame(identify_buf);
    }

    ahci_device_count++;

    // 8. 로그 출력
    const char* type_name;
    switch (device->type) {
        case AHCI_DEV_SATA:   type_name = "SATA"; break;
        case AHCI_DEV_SATAPI: type_name = "SATAPI"; break;
        case AHCI_DEV_SEMB:   type_name = "SEMB"; break;
        case AHCI_DEV_PM:     type_name = "PM"; break;
        default:              type_name = "Unknown"; break;
    }

    kprintf("[AHCI] Port %d: %s device detected\n", port_num, type_name);
    return 1;
}

// AHCI 섹터 읽기
int ahci_read_sectors(ahci_device_t* device, uint64_t lba, uint16_t count, void* buffer) {
    if (!device || !buffer || count == 0) return 0;

    hba_port_t* port = device->port;
    port->is = (uint32_t)-1; // 인터럽트 상태 클리어

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return 0;

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)device->cmd_list;
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 0; // Read from device
    cmd_header->prdtl = 1;

    hba_cmd_tbl_t* cmd_tbl = (hba_cmd_tbl_t*)device->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + (cmd_header->prdtl - 1) * sizeof(hba_prdt_entry_t));

    // PRD 설정
    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = count * 512 - 1; // 512 bytes per sector
    cmd_tbl->prdt_entry[0].i = 1;

    // 명령어 FIS 설정
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_tbl->cfis;
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1; // Command
    cmd_fis->command = ATA_CMD_READ_DMA_EX;

    cmd_fis->lba0 = (uint8_t)lba;
    cmd_fis->lba1 = (uint8_t)(lba >> 8);
    cmd_fis->lba2 = (uint8_t)(lba >> 16);
    cmd_fis->device = 1 << 6; // LBA mode

    cmd_fis->lba3 = (uint8_t)(lba >> 24);
    cmd_fis->lba4 = (uint8_t)(lba >> 32);
    cmd_fis->lba5 = (uint8_t)(lba >> 40);

    cmd_fis->countl = (uint8_t)count;
    cmd_fis->counth = (uint8_t)(count >> 8);

    // 명령어 전송
    port->ci = 1 << slot;

    // 완료 대기
    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) {
            kprintf("AHCI read error\n");
            return 0;
        }
    }

    if (port->is & (1 << 30)) {
        kprintf("AHCI read error\n");
        return 0;
    }

    return 1;
}

// AHCI 섹터 쓰기
int ahci_write_sectors(ahci_device_t* device, uint64_t lba, uint16_t count, void* buffer) {
    if (!device || !buffer || count == 0) return 0;

    hba_port_t* port = device->port;
    port->is = (uint32_t)-1; // 인터럽트 상태 클리어

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return 0;

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)device->cmd_list;
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 1; // Write to device
    cmd_header->prdtl = 1;

    hba_cmd_tbl_t* cmd_tbl = (hba_cmd_tbl_t*)device->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + (cmd_header->prdtl - 1) * sizeof(hba_prdt_entry_t));

    // PRD 설정
    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = count * 512 - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    // 명령어 FIS 설정
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_tbl->cfis;
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1; // Command
    cmd_fis->command = ATA_CMD_WRITE_DMA_EX;

    cmd_fis->lba0 = (uint8_t)lba;
    cmd_fis->lba1 = (uint8_t)(lba >> 8);
    cmd_fis->lba2 = (uint8_t)(lba >> 16);
    cmd_fis->device = 1 << 6; // LBA mode

    cmd_fis->lba3 = (uint8_t)(lba >> 24);
    cmd_fis->lba4 = (uint8_t)(lba >> 32);
    cmd_fis->lba5 = (uint8_t)(lba >> 40);

    cmd_fis->countl = (uint8_t)count;
    cmd_fis->counth = (uint8_t)(count >> 8);

    // 명령어 전송
    port->ci = 1 << slot;

    // 완료 대기
    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) {
            kprintf("AHCI write error\n");
            return 0;
        }
    }

    if (port->is & (1 << 30)) {
        kprintf("AHCI write error\n");
        return 0;
    }

    return 1;
}

// AHCI IDENTIFY 명령어
int ahci_identify(ahci_device_t* device, void* buffer) {
    if (!device || !buffer) return 0;

    hba_port_t* port = device->port;
    port->is = (uint32_t)-1;

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return 0;

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)device->cmd_list;
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 0;
    cmd_header->prdtl = 1;

    hba_cmd_tbl_t* cmd_tbl = (hba_cmd_tbl_t*)device->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = 511; // 512 bytes
    cmd_tbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_tbl->cfis;
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1;
    cmd_fis->command = ATA_CMD_IDENTIFY;

    port->ci = 1 << slot;
    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) {
            kprintf("AHCI identify error\n");
            return 0;
        }
    }

    if (port->is & (1 << 30)) {
        kprintf("AHCI identify error\n");
        return 0;
    }

    return 1;
}

// AHCI 장치 목록 출력
void ahci_list_devices(void) {
    kprintf("\n=== AHCI Device List ===\n");
    kprintf("Port Type    Sectors   Size(MB)  Signature\n");
    kprintf("---- ------- --------- --------- ----------\n");

    for (uint32_t i = 0; i < ahci_device_count; i++) {
        ahci_device_t* device = &ahci_devices[i];

        const char* type_name;
        switch (device->type) {
            case AHCI_DEV_SATA:   type_name = "SATA   "; break;
            case AHCI_DEV_SATAPI: type_name = "SATAPI "; break;
            case AHCI_DEV_SEMB:   type_name = "SEMB   "; break;
            case AHCI_DEV_PM:     type_name = "PM     "; break;
            default:              type_name = "Unknown"; break;
        }

        uint32_t size_mb = (uint32_t)(device->total_sectors * 512 / 1024 / 1024);

        kprintf("%d    %s %8d  %8d  %x\n",
               device->port_num,
               type_name,
               (uint32_t)device->total_sectors,
               size_mb,
               device->port->sig);
    }

    kprintf("\nTotal: %d devices\n", ahci_device_count);
}

// AHCI 초기화
void init_ahci(void) {
    kprintf("[AHCI] Initializing AHCI subsystem...\n");

    // PCI에서 AHCI 컨트롤러 찾기
    ahci_pci_device = pci_find_device(0x8086, 0x2922); // Intel ICH9 AHCI
    if (!ahci_pci_device) {
        // 다른 일반적인 AHCI 컨트롤러들 시도
        for (uint32_t i = 0; i < pci_device_count; i++) {
            pci_device_t* dev = &pci_devices[i];
            if (dev->class_code == PCI_CLASS_MASS_STORAGE && dev->subclass == 0x06) {
                ahci_pci_device = dev;
                break;
            }
        }
    }

    if (!ahci_pci_device) {
        kprintf("[AHCI] No AHCI controller found\n");
        return;
    }

    kprintf("[AHCI] Found AHCI controller: %x:%x\n",
            ahci_pci_device->vendor_id, ahci_pci_device->device_id);

    // PCI 장치 활성화
    pci_enable_device(ahci_pci_device);

    // BAR5 (AHCI 베이스 주소) 가져오기
    uint32_t ahci_base = ahci_pci_device->bar[5] & ~0xF;
    if (ahci_base == 0) {
        kprintf("[AHCI] Invalid BAR5 address\n");
        return;
    }

    // AHCI 레지스터 영역(4KB)을 1:1 매핑
    vmm_map_page(ahci_base, ahci_base, 0x03);

    kprintf("[AHCI] AHCI base address: %x\n", ahci_base);

    // HBA 메모리 매핑
    ahci_hba_mem = (hba_mem_t*)ahci_base;

    // AHCI 활성화
    ahci_hba_mem->ghc |= AHCI_GHC_AE;

    // 구현된 포트 확인
    uint32_t pi = ahci_hba_mem->pi;
    kprintf("[AHCI] Implemented ports: %x\n", pi);

    // 각 포트 검사 및 초기화
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            hba_port_t* port = &ahci_hba_mem->ports[i];
            ahci_init_port(port, i);
        }
    }

    kprintf("[AHCI] Initialization complete! Found %d devices.\n", ahci_device_count);
}

// AHCI 장치 개수 반환
uint32_t ahci_get_device_count(void) {
    return ahci_device_count;
}

// AHCI 장치 가져오기 (인덱스로)
ahci_device_t* ahci_get_device(uint32_t index) {
    if (index >= ahci_device_count) return NULL;
    return &ahci_devices[index];
}