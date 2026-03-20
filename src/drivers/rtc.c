//
// Created by jwyoo on 26. 3. 9..
//

#include "rtc.h"
#include "../hal/io.h" // outb, inb가 정의된 곳

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

// 달 이름 배열
static const char* month_names[] = {
    "nul", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// CMOS가 현재 업데이트 중인지 확인 (데이터 오염 방지)
int is_update_in_progress() {
    outb(CMOS_ADDR, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

// BCD -> 10진수 변환 함수
// 예: 0x26 (BCD) -> 26 (Decimal)
uint32_t bcd_to_bin(uint32_t bcd) {
    return ((bcd / 16) * 10) + (bcd % 16);
}

void read_rtc(rtc_time_t *time) {
    while (is_update_in_progress()); // 업데이트 끝날 때까지 대기

    time->second = bcd_to_bin(get_rtc_register(0x00));
    time->minute = bcd_to_bin(get_rtc_register(0x02));
    time->hour   = bcd_to_bin(get_rtc_register(0x04));
    time->day    = bcd_to_bin(get_rtc_register(0x07));
    time->month  = bcd_to_bin(get_rtc_register(0x08));
    time->year   = bcd_to_bin(get_rtc_register(0x09));

    // 보통 CMOS는 연도를 뒤의 2자리만 저장함 (예: 26)
    // 2000년을 더해줌 (Y2K 고려)
    time->year += 2000;
}

const char* get_month_name(uint8_t month) {
    if (month < 1 || month > 12) return month_names[0];
    return month_names[month];
}