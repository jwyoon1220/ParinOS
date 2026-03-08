//
// Created by jwyoo on 26. 3. 7..
//

#ifndef PARINOS_SERIAL_H
#define PARINOS_SERIAL_H

#define COM1 0x3F8

int init_serial();
void write_serial(char a);
void kprintf_serial(const char* format, ...);


#endif //PARINOS_SERIAL_H