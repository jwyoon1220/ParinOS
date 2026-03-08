//
// Created by jwyoo on 26. 3. 9..
//

#ifndef PARINOS_UTIL_H
#define PARINOS_UTIL_H

#include "../io.h"

inline void eoi() { // end of interrupt
    outb(0x20, 0x20);
}

#endif //PARINOS_UTIL_H