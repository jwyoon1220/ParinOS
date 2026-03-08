//
// Created by jwyoo on 26. 3. 9..
//

#include <stdint.h>
#ifndef PARINOS_RTC_H
#define PARINOS_RTC_H

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

void read_rtc(rtc_time_t *time);
const char* get_month_name(uint8_t month);

#endif