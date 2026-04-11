/*
 * src/drivers/ne2000.c — NE2000 / RTL8019 PCI NIC 드라이버
 *
 * 지원 칩:
 *   - Realtek RTL8029AS (QEMU ne2k_pci)
 *   - NE2000 호환 10 Mbps 이더넷
 *
 * I/O 레지스터 맵 (DP8390 기반, Page 0):
 *   BASE+0x00  CR   — Command Register
 *   BASE+0x01  PSTART — Page Start
 *   BASE+0x02  PSTOP  — Page Stop
 *   BASE+0x03  BNRY   — Boundary Pointer
 *   BASE+0x04  TPSR   — TX Page Start
 *   BASE+0x05  TBCR0  — TX Byte Count (lo)
 *   BASE+0x06  TBCR1  — TX Byte Count (hi)
 *   BASE+0x07  ISR    — Interrupt Status
 *   BASE+0x08  RSAR0  — Remote Start Address (lo)
 *   BASE+0x09  RSAR1  — Remote Start Address (hi)
 *   BASE+0x0A  RBCR0  — Remote Byte Count (lo)
 *   BASE+0x0B  RBCR1  — Remote Byte Count (hi)
 *   BASE+0x0C  RCR    — Receive Config
 *   BASE+0x0D  TCR    — Transmit Config
 *   BASE+0x0E  DCR    — Data Config
 *   BASE+0x0F  IMR    — Interrupt Mask
 *   BASE+0x10  DATA   — DMA 데이터 포트
 *   BASE+0x1F  RESET  — 리셋 포트
 *
 * NIC 내부 링 버퍼 (256-byte pages):
 *   Pages 0x40-0x4B : TX 버퍼 (2 페이지 = 512 bytes × 2)
 *   Pages 0x4C-0x7F : RX 링 버퍼
 */

#include "ne2000.h"
#include "pci.h"
#include "../hal/io.h"
#include "../hal/vga.h"

/* ── DP8390 레지스터 오프셋 ─────────────────────────────────────── */
#define NE_CR     0x00
#define NE_PSTART 0x01
#define NE_PSTOP  0x02
#define NE_BNRY   0x03
#define NE_TPSR   0x04
#define NE_TBCR0  0x05
#define NE_TBCR1  0x06
#define NE_ISR    0x07
#define NE_RSAR0  0x08
#define NE_RSAR1  0x09
#define NE_RBCR0  0x0A
#define NE_RBCR1  0x0B
#define NE_RCR    0x0C
#define NE_TCR    0x0D
#define NE_DCR    0x0E
#define NE_IMR    0x0F
#define NE_DATA   0x10
#define NE_RESET  0x1F

/* CR 비트 */
#define CR_STOP   0x01
#define CR_START  0x02
#define CR_TX     0x04
#define CR_RD0    0x08
#define CR_RD1    0x10
#define CR_RD2    0x20
#define CR_PS0    0x40
#define CR_PS1    0x80

/* ISR 비트 */
#define ISR_PRX   0x01  /* 패킷 수신 OK */
#define ISR_PTX   0x02  /* 패킷 송신 OK */
#define ISR_RXE   0x04  /* 수신 에러   */
#define ISR_TXE   0x08  /* 송신 에러   */
#define ISR_OVW   0x10  /* 링 버퍼 오버플로우 */
#define ISR_CNT   0x20
#define ISR_RDC   0x40  /* Remote DMA 완료 */
#define ISR_RST   0x80

/* Page 1 레지스터 (CR_PS0 세트) */
#define NE_PAR0   0x01   /* 물리 주소 바이트 0..5 */
#define NE_CURR   0x07   /* Current Page */
#define NE_MAR0   0x08   /* Multicast 주소 */

/* ── 링 버퍼 레이아웃 ────────────────────────────────────────────── */
#define TX_START  0x40
#define TX_PAGES  6     /* 6 × 256 = 1536 bytes (최대 이더넷 프레임) */
#define RX_START  (TX_START + TX_PAGES)   /* 0x46 */
#define RX_STOP   0x80

/* ── 수신 패킷 헤더 (NIC가 링 버퍼에 기록) ─────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  status;   /* 수신 상태 플래그 */
    uint8_t  next;     /* 다음 패킷 페이지 번호 */
    uint16_t length;   /* 헤더 4바이트 포함한 패킷 총 길이 */
} ne_rxhdr_t;

/* ── 상태 ────────────────────────────────────────────────────────── */
static uint16_t     g_iobase   = 0;
static uint8_t      g_mac[6]   = {0};
static uint8_t      g_next_pkt = RX_START + 1;
static ne2000_rx_cb_t g_rx_cb  = (ne2000_rx_cb_t)0;

/* ── 헬퍼: 레지스터 읽기/쓰기 ──────────────────────────────────── */
static inline void ne_write(uint8_t reg, uint8_t val) {
    outb((uint16_t)(g_iobase + reg), val);
}
static inline uint8_t ne_read(uint8_t reg) {
    return inb((uint16_t)(g_iobase + reg));
}

/* ── Remote DMA: NIC 내부 메모리 읽기 ──────────────────────────── */
static void ne_rdma_read(uint16_t src_addr, void *dst, uint16_t count) {
    /* count는 반드시 짝수 */
    if (count & 1) count++;

    ne_write(NE_RSAR0, (uint8_t)(src_addr & 0xFF));
    ne_write(NE_RSAR1, (uint8_t)(src_addr >> 8));
    ne_write(NE_RBCR0, (uint8_t)(count & 0xFF));
    ne_write(NE_RBCR1, (uint8_t)(count >> 8));
    ne_write(NE_CR,    CR_START | CR_RD0);  /* 원격 DMA Read */

    uint16_t *p = (uint16_t *)dst;
    uint16_t  n = count >> 1;
    for (uint16_t i = 0; i < n; i++)
        p[i] = inw((uint16_t)(g_iobase + NE_DATA));

    /* Remote DMA 완료 대기 */
    for (int i = 0; i < 0x1000; i++) {
        if (ne_read(NE_ISR) & ISR_RDC) break;
    }
    ne_write(NE_ISR, ISR_RDC);
}

/* ── Remote DMA: NIC 내부 메모리 쓰기 ──────────────────────────── */
static void ne_rdma_write(uint16_t dst_addr, const void *src, uint16_t count) {
    if (count & 1) count++;

    ne_write(NE_RSAR0, (uint8_t)(dst_addr & 0xFF));
    ne_write(NE_RSAR1, (uint8_t)(dst_addr >> 8));
    ne_write(NE_RBCR0, (uint8_t)(count & 0xFF));
    ne_write(NE_RBCR1, (uint8_t)(count >> 8));
    ne_write(NE_CR,    CR_START | CR_RD1);  /* 원격 DMA Write */

    const uint16_t *p = (const uint16_t *)src;
    uint16_t n = count >> 1;
    for (uint16_t i = 0; i < n; i++)
        outw((uint16_t)(g_iobase + NE_DATA), p[i]);

    for (int i = 0; i < 0x1000; i++) {
        if (ne_read(NE_ISR) & ISR_RDC) break;
    }
    ne_write(NE_ISR, ISR_RDC);
}

/* ── ne2000_init ─────────────────────────────────────────────────── */
int ne2000_init(ne2000_rx_cb_t rx_cb) {
    /* PCI 탐색 */
    pci_device_t *dev = pci_find_device(NE2000_VENDOR_ID, NE2000_DEVICE_ID);
    if (!dev) {
        klog_warn("[NE2K] RTL8029/NE2000 card not found\n");
        return -1;
    }

    pci_enable_device(dev);
    g_iobase = (uint16_t)(dev->bar[0] & ~3U);   /* I/O BAR (비트 0-1 = type 플래그) */
    g_rx_cb  = rx_cb;

    klog_info("[NE2K] found at I/O base 0x%x\n", (unsigned)g_iobase);

    /* ── 하드웨어 리셋 ─────────────────────────────────────────── */
    uint8_t tmp = ne_read(NE_RESET);
    ne_write(NE_RESET, tmp);
    /* RST 비트가 세트될 때까지 대기 */
    for (int i = 0; i < 0x10000; i++) {
        if (ne_read(NE_ISR) & ISR_RST) break;
    }
    ne_write(NE_ISR, 0xFF);  /* 모든 인터럽트 클리어 */

    /* ── 초기화 시퀀스 ─────────────────────────────────────────── */
    ne_write(NE_CR,    CR_STOP | CR_RD2);
    ne_write(NE_DCR,   0x49);   /* FIFO=8, BUS=16bit, 바이트 단위 */
    ne_write(NE_RBCR0, 0x00);
    ne_write(NE_RBCR1, 0x00);
    ne_write(NE_RCR,   0x20);   /* Monitor 모드 */
    ne_write(NE_TCR,   0x02);   /* Internal loopback */
    ne_write(NE_PSTART, RX_START);
    ne_write(NE_PSTOP,  RX_STOP);
    ne_write(NE_BNRY,   RX_START);
    ne_write(NE_TPSR,   TX_START);

    /* Page 1: MAC 주소 읽기 + CURR 설정 */
    ne_write(NE_CR, CR_STOP | CR_RD2 | CR_PS0);

    /* NE2k MAC은 PROM 첫 32 바이트에 인터리브로 저장됨 */
    uint8_t prom[32];
    ne_write(NE_CR, CR_STOP | CR_RD2);  /* Page 0 로 복귀 후 RDMA */
    ne_rdma_read(0x0000, prom, 32);

    for (int i = 0; i < 6; i++) g_mac[i] = prom[i * 2];

    klog_info("[NE2K] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
              g_mac[0], g_mac[1], g_mac[2],
              g_mac[3], g_mac[4], g_mac[5]);

    /* Page 1: PAR (MAC) + CURR 설정 */
    ne_write(NE_CR, CR_STOP | CR_RD2 | CR_PS0);
    for (int i = 0; i < 6; i++)
        ne_write((uint8_t)(NE_PAR0 + i), g_mac[i]);
    for (int i = 0; i < 8; i++)
        ne_write((uint8_t)(NE_MAR0 + i), 0xFF);  /* 멀티캐스트 수신 허용 */
    ne_write(NE_CURR, (uint8_t)(RX_START + 1));
    g_next_pkt = (uint8_t)(RX_START + 1);

    /* Page 0: 정상 모드 */
    ne_write(NE_CR,  CR_START | CR_RD2);
    ne_write(NE_TCR, 0x00);   /* Normal transmit */
    ne_write(NE_RCR, 0x04);   /* Broadcast 수신, 오류 패킷 무시 */
    ne_write(NE_ISR, 0xFF);
    ne_write(NE_IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);

    klog_info("[NE2K] initialized OK\n");
    return 0;
}

/* ── ne2000_send ─────────────────────────────────────────────────── */
int ne2000_send(const uint8_t *pkt, uint16_t len) {
    if (!g_iobase) return -1;
    if (len < 60) {
        /* 패딩 — 로컬 버퍼에서 처리 */
        uint8_t buf[60];
        for (int i = 0; i < 60; i++) buf[i] = (i < len) ? pkt[i] : 0;
        pkt = buf;
        len = 60;
    }
    /* NIC TX 버퍼에 기록 */
    ne_rdma_write((uint16_t)(TX_START * 256), pkt, len);

    /* 전송 시작 */
    ne_write(NE_CR,    CR_START | CR_RD2);
    ne_write(NE_TPSR,  TX_START);
    ne_write(NE_TBCR0, (uint8_t)(len & 0xFF));
    ne_write(NE_TBCR1, (uint8_t)(len >> 8));
    ne_write(NE_CR,    CR_START | CR_RD2 | CR_TX);

    /* 완료 대기 (간단한 폴링) */
    for (int i = 0; i < 0x100000; i++) {
        uint8_t isr = ne_read(NE_ISR);
        if (isr & (ISR_PTX | ISR_TXE)) {
            ne_write(NE_ISR, ISR_PTX | ISR_TXE);
            return (isr & ISR_TXE) ? -1 : 0;
        }
    }
    return -1;  /* timeout */
}

/* ── ne2000_get_mac ──────────────────────────────────────────────── */
void ne2000_get_mac(uint8_t *out) {
    for (int i = 0; i < 6; i++) out[i] = g_mac[i];
}

/* ── ne2000_poll ─────────────────────────────────────────────────── */
void ne2000_poll(void) {
    if (!g_iobase) return;

    uint8_t isr = ne_read(NE_ISR);
    if (!(isr & (ISR_PRX | ISR_RXE | ISR_OVW))) return;

    ne_write(NE_ISR, ISR_PRX | ISR_RXE);

    while (1) {
        /* Current 페이지 읽기 (Page 1) */
        ne_write(NE_CR, CR_START | CR_RD2 | CR_PS0);
        uint8_t curr = ne_read(NE_CURR);
        ne_write(NE_CR, CR_START | CR_RD2);

        if (g_next_pkt == curr) break;  /* 새 패킷 없음 */

        /* 수신 헤더 읽기 */
        ne_rxhdr_t hdr;
        ne_rdma_read((uint16_t)(g_next_pkt * 256), &hdr, sizeof(hdr));

        uint16_t pkt_len = hdr.length;
        if (pkt_len > 4) pkt_len = (uint16_t)(pkt_len - 4);  /* CRC 제거 */

        if (pkt_len > 0 && pkt_len <= 1514 && g_rx_cb) {
            uint8_t buf[1514];
            uint16_t data_src = (uint16_t)(g_next_pkt * 256 + sizeof(hdr));
            /* 링 버퍼 wrap-around 처리 */
            uint16_t end_page  = (uint16_t)(g_next_pkt * 256 + sizeof(hdr) + pkt_len);
            if ((end_page >> 8) < RX_STOP) {
                ne_rdma_read(data_src, buf, pkt_len);
            } else {
                /* wrap: 두 부분으로 나눠서 읽기 */
                uint16_t first = (uint16_t)(RX_STOP * 256 - data_src);
                ne_rdma_read(data_src, buf, first);
                ne_rdma_read((uint16_t)(RX_START * 256), buf + first,
                             (uint16_t)(pkt_len - first));
            }
            g_rx_cb(buf, pkt_len);
        }

        /* BNRY 업데이트 */
        g_next_pkt = hdr.next;
        uint8_t bnry = (hdr.next == RX_START)
                       ? (uint8_t)(RX_STOP - 1)
                       : (uint8_t)(hdr.next - 1);
        ne_write(NE_BNRY, bnry);
    }

    if (isr & ISR_OVW) {
        /* 링 버퍼 오버플로우 복구 */
        ne_write(NE_CR,  CR_STOP | CR_RD2);
        ne_write(NE_RBCR0, 0); ne_write(NE_RBCR1, 0);
        ne_write(NE_TCR, 0x02);
        ne_write(NE_CR,  CR_START | CR_RD2);
        ne_write(NE_ISR, ISR_OVW);
        ne_write(NE_TCR, 0x00);
    }
}
