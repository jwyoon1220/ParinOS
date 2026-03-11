//
// Created by jwyoo on 26. 3. 9..
//

#include "pci.h"
#include "../vga.h"
#include "../io.h"
#include "../std/malloc.h"
#include <stddef.h>

#define MAX_PCI_DEVICES 256

pci_device_t pci_devices[MAX_PCI_DEVICES];
uint32_t pci_device_count = 0;

// PCI Configuration Space 주소 생성
static uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
}

// PCI Configuration Space에서 바이트 읽기
uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

// PCI Configuration Space에서 워드 읽기
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

// PCI Configuration Space에서 더블워드 읽기
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// PCI Configuration Space에 바이트 쓰기
void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}

// PCI Configuration Space에 워드 쓰기
void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

// PCI Configuration Space에 더블워드 쓰기
void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = pci_config_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// PCI 클래스 이름 반환
const char* pci_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:        return "Unclassified";
        case PCI_CLASS_MASS_STORAGE:        return "Mass Storage";
        case PCI_CLASS_NETWORK:             return "Network";
        case PCI_CLASS_DISPLAY:             return "Display";
        case PCI_CLASS_MULTIMEDIA:          return "Multimedia";
        case PCI_CLASS_MEMORY:              return "Memory";
        case PCI_CLASS_BRIDGE:              return "Bridge";
        case PCI_CLASS_COMMUNICATION:       return "Communication";
        case PCI_CLASS_SYSTEM:              return "System";
        case PCI_CLASS_INPUT:               return "Input";
        case PCI_CLASS_DOCKING:             return "Docking";
        case PCI_CLASS_PROCESSOR:           return "Processor";
        case PCI_CLASS_SERIAL_BUS:          return "Serial Bus";
        case PCI_CLASS_WIRELESS:            return "Wireless";
        case PCI_CLASS_INTELLIGENT_IO:      return "Intelligent I/O";
        case PCI_CLASS_SATELLITE:           return "Satellite";
        case PCI_CLASS_ENCRYPTION:          return "Encryption";
        case PCI_CLASS_SIGNAL_PROCESSING:   return "Signal Processing";
        default:                            return "Unknown";
    }
}

// 주요 벤더 ID 이름 반환
const char* pci_get_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "ATI/AMD";
        case 0x1106: return "VIA";
        case 0x10EC: return "Realtek";
        case 0x8888: return "VMware";
        case 0x1234: return "QEMU";
        case 0x15AD: return "VMware";
        case 0x80EE: return "VirtualBox";
        case 0x1AB8: return "Parallels";
        case 0x1414: return "Microsoft";
        default:     return "Unknown";
    }
}

// 특정 장치 검색
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

// PCI 장치 활성화
void pci_enable_device(pci_device_t* device) {
    if (device == NULL) return;

    // Command 레지스터 읽기
    uint16_t command = pci_config_read_word(device->bus, device->device, device->function, PCI_COMMAND);

    // I/O Space와 Memory Space 접근 활성화
    command |= 0x03; // Bit 0: I/O Space, Bit 1: Memory Space

    // Bus Master 활성화 (DMA를 위해)
    command |= 0x04; // Bit 2: Bus Master

    pci_config_write_word(device->bus, device->device, device->function, PCI_COMMAND, command);

    kprintf("[PCI] Device 0x%x:0x%x enabled\n", device->vendor_id, device->device_id);
}

// PCI 장치 정보 읽기
static void pci_read_device_info(uint8_t bus, uint8_t device, uint8_t function) {
    if (pci_device_count >= MAX_PCI_DEVICES) return;

    pci_device_t* dev = &pci_devices[pci_device_count];

    dev->vendor_id = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
    dev->device_id = pci_config_read_word(bus, device, function, PCI_DEVICE_ID);
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->class_code = pci_config_read_byte(bus, device, function, PCI_CLASS_CODE);
    dev->subclass = pci_config_read_byte(bus, device, function, PCI_SUBCLASS);
    dev->prog_if = pci_config_read_byte(bus, device, function, PCI_PROG_IF);
    dev->revision_id = pci_config_read_byte(bus, device, function, PCI_REVISION_ID);
    dev->header_type = pci_config_read_byte(bus, device, function, PCI_HEADER_TYPE);
    dev->interrupt_line = pci_config_read_byte(bus, device, function, PCI_INTERRUPT_LINE);
    dev->interrupt_pin = pci_config_read_byte(bus, device, function, PCI_INTERRUPT_PIN);

    // BAR 레지스터 읽기
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_config_read_dword(bus, device, function, PCI_BAR0 + (i * 4));
    }

    pci_device_count++;
}

// PCI 버스 스캔
void pci_scan_bus(void) {
    pci_device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor_id = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);

                if (vendor_id == 0xFFFF) {
                    // 장치가 존재하지 않음
                    if (function == 0) break; // 다음 device로
                    continue; // 다음 function으로
                }

                pci_read_device_info(bus, device, function);

                // Multi-function 장치가 아니라면 function 0만 확인
                if (function == 0) {
                    uint8_t header_type = pci_config_read_byte(bus, device, function, PCI_HEADER_TYPE);
                    if ((header_type & 0x80) == 0) {
                        break; // Single function device
                    }
                }
            }
        }
    }

    kprintf("[PCI] Scan completed. Found %d devices.\n", pci_device_count);
}

// 숫자를 2자리 16진수로 출력하는 헬퍼 함수
static void print_hex_2digit(uint8_t value) {
    uint8_t high = (value >> 4) & 0xF;
    uint8_t low = value & 0xF;

    char high_char = (high < 10) ? ('0' + high) : ('A' + high - 10);
    char low_char = (low < 10) ? ('0' + low) : ('A' + low - 10);

    lkputchar(high_char);
    lkputchar(low_char);
}

// 숫자를 4자리 16진수로 출력하는 헬퍼 함수
static void print_hex_4digit(uint16_t value) {
    print_hex_2digit((value >> 8) & 0xFF);
    print_hex_2digit(value & 0xFF);
}

// PCI 장치 목록 출력
void pci_list_devices(void) {
    kprintf("\n=== PCI Device List ===\n");
    kprintf("Bus Dev Fn Vendor Device Class         SubClass Vendor Name\n");
    kprintf("--- --- -- ------ ------ ------------ -------- -----------\n");

    for (uint32_t i = 0; i < pci_device_count; i++) {
        pci_device_t* dev = &pci_devices[i];

        // Bus (2자리)
        print_hex_2digit(dev->bus);
        kprint("  ");

        // Device (2자리)
        print_hex_2digit(dev->device);
        kprint("  ");

        // Function (1자리)
        kprintf("%x  ", dev->function);

        // Vendor ID (4자리)
        print_hex_4digit(dev->vendor_id);
        kprint("   ");

        // Device ID (4자리)
        print_hex_4digit(dev->device_id);
        kprint("   ");

        // Class name (12자리 고정폭)
        const char* class_name = pci_get_class_name(dev->class_code);
        kprint(class_name);

        // 클래스 이름 뒤에 공백 추가 (12자리 맞추기)
        int len = 0;
        for (int j = 0; class_name[j] != '\0'; j++) len++;
        for (int j = len; j < 12; j++) lkputchar(' ');

        // SubClass (2자리)
        print_hex_2digit(dev->subclass);
        kprint("       ");

        // Vendor name
        kprint(pci_get_vendor_name(dev->vendor_id));
        kprintf("\n");

        // BAR 정보 출력 (값이 0이 아닌 경우만)
        for (int j = 0; j < 6; j++) {
            if (dev->bar[j] != 0) {
                if (dev->bar[j] & 1) {
                    // I/O BAR
                    kprintf("    BAR%d: I/O Port at %x\n", j, dev->bar[j] & ~3);
                } else {
                    // Memory BAR
                    kprintf("    BAR%d: Memory at %x\n", j, dev->bar[j] & ~15);
                }
            }
        }

        if (dev->interrupt_line != 0xFF) {
            kprintf("    IRQ: %d\n", dev->interrupt_line);
        }

        kprintf("\n");
    }
}

// PCI 초기화
void init_pci(void) {
    kprintf("[PCI] Initializing PCI subsystem...\n");

    // PCI 지원 여부 확인 (간단한 방법)
    outl(PCI_CONFIG_ADDRESS, 0x80000000);
    uint32_t test = inl(PCI_CONFIG_ADDRESS);
    if (test != 0x80000000) {
        kprintf("[PCI] ERROR: PCI not supported!\n");
        return;
    }

    // PCI 버스 스캔
    pci_scan_bus();

    kprintf("[PCI] Initialization complete!\n");
}