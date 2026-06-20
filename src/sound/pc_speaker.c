//
// src/sound/pc_speaker.c — PIT 채널 2 PC 스피커 드라이버
//
// WAV 재생 알고리즘: Delta-Sigma 1-bit 변조
//   • 각 샘플에서 오차 누적기(error accumulator)를 운용
//   • 오차 > 0이면 스피커 ON, 이하면 OFF
//   → 주파수 변조 방식보다 실제 파형을 정확하게 재현
//
// 하드웨어 메모:
//   Port 0x61 bit0 = PIT Ch2 Gate, bit1 = Speaker Data
//   Gate=0이면 PIT 출력 강제 LOW → bit1 값이 스피커에 직접 인가
//   → 1-bit DAC로 직접 제어 가능
//

#include "pc_speaker.h"
#include <stddef.h>
#include "../hal/io.h"
#include "../hal/vga.h"
#include "../drivers/timer.h"
#include "../fs/fat.h"

/* ─── I/O 포트 상수 ─────────────────────────────────────────────────────── */
#define PIT_CH2_PORT        0x42u
#define PIT_CMD_PORT        0x43u
#define PC_SPK_PORT         0x61u
#define SPK_GATE_BIT        0x01u   /* bit0: PIT Ch2 Gate */
#define SPK_DATA_BIT        0x02u   /* bit1: Speaker Data */
#define PIT_BASE_FREQ       1193180UL

/* ─── 하드웨어 헬퍼 ─────────────────────────────────────────────────────── */

static void _pit2_set_freq(uint32_t freq) {
    uint32_t div = PIT_BASE_FREQ / freq;
    if (div > 0xFFFFu) div = 0xFFFFu;
    if (div < 1u)      div = 1u;
    outb(PIT_CMD_PORT, 0xB6u);                        /* ch2, lobyte/hibyte, square wave */
    outb(PIT_CH2_PORT, (uint8_t)(div & 0xFFu));
    outb(PIT_CH2_PORT, (uint8_t)((div >> 8) & 0xFFu));
}

/* play_tone: PIT 방형파 모드로 연속 비프 */
static inline void _spk_tone_on(void) {
    outb(PC_SPK_PORT, inb(PC_SPK_PORT) | (SPK_GATE_BIT | SPK_DATA_BIT));
}

/* play_wav: Gate를 끄고 Data bit만 제어 → 직접 1-bit 출력 */
static inline void _spk_1bit_on (uint8_t base) { outb(PC_SPK_PORT, base |  SPK_DATA_BIT); }
static inline void _spk_1bit_off(uint8_t base) { outb(PC_SPK_PORT, base & ~SPK_DATA_BIT); }

static inline void _spk_off(void) {
    outb(PC_SPK_PORT, inb(PC_SPK_PORT) & ~(SPK_GATE_BIT | SPK_DATA_BIT));
}

static void _delay_us(uint32_t us) {
    /* 클럭-기반 근사 busy-wait (호스트 속도에 따라 편차 있음) */
    volatile uint32_t n = us * 100u;
    while (n--) __asm__ __volatile__("nop");
}

/* ─── WAV 파싱 ──────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_size;
} WavInfo;

static bool _parse_wav(File *f, WavInfo *out) {
    int br;

    /* RIFF/WAVE 프리앰블 */
    uint8_t pre[12];
    fat_file_read(f, pre, 12, &br);
    if (br < 12
     || pre[0]!='R'||pre[1]!='I'||pre[2]!='F'||pre[3]!='F'
     || pre[8]!='W'||pre[9]!='A'||pre[10]!='V'||pre[11]!='E')
        return false;

    bool got_fmt = false, got_data = false;

    while (!got_data) {
        uint8_t ch[8];
        fat_file_read(f, ch, 8, &br);
        if (br < 8) break;

        uint32_t sz = (uint32_t)ch[4]
                    | ((uint32_t)ch[5] <<  8)
                    | ((uint32_t)ch[6] << 16)
                    | ((uint32_t)ch[7] << 24);

        if (ch[0]=='f' && ch[1]=='m' && ch[2]=='t' && ch[3]==' ') {
            if (sz < 16) return false;
            uint8_t fb[16];
            fat_file_read(f, fb, 16, &br);
            if (br < 16) return false;
            if ((uint16_t)(fb[0] | (fb[1]<<8)) != 1) {
                klog_error("[SND] Only PCM WAV supported\n");
                return false;
            }
            out->channels        = (uint16_t)(fb[2]  | (fb[3]  << 8));
            out->sample_rate     = (uint32_t)(fb[4]  | (fb[5]  << 8)
                                             | (fb[6] << 16) | ((uint32_t)fb[7] << 24));
            out->bits_per_sample = (uint16_t)(fb[14] | (fb[15] << 8));
            if (sz > 16)
                fat_file_seek(f, (int)(sz - 16), FAT_SEEK_CURR);
            got_fmt = true;

        } else if (ch[0]=='d' && ch[1]=='a' && ch[2]=='t' && ch[3]=='a') {
            if (!got_fmt) return false;
            out->data_size = sz;
            got_data = true;

        } else {
            /* 알 수 없는 청크(LIST, JUNK 등) 건너뜀 */
            if (sz > 0)
                fat_file_seek(f, (int)sz, FAT_SEEK_CURR);
        }

        /* RIFF 홀수 청크 패딩 */
        if (!got_data && (sz & 1u))
            fat_file_seek(f, 1, FAT_SEEK_CURR);
    }

    return got_fmt && got_data && out->data_size > 0;
}

/* ─── 스트리밍 버퍼 헬퍼 ────────────────────────────────────────────────── */

typedef struct {
    File    *file;
    uint8_t  buf[512];
    int      avail;
    int      pos;
    uint32_t left;     /* 남은 data 바이트 */
} WavStream;

/* 1바이트 읽기; 실패 시 0 반환하고 *ok = false */
static uint8_t _stream_read(WavStream *s, bool *ok) {
    if (!*ok || s->left == 0) { *ok = false; return 0; }
    if (s->pos >= s->avail) {
        uint32_t tr = s->left < 512u ? s->left : 512u;
        fat_file_read(s->file, s->buf, (int)tr, &s->avail);
        s->pos = 0;
        if (s->avail <= 0) { *ok = false; return 0; }
    }
    s->left--;
    return s->buf[s->pos++];
}

/* ─── 채널 믹스다운: 한 프레임 → 8-bit mono 진폭 ─────────────────────── */

static uint8_t _read_frame(WavStream *s, const WavInfo *info, bool *ok) {
    uint32_t sum = 0;
    for (uint16_t ch = 0; ch < info->channels; ch++) {
        if (info->bits_per_sample == 8) {
            sum += _stream_read(s, ok);
        } else if (info->bits_per_sample == 16) {
            uint8_t lo = _stream_read(s, ok);
            uint8_t hi = _stream_read(s, ok);
            if (*ok) {
                int16_t s16 = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
                sum += (uint8_t)(((int32_t)s16 + 32768) >> 8);
            }
        } else {
            _stream_read(s, ok); /* unsupported bps — skip */
            sum += 128;
        }
    }
    return (uint8_t)(sum / (info->channels ? info->channels : 1u));
}

/* ─── vtable 구현 ───────────────────────────────────────────────────────── */

static bool _init(SoundDevice *self) {
    (void)self;
    _spk_off();
    klog_ok("[SND] PC Speaker ready\n");
    return true;
}

static void _play_tone(SoundDevice *self, uint32_t freq_hz) {
    (void)self;
    if (freq_hz == 0) { _spk_off(); return; }
    _pit2_set_freq(freq_hz);
    _spk_tone_on();
}

static void _stop(SoundDevice *self) {
    (void)self;
    _spk_off();
}

static bool _play_wav(SoundDevice *self, const char *path) {
    (void)self;

    File f;
    if (fat_file_open(&f, path, FAT_READ) != FAT_ERR_NONE) {
        klog_error("[SND] Cannot open '%s'\n", path);
        return false;
    }

    WavInfo info = {0};
    if (!_parse_wav(&f, &info)) {
        klog_error("[SND] Invalid WAV: %s\n", path);
        fat_file_close(&f);
        return false;
    }

    klog_info("[SND] %s — %dHz %dch %dbit %d bytes\n",
              path,
              (int)info.sample_rate, (int)info.channels,
              (int)info.bits_per_sample, (int)info.data_size);

    uint32_t sr            = info.sample_rate ? info.sample_rate : 8000u;
    uint32_t us_per_sample = 1000000UL / sr;

    WavStream stream = { .file = &f, .avail = 0, .pos = 0, .left = info.data_size };

    /* Gate=0: PIT 출력 비활성, bit1만으로 스피커 직접 제어 */
    uint8_t base = inb(PC_SPK_PORT) & ~(SPK_GATE_BIT | SPK_DATA_BIT);
    outb(PC_SPK_PORT, base);

    /* Delta-Sigma 1-bit DAC */
    int32_t error = 0;
    bool    ok    = true;

    while (ok && stream.left > 0) {
        uint8_t amp = _read_frame(&stream, &info, &ok);
        if (!ok) break;

        error += (int32_t)amp - 128;
        if (error > 0) {
            _spk_1bit_on(base);
            error -= 255;
        } else {
            _spk_1bit_off(base);
        }

        _delay_us(us_per_sample);
    }

    _spk_off();
    fat_file_close(&f);
    klog_ok("[SND] Playback done\n");
    return true;
}

/* ─── 싱글턴 인스턴스 ───────────────────────────────────────────────────── */

static const SoundOps g_pc_ops = {
    .init      = _init,
    .play_tone = _play_tone,
    .stop      = _stop,
    .play_wav  = _play_wav,
};

static SoundDevice g_pc_dev = {
    .name = "PC Speaker (PIT ch2)",
    .ops  = &g_pc_ops,
    .priv = NULL,
};

SoundDevice *pc_speaker_device(void) { return &g_pc_dev; }
