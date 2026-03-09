//
// Created by jwyoo on 26. 3. 10..
//

#ifndef PARINOS_AHCI_ADAPTOR_H
#define PARINOS_AHCI_ADAPTOR_H

#include "sda.h"
#include "../drivers/ahci.h"

// AHCI Block Device 개인 데이터
typedef struct {
    ahci_device_t* ahci_device;
    uint32_t ahci_port;
} ahci_block_device_data_t;

// AHCI 어댑터 함수들
int ahci_adapter_init(void);
int ahci_adapter_register_devices(void);
block_device_t* ahci_adapter_create_device(ahci_device_t* ahci_dev, uint32_t port);

#endif //PARINOS_AHCI_ADAPTOR_H