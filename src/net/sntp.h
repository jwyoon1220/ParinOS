/*
 * src/net/sntp.h — SNTP (Simple Network Time Protocol, RFC 4330) 클라이언트
 *
 * 사용법:
 *   sntp_result_t t;
 *   if (sntp_query(&t) == 0) {
 *       kprintf("%04d-%02d-%02d %02d:%02d:%02d UTC\n",
 *               t.year, t.month, t.day, t.hour, t.min, t.sec);
 *   }
 */

#ifndef PARINOS_SNTP_H
#define PARINOS_SNTP_H

#include <stdint.h>

/* SNTP 쿼리 결과 — UTC 날짜/시각 */
typedef struct {
    uint16_t year;   /* 예: 2025 */
    uint8_t  month;  /* 1–12 */
    uint8_t  day;    /* 1–31 */
    uint8_t  hour;   /* 0–23 */
    uint8_t  min;    /* 0–59 */
    uint8_t  sec;    /* 0–59 */
} sntp_result_t;

/**
 * NTP 서버에 SNTP 쿼리를 전송하고 현재 UTC 시각을 가져옵니다.
 *
 * @param out  결과 저장 구조체 (성공 시 채워집니다)
 * @return 0 = 성공, -1 = 실패 (네트워크 오류 또는 타임아웃)
 *
 * QEMU user-mode 환경에서는 게이트웨이(10.0.2.2:123)로 쿼리하면
 * QEMU 내장 SNTP 응답을 받을 수 있습니다.
 */
int sntp_query(sntp_result_t *out);

#endif /* PARINOS_SNTP_H */
