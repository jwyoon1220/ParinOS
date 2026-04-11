/*
 * src/net/sntp.c — SNTP (Simple Network Time Protocol) 클라이언트
 *
 * RFC 4330 준수 최소 구현:
 *   - NTP v3 클라이언트 모드 패킷(48 바이트) 전송
 *   - 서버 응답의 Transmit Timestamp(초 단위)로 UTC 시각 계산
 *   - NTP epoch(1900-01-01) → Unix epoch(1970-01-01) → 그레고리력 변환
 *
 * NTP 서버 우선순위:
 *   1순위: KAIST 시간 서버  time.kaist.ac.kr  143.248.2.2  UDP/123
 *   2순위: QEMU 내장 SNTP   10.0.2.2          UDP/123
 *          (호스트 오프라인 또는 KAIST 응답 없을 때 폴백)
 *
 * QEMU user-mode 네트워크에서 143.248.2.2 는 10.0.2.2 게이트웨이를 통해
 * 호스트의 인터넷으로 NAT 라우팅됩니다.
 */

#include "sntp.h"
#include "lwip_port.h"

/* NTP/SNTP 포트 */
#define NTP_PORT        123

/* NTP epoch (1900-01-01 00:00:00) → Unix epoch (1970-01-01) 차이(초) */
#define NTP_UNIX_DELTA  2208988800UL

/* NTP 패킷 크기 */
#define NTP_PKT_LEN     48

/* ── NTP 서버 주소 ────────────────────────────────────────────────── */
/* 1순위: KAIST 시간 서버 (time.kaist.ac.kr) */
#define NTP_SERVER_KAIST  ((143U<<24)|(248U<<16)|(2U<<8)|2U)  /* 143.248.2.2 */

/* 2순위 폴백: QEMU user-mode 내장 SNTP */
#define NTP_SERVER_QEMU   ((10U<<24)|(0U<<16)|(2U<<8)|2U)     /* 10.0.2.2    */

/* 로컬 에페메럴 소스 포트 */
#define NTP_LOCAL_PORT    12300

/* ── 윤년 판별 ────────────────────────────────────────────────────── */
static int is_leap(uint32_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static const uint8_t days_in_month[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

/* ── Unix timestamp → UTC 날짜+시각 변환 ─────────────────────────── */
static void unix_to_utc(uint32_t ts, sntp_result_t *out) {
    out->sec  = (uint8_t)(ts % 60); ts /= 60;
    out->min  = (uint8_t)(ts % 60); ts /= 60;
    out->hour = (uint8_t)(ts % 24); ts /= 24;

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
        if (days < dim) {
            out->month = m + 1;
            out->day   = (uint8_t)(days + 1);
            return;
        }
        days -= dim;
    }
    out->month = 12;
    out->day   = 31;
}

/* ── 특정 서버에 NTP 쿼리 전송 ── 0=성공, -1=실패 ─────────────────── */
static int query_server(uint32_t server_ip, sntp_result_t *out) {
    uint8_t req[NTP_PKT_LEN];
    int i;
    for (i = 0; i < NTP_PKT_LEN; i++) req[i] = 0;
    /*
     * LI=0, VN=3 (NTPv3), Mode=3 (client)
     * Byte 0: 00 011 011 = 0x1B
     */
    req[0] = 0x1B;

    uint8_t resp[NTP_PKT_LEN];
    int n = lwip_udp_query(
        server_ip,
        NTP_PORT,
        NTP_LOCAL_PORT,
        req,  (uint16_t)NTP_PKT_LEN,
        resp, (uint16_t)NTP_PKT_LEN,
        4000  /* 폴링 횟수 (수 초 타임아웃) */
    );

    if (n < NTP_PKT_LEN) return -1;

    /* Transmit Timestamp: 바이트 40-43 (big-endian 초) */
    uint32_t ntp_sec = ((uint32_t)resp[40] << 24)
                     | ((uint32_t)resp[41] << 16)
                     | ((uint32_t)resp[42] <<  8)
                     |  (uint32_t)resp[43];

    if (ntp_sec < (uint32_t)NTP_UNIX_DELTA) return -1;

    unix_to_utc(ntp_sec - (uint32_t)NTP_UNIX_DELTA, out);
    return 0;
}

/* ── sntp_query: KAIST 우선, 실패 시 QEMU 폴백 ──────────────────── */
int sntp_query(sntp_result_t *out) {
    /* 1순위: KAIST time.kaist.ac.kr (143.248.2.2) */
    if (query_server(NTP_SERVER_KAIST, out) == 0)
        return 0;

    /* 2순위 폴백: QEMU 내장 SNTP (10.0.2.2) */
    if (query_server(NTP_SERVER_QEMU, out) == 0)
        return 0;

    return -1;
}
