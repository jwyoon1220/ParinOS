//
// Created by jwyoo on 26. 3. 11..
//

#ifndef PARINOS_MULTITASKING_H
#define PARINOS_MULTITASKING_H
#include <stdint.h>

void* create_thread(void (*entry_point)(void), uint32_t stack_size);
void thread_exit();

void schedule();
void runAsync(void (*func)(void));

#endif //PARINOS_MULTITASKING_H