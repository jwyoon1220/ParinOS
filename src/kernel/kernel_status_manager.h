//
// Created by jwyoo on 26. 3. 8..
//
#include <stdint.h>

#ifndef PARINOS_KERNEL_STATUS_MANAGER_H
#define PARINOS_KERNEL_STATUS_MANAGER_H

void kernel_panic(char* reason, uint32_t addr);
void kernel_error(char* reason, uint32_t addr);

#endif //PARINOS_KERNEL_STATUS_MANAGER_H