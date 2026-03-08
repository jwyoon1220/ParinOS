//
// Created by jwyoo on 26. 3. 8..
//

#ifndef PARINOS_TIMER_H
#define PARINOS_TIMER_H

#include <stdint.h>
#include "../idt.h"

void init_timer(uint32_t frequency);
void sleep(uint32_t ms);
uint32_t get_total_ticks();
extern volatile uint32_t tick;

#endif //PARINOS_TIMER_H