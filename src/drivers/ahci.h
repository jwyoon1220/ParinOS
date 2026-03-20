//
// Created by jwyoo on 26. 3. 10..
//

#ifndef PARINOS_AHCI_H
#define PARINOS_AHCI_H

#include "pci.h"
#include "../hal/io.h"
#include "../mem/mem.h"
#include "../std/malloc.h"
#include <stdint.h>
#include <stddef.h>

// AHCI 레지스터 오프셋 (HBA 메모리 매핑)
#define AHCI_CAP        0x00    // Host Capabilities
#define AHCI_GHC        0x04    // Global Host Control
#define AHCI_IS         0x08    // Interrupt Status
#define AHCI_PI         0x0C    // Ports Implemented
#define AHCI_VS         0x10    // Version
#define AHCI_CCC_CTL    0x14    // Command Completion Coalescing Control
#define AHCI_CCC_PORTS  0x18    // Command Completion Coalescing Ports
#define AHCI_EM_LOC     0x1C    // Enclosure Management Location
#define AHCI_EM_CTL     0x20    // Enclosure Management Control
#define AHCI_CAP2       0x24    // Host Capabilities Extended

// 포트별 레지스터 오프셋 (포트 베이스 + 오프셋)
#define AHCI_PORT_CLB   0x00    // Command List Base Address
#define AHCI_PORT_CLBU  0x04    // Command List Base Address Upper 32-bits
#define AHCI_PORT_FB    0x08    // FIS Base Address
#define AHCI_PORT_FBU   0x0C    // FIS Base Address Upper 32-bits
#define AHCI_PORT_IS    0x10    // Interrupt Status
#define AHCI_PORT_IE    0x14    // Interrupt Enable
#define AHCI_PORT_CMD   0x18    // Command and Status
#define AHCI_PORT_TFD   0x20    // Task File Data
#define AHCI_PORT_SIG   0x24    // Signature
#define AHCI_PORT_SSTS  0x28    // SATA Status (SCR0:SStatus)
#define AHCI_PORT_SCTL  0x2C    // SATA Control (SCR2:SControl)
#define AHCI_PORT_SERR  0x30    // SATA Error (SCR1:SError)
#define AHCI_PORT_SACT  0x34    // SATA Active (SCR3:SActive)
#define AHCI_PORT_CI    0x38    // Command Issue

// AHCI Global Host Control 비트
#define AHCI_GHC_HR     (1 << 0)    // HBA Reset
#define AHCI_GHC_IE     (1 << 1)    // Interrupt Enable
#define AHCI_GHC_MRSM   (1 << 2)    // MSI Revert to Single Message
#define AHCI_GHC_AE     (1 << 31)   // AHCI Enable

// AHCI 포트 명령어/상태 비트
#define AHCI_PORT_CMD_ST    (1 << 0)    // Start
#define AHCI_PORT_CMD_SUD   (1 << 1)    // Spin-Up Device
#define AHCI_PORT_CMD_POD   (1 << 2)    // Power On Device
#define AHCI_PORT_CMD_CLO   (1 << 3)    // Command List Override
#define AHCI_PORT_CMD_FRE   (1 << 4)    // FIS Receive Enable
#define AHCI_PORT_CMD_CCS   (0x1F << 8) // Current Command Slot
#define AHCI_PORT_CMD_MPSS  (1 << 13)   // Mechanical Presence Switch State
#define AHCI_PORT_CMD_FR    (1 << 14)   // FIS Receive Running
#define AHCI_PORT_CMD_CR    (1 << 15)   // Command List Running
#define AHCI_PORT_CMD_CPS   (1 << 16)   // Cold Presence State
#define AHCI_PORT_CMD_PMA   (1 << 17)   // Port Multiplier Attached
#define AHCI_PORT_CMD_HPCP  (1 << 18)   // Hot Plug Capable Port
#define AHCI_PORT_CMD_MPSP  (1 << 19)   // Mechanical Presence Switch Attached
#define AHCI_PORT_CMD_CPD   (1 << 20)   // Cold Presence Detection
#define AHCI_PORT_CMD_ESP   (1 << 21)   // External SATA Port
#define AHCI_PORT_CMD_FBSCP (1 << 22)   // FIS-based Switching Capable Port
#define AHCI_PORT_CMD_APSTE (1 << 23)   // Automatic Partial to Slumber Transition Enabled
#define AHCI_PORT_CMD_ATAPI (1 << 24)   // Device is ATAPI
#define AHCI_PORT_CMD_DLAE  (1 << 25)   // Drive LED on ATAPI Enable
#define AHCI_PORT_CMD_ALPE  (1 << 26)   // Aggressive Link Power Management Enable
#define AHCI_PORT_CMD_ASP   (1 << 27)   // Aggressive Slumber/Partial
#define AHCI_PORT_CMD_ICC   (0xF << 28) // Interface Communication Control

// SATA 장치 시그니처
#define AHCI_SIG_ATA    0x00000101  // SATA 드라이브
#define AHCI_SIG_ATAPI  0xEB140101  // SATAPI 드라이브
#define AHCI_SIG_SEMB   0xC33C0101  // Enclosure management bridge
#define AHCI_SIG_PM     0x96690101  // 포트 멀티플라이어

// FIS 타입
#define FIS_TYPE_REG_H2D    0x27    // Register FIS - host to device
#define FIS_TYPE_REG_D2H    0x34    // Register FIS - device to host
#define FIS_TYPE_DMA_ACT    0x39    // DMA activate FIS - device to host
#define FIS_TYPE_DMA_SETUP  0x41    // DMA setup FIS - bidirectional
#define FIS_TYPE_DATA       0x46    // Data FIS - bidirectional
#define FIS_TYPE_BIST       0x58    // BIST activate FIS - bidirectional
#define FIS_TYPE_PIO_SETUP  0x5F    // PIO setup FIS - device to host
#define FIS_TYPE_DEV_BITS   0xA1    // Set device bits FIS - device to host

// ATA 명령어
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_IDENTIFY        0xEC

// AHCI 구조체들
#pragma pack(push, 1)

// FIS - Register Host to Device
typedef struct {
    uint8_t fis_type;       // FIS_TYPE_REG_H2D
    uint8_t pmport:4;       // Port multiplier
    uint8_t rsv0:3;         // Reserved
    uint8_t c:1;            // 1: Command, 0: Control
    uint8_t command;        // Command register
    uint8_t featurel;       // Feature register, 7:0

    uint8_t lba0;           // LBA low register, 7:0
    uint8_t lba1;           // LBA mid register, 15:8
    uint8_t lba2;           // LBA high register, 23:16
    uint8_t device;         // Device register

    uint8_t lba3;           // LBA register, 31:24
    uint8_t lba4;           // LBA register, 39:32
    uint8_t lba5;           // LBA register, 47:40
    uint8_t featureh;       // Feature register, 15:8

    uint8_t countl;         // Count register, 7:0
    uint8_t counth;         // Count register, 15:8
    uint8_t icc;            // Isochronous command completion
    uint8_t control;        // Control register

    uint8_t rsv1[4];        // Reserved
} fis_reg_h2d_t;

// FIS - Register Device to Host
typedef struct {
    uint8_t fis_type;       // FIS_TYPE_REG_D2H
    uint8_t pmport:4;       // Port multiplier
    uint8_t rsv0:2;         // Reserved
    uint8_t i:1;            // Interrupt bit
    uint8_t rsv1:1;         // Reserved
    uint8_t status;         // Status register
    uint8_t error;          // Error register

    uint8_t lba0;           // LBA low register, 7:0
    uint8_t lba1;           // LBA mid register, 15:8
    uint8_t lba2;           // LBA high register, 23:16
    uint8_t device;         // Device register

    uint8_t lba3;           // LBA register, 31:24
    uint8_t lba4;           // LBA register, 39:32
    uint8_t lba5;           // LBA register, 47:40
    uint8_t rsv2;           // Reserved

    uint8_t countl;         // Count register, 7:0
    uint8_t counth;         // Count register, 15:8
    uint8_t rsv3[2];        // Reserved
    uint8_t rsv4[4];        // Reserved
} fis_reg_d2h_t;

// FIS - Data
typedef struct {
    uint8_t fis_type;       // FIS_TYPE_DATA
    uint8_t pmport:4;       // Port multiplier
    uint8_t rsv0:4;         // Reserved
    uint8_t rsv1[2];        // Reserved
    uint32_t data[1];       // Payload
} fis_data_t;

// FIS - PIO Setup Device to Host
typedef struct {
    uint8_t fis_type;       // FIS_TYPE_PIO_SETUP
    uint8_t pmport:4;       // Port multiplier
    uint8_t rsv0:1;         // Reserved
    uint8_t d:1;            // Data transfer direction, 1 - device to host
    uint8_t i:1;            // Interrupt bit
    uint8_t rsv1:1;         // Reserved
    uint8_t status;         // Status register
    uint8_t error;          // Error register

    uint8_t lba0;           // LBA low register, 7:0
    uint8_t lba1;           // LBA mid register, 15:8
    uint8_t lba2;           // LBA high register, 23:16
    uint8_t device;         // Device register

    uint8_t lba3;           // LBA register, 31:24
    uint8_t lba4;           // LBA register, 39:32
    uint8_t lba5;           // LBA register, 47:40
    uint8_t rsv2;           // Reserved

    uint8_t countl;         // Count register, 7:0
    uint8_t counth;         // Count register, 15:8
    uint8_t rsv3;           // Reserved
    uint8_t e_status;       // New value of status register

    uint16_t tc;            // Transfer count
    uint8_t rsv4[2];        // Reserved
} fis_pio_setup_t;

// FIS - DMA Setup Device to Host
typedef struct {
    uint8_t fis_type;       // FIS_TYPE_DMA_SETUP
    uint8_t pmport:4;       // Port multiplier
    uint8_t rsv0:1;         // Reserved
    uint8_t d:1;            // Data transfer direction, 1 - device to host
    uint8_t i:1;            // Interrupt bit
    uint8_t a:1;            // Auto-activate. Specifies if DMA Activate FIS is needed
    uint8_t rsv1[2];        // Reserved
    uint64_t DMAbufferID;   // DMA Buffer Identifier
    uint32_t rsv2;          // More reserved
    uint32_t DMAbufOffset;  // Byte offset into buffer. First 2 bits must be 0
    uint32_t TransferCount; // Number of bytes to transfer. Bit 0 must be 0
    uint32_t rsv3;          // Reserved
} fis_dma_setup_t;

// HBA 포트 구조체
typedef volatile struct {
    uint32_t clb;       // Command list base address, 1K-byte aligned
    uint32_t clbu;      // Command list base address upper 32 bits
    uint32_t fb;        // FIS base address, 256-byte aligned
    uint32_t fbu;       // FIS base address upper 32 bits
    uint32_t is;        // Interrupt status
    uint32_t ie;        // Interrupt enable
    uint32_t cmd;       // Command and status
    uint32_t rsv0;      // Reserved
    uint32_t tfd;       // Task file data
    uint32_t sig;       // Signature
    uint32_t ssts;      // SATA status (SCR0:SStatus)
    uint32_t sctl;      // SATA control (SCR2:SControl)
    uint32_t serr;      // SATA error (SCR1:SError)
    uint32_t sact;      // SATA active (SCR3:SActive)
    uint32_t ci;        // Command issue
    uint32_t sntf;      // SATA notification (SCR4:SNotification)
    uint32_t fbs;       // FIS-based switch control
    uint32_t rsv1[11];  // Reserved
    uint32_t vendor[4]; // Vendor specific
} hba_port_t;

// HBA 메모리 구조체
typedef volatile struct {
    // 0x00 - 0x2B, Generic Host Control
    uint32_t cap;       // Host capability
    uint32_t ghc;       // Global host control
    uint32_t is;        // Interrupt status
    uint32_t pi;        // Port implemented
    uint32_t vs;        // Version
    uint32_t ccc_ctl;   // Command completion coalescing control
    uint32_t ccc_pt;    // Command completion coalescing ports
    uint32_t em_loc;    // Enclosure management location
    uint32_t em_ctl;    // Enclosure management control
    uint32_t cap2;      // Host capabilities extended
    uint32_t bohc;      // BIOS/OS handoff control and status

    // 0x2C - 0x9F, Reserved
    uint8_t rsv[0xA0-0x2C];

    // 0xA0 - 0xFF, Vendor specific registers
    uint8_t vendor[0x100-0xA0];

    // 0x100 - 0x10FF, Port control registers
    hba_port_t ports[1]; // 1 ~ 32 포트
} hba_mem_t;

// Command Header
typedef struct {
    uint8_t cfl:5;          // Command FIS length in DWORDS, 2 ~ 16
    uint8_t a:1;            // ATAPI
    uint8_t w:1;            // Write, 1: H2D, 0: D2H
    uint8_t p:1;            // Prefetchable

    uint8_t r:1;            // Reset
    uint8_t b:1;            // BIST
    uint8_t c:1;            // Clear busy upon R_OK
    uint8_t rsv0:1;         // Reserved
    uint8_t pmp:4;          // Port multiplier port

    uint16_t prdtl;         // Physical region descriptor table length in entries

    volatile uint32_t prdbc;// Physical region descriptor byte count transferred

    uint32_t ctba;          // Command table descriptor base address
    uint32_t ctbau;         // Command table descriptor base address upper 32 bits

    uint32_t rsv1[4];       // Reserved
} hba_cmd_header_t;

// Physical Region Descriptor Table
typedef struct {
    uint32_t dba;           // Data base address
    uint32_t dbau;          // Data base address upper 32 bits
    uint32_t rsv0;          // Reserved

    uint32_t dbc:22;        // Byte count, 4M max
    uint32_t rsv1:9;        // Reserved
    uint32_t i:1;           // Interrupt on completion
} hba_prdt_entry_t;

// Command table
typedef struct {
    uint8_t cfis[64];       // Command FIS
    uint8_t acmd[16];       // ATAPI command, 12 or 16 bytes
    uint8_t rsv[48];        // Reserved
    hba_prdt_entry_t prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} hba_cmd_tbl_t;

#pragma pack(pop)

// AHCI 포트 타입
typedef enum {
    AHCI_DEV_NULL = 0,
    AHCI_DEV_SATA = 1,
    AHCI_DEV_SEMB = 2,
    AHCI_DEV_PM = 3,
    AHCI_DEV_SATAPI = 4,
} ahci_device_type_t;

// AHCI 장치 정보
typedef struct {
    hba_port_t* port;
    hba_mem_t* hba_mem;
    uint8_t port_num;
    ahci_device_type_t type;

    // 추가된 부분: 디스크의 총 섹터 수 저장
    uint64_t total_sectors;

    uint8_t* cmd_list;
    uint8_t* fis_base;
    uint8_t* cmd_tables[32];
} ahci_device_t;

// 함수 프로토타입
void init_ahci(void);
int ahci_read_sectors(ahci_device_t* device, uint64_t lba, uint16_t count, void* buffer);
int ahci_write_sectors(ahci_device_t* device, uint64_t lba, uint16_t count, void* buffer);
int ahci_identify(ahci_device_t* device, void* buffer);
ahci_device_type_t ahci_check_type(hba_port_t* port);
void ahci_start_cmd(hba_port_t* port);
void ahci_stop_cmd(hba_port_t* port);
int ahci_find_cmdslot(hba_port_t* port);
void ahci_list_devices(void);
uint32_t ahci_get_device_count(void);
ahci_device_t* ahci_get_device(uint32_t index);

#endif //PARINOS_AHCI_H