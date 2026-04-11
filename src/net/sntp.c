/*
 * src/net/sntp.c — SNTP (Simple Network Time Protocol) 클라이언트
 *
 * RFC 4330 준수 최소 구현:
 *   - NTP v3 클라이언트 모드 패킷(48 바이트) 전송
 *   - 서버 응답의 Transmit Timestamp(초 단위)로 UTC 시각 계산
 *   - NTP epoch(1900-01-01) → Unix epoch(1970-01-01) → 그레고리력 변환
 *
 * QEMU user-mode 네트워크:
 *   - 게이트웨이 10.0.2.2:123 → QEMU 내장 SNTP 응답 (호스트 시스템 시각)
 */

#include "sntp.h"
#include "lwip_port.h"

/* NTP/SNTP 포트 */
#define NTP_PORT    123

/* NTP epoch (1900-01-01 00:00:00) → Unix epoch (1970-01-01) 차이(초) */
#define NTP_UNIX_DELTA  2208988800UL

/* NTP 패킷 크기 */
#define NTP_PKT_LEN     48

/* QEMU user-mode 게이트웨이 — NTP 쿼리 목적지 */
#define NTP_SERVER_IP   ((10U<<24)|(0U<<16)|(2U<<8)|2U)   /* 10.0.2.2 */

/* 로컬 소스 포트 (임의 에페메럴) */
#define NTP_LOCAL_PORT  12300

/* ── 윤년 판별 ────────────────────────────────────────────────────── */
static int is_leap(uint32_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/* 각 월의 일수 */
static const uint8_t days_in_month[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

/* ── Unix timestamp → UTC 날짜+시각 변환 ─────────────────────────── */
static void unix_to_utc(uint32_t ts, sntp_result_t *out) {
    /* 시/분/초 */
    out->sec  = (uint8_t)(ts % 60); ts /= 60;
    out->min  = (uint8_t)(ts % 60); ts /= 60;
    out->hour = (uint8_t)(ts % 24); ts /= 24;

    /* 1970-01-01 부터 day 수 → 연/월/일 */
    uint32_t days = ts;
    uint32_t y = 1970;
    while (1) {
        uint32_t dy = (uint32_t)(is_leap(y) ? 366 : 365);
        if (days < dy) break;
        days -= dy;
        y++;
    }
    out->year = (uint16_t)y;

    for (uint8_t m = 0; m < 12; m++) {
        uint8_t dim = days_in_month[m];
        if (m == 1 && is_leap(y)) dim = 29;
        if (days < dim) { out->month = m + 1; out->day = (uint8_t)(days + 1); return; }
        days -= dim;
    }
    /* 도달하면 안 되지만 안전을 위해 */
    out->month = 12;
    out->day   = 31;
}

/* ── SNTP 쿼리 ───────────────────────────────────────────────────── */
int sntp_query(sntp_result_t *out) {
    /* NTP 요청 패킷 (48 바이트) */
    uint8_t req[NTP_PKT_LEN];
    for (int i = 0; i < NTP_PKT_LEN; i++) req[i] = 0;

    /*
     * LI=0 (no warning), VN=3 (NTPv3), Mode=3 (client)
     * Byte 0: [LI(2)][VN(3)][Mode(3)] = 00 011 011 = 0x1B
     */
    req[0] = 0x1B;

    uint8_t resp[NTP_PKT_LEN];
    int n = lwip_udp_query(
        NTP_SERVER_IP,
        NTP_PORT,
        NTP_LOCAL_PORT,
        req,  NTP_PKT_LEN,
        resp, NTP_PKT_LEN,
        4000   /* 폴링 횟수 — NIC가 1회/~ms 이므로 약 4초 대기 */
    );

    if (n < NTP_PKT_LEN) return -1;

    /*
     * NTP 응답에서 Transmit Timestamp (바이트 40–43 = 초 단위, big-endian)
     * 바이트 44–47 = 소수 초 (사용하지 않음)
     */
    uint32_t ntp_sec = ((uint32_t)resp[40] << 24)
                     | ((uint32_t)resp[41] << 16)
                     | ((uint32_t)resp[42] <<  8)
                     |  (uint32_t)resp[43];

    if (ntp_sec < NTP_UNIX_DELTA) return -1;   /* 유효하지 않은 타임스탬프 */

    uint32_t unix_sec = ntp_sec - (uint32_t)NTP_UNIX_DELTA;
    unix_to_utc(unix_sec, out);
    return 0;
}
