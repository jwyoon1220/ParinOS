//
// Created by jwyoo on 26. 3. 7..
//

#ifndef PARINOS_SHELL_H
#define PARINOS_SHELL_H

#include <stdint.h>

extern char* cmd_buf;
extern int cmd_idx;

void shell_init();
void shell_input(char c);

#endif //PARINOS_SHELL_H