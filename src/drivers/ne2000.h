/*
 * src/drivers/ne2000.h — NE2000 / RTL8019 PCI NIC 드라이버 헤더
 *
 * QEMU는 `-net nic,model=ne2k_pci` 로 NE2000 PCI 카드를 에뮬레이션합니다.
 * 이 드라이버는 해당 칩의 기본 I/O 레지스터를 직접 사용합니다.
 */

#ifndef PARINOS_NE2000_H
#define PARINOS_NE2000_H

#include <stdint.h>
#include <stddef.h>

/* ── PCI Vendor/Device ID ─────────────────────────────────────────── */
#define NE2000_VENDOR_ID  0x10EC  /* Realtek */
#define NE2000_DEVICE_ID  0x8029  /* RTL8019AS (NE2k compatible) */

/* ── 전송 완료 콜백 ─────────────────────────────────────────────── */
typedef void (*ne2000_rx_cb_t)(const uint8_t *pkt, uint16_t len);

/* ── 공개 API ────────────────────────────────────────────────────── */

/**
 * NE2000 PCI 카드를 초기화합니다.
 * PCI 버스를 스캔하여 NE2k 카드를 탐색하고 I/O 포트를 설정합니다.
 * @param rx_cb  수신 패킷 콜백 (NULL 이면 패킷 드롭)
 * @return 0 = 성공, -1 = 카드 없음
 */
int ne2000_init(ne2000_rx_cb_t rx_cb);

/**
 * 패킷을 송신합니다.
 * @param pkt  이더넷 프레임 (헤더 포함)
 * @param len  길이 (최대 1514 바이트)
 * @return 0 = 성공, -1 = 실패
 */
int ne2000_send(const uint8_t *pkt, uint16_t len);

/**
 * MAC 주소를 가져옵니다.
 * @param out  6바이트 버퍼
 */
void ne2000_get_mac(uint8_t *out);

/**
 * 수신 인터럽트 / 폴링 처리기 — IRQ 핸들러나 타이머 틱에서 호출합니다.
 */
void ne2000_poll(void);

#endif /* PARINOS_NE2000_H */
