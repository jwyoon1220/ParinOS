//
// src/drivers/speaker.c
// PC 스피커 드라이버 구현
// PIT 채널 2를 이용한 방형파(square-wave) 출력 + WAV 재생
//

#include "speaker.h"
#include "../io.h"
#include "../vga.h"
#include "../drivers/timer.h"
#include "../fs/fs.h"
#include "../fs/fat.h"
#include "../std/string.h"

// ─────────────────────────────────────────────────────────────────────────────
// 내부 헬퍼: PIT 채널 2에 주파수 설정
// ─────────────────────────────────────────────────────────────────────────────
static void pit_set_channel2(uint32_t frequency) {
    if (frequency == 0) return;

    uint32_t divisor = PIT_BASE_FREQ / frequency;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor < 1)      divisor = 1;

    // PIT 명령어: 채널 2(bits 7-6=10), 로/하이 바이트(bits 5-4=11), 방형파(bits 3-1=011)
    outb(PIT_CMD_PORT, 0xB6);
    outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

// ─────────────────────────────────────────────────────────────────────────────
// 공개 API 구현
// ─────────────────────────────────────────────────────────────────────────────

void speaker_init(void) {
    // 스피커를 끄고 시작
    uint8_t ctrl = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, ctrl & ~SPEAKER_ENABLE_BITS);
    klog_ok("[SPEAKER] PC Speaker driver initialized\n");
}

void speaker_beep(uint32_t frequency) {
    pit_set_channel2(frequency);
    // 시스템 컨트롤 포트의 비트 0(Gate2)과 비트 1(Speaker)을 활성화
    uint8_t ctrl = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, ctrl | SPEAKER_ENABLE_BITS);
}

void speaker_stop(void) {
    uint8_t ctrl = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, ctrl & ~SPEAKER_ENABLE_BITS);
}

void speaker_beep_ms(uint32_t frequency, uint32_t ms) {
    speaker_beep(frequency);
    sleep(ms);
    speaker_stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// WAV 재생 구현
//
// PC 스피커는 단순 방형파 출력만 가능합니다 (1-bit DAC 유사).
// 각 샘플을 진폭에 따라 주파수를 변조하는 방식(Delta Sigma PWM)으로 재생합니다.
//
// 알고리즘:
//   1. WAV 헤더를 파싱하여 sample_rate, bits_per_sample, num_channels를 얻습니다.
//   2. 각 샘플을 0~255 범위(8-bit 정규화)로 변환합니다.
//   3. 샘플 값을 주파수(Hz)로 매핑합니다: 낮은 진폭 → 낮은 Hz, 높은 진폭 → 높은 Hz
//      (인간 청각 가청 범위 37Hz~8000Hz)
//   4. 각 샘플마다 PIT 채널 2를 업데이트하고, 다음 샘플까지 busy-wait 합니다.
//
// 주의: 샘플 레이트가 높을수록 CPU 부하가 증가합니다.
//       재생 중 인터럽트는 계속 처리됩니다.
// ─────────────────────────────────────────────────────────────────────────────

// 1 샘플 주기(마이크로초) 동안 대기하는 busy-wait 루프
// timer.c의 get_total_ticks()는 1ms 해상도이므로,
// 고주파 샘플링에는 CPU 클럭 카운터 기반 딜레이를 사용합니다.
static void delay_us(uint32_t us) {
    // 근사 딜레이: i686 @ ~400MHz 에서 루프 1회 ≈ 10ns
    // 1us ≈ 100회 반복 (보수적 추정)
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

bool speaker_play_wav(const char* path) {
    File file;
    wav_header_t header;
    int bytes_read;
    int err;

    klog_info("[SPEAKER] Opening: %s\n", path);

    // 1. 파일 열기
    err = fat_file_open(&file, path, FAT_READ);
    if (err != FAT_ERR_NONE) {
        klog_error("[SPEAKER] Cannot open '%s': %s\n", path, fat_get_error(err));
        return false;
    }

    // 2. WAV 헤더 읽기
    err = fat_file_read(&file, (char*)&header, sizeof(wav_header_t), &bytes_read);
    if (err != FAT_ERR_NONE || bytes_read < (int)sizeof(wav_header_t)) {
        klog_error("[SPEAKER] Failed to read WAV header\n");
        fat_file_close(&file);
        return false;
    }

    // 3. WAV 유효성 검사
    if (header.riff_id[0] != 'R' || header.riff_id[1] != 'I' ||
        header.riff_id[2] != 'F' || header.riff_id[3] != 'F') {
        klog_error("[SPEAKER] Not a RIFF file\n");
        fat_file_close(&file);
        return false;
    }
    if (header.wave_id[0] != 'W' || header.wave_id[1] != 'A' ||
        header.wave_id[2] != 'V' || header.wave_id[3] != 'E') {
        klog_error("[SPEAKER] Not a WAVE file\n");
        fat_file_close(&file);
        return false;
    }
    if (header.audio_format != 1) {
        klog_error("[SPEAKER] Only PCM WAV (format=1) supported, got %d\n",
                   (int)header.audio_format);
        fat_file_close(&file);
        return false;
    }

    klog_info("[SPEAKER] WAV: %dHz, %dch, %d-bit, %d bytes\n",
              (int)header.sample_rate,
              (int)header.num_channels,
              (int)header.bits_per_sample,
              (int)header.data_size);

    // 4. 샘플 주기 계산 (마이크로초)
    uint32_t sample_rate = header.sample_rate;
    if (sample_rate == 0) sample_rate = 8000;
    uint32_t us_per_sample = 1000000UL / sample_rate;

    uint8_t  channels  = (uint8_t)header.num_channels;
    uint8_t  bps       = (uint8_t)header.bits_per_sample;
    uint32_t data_left = header.data_size;

    // 5. 읽기 버퍼 (최대 512바이트씩 처리)
    char buf[512];
    int  buf_avail = 0;
    int  buf_pos   = 0;

    // 스피커 활성화
    uint8_t ctrl = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, ctrl | SPEAKER_ENABLE_BITS);

    // 6. 샘플 재생 루프
    while (data_left > 0) {
        // 버퍼가 비었으면 채우기
        if (buf_pos >= buf_avail) {
            uint32_t to_read = (data_left < 512) ? data_left : 512;
            err = fat_file_read(&file, buf, (int)to_read, &buf_avail);
            if (err != FAT_ERR_NONE || buf_avail <= 0) break;
            buf_pos = 0;
        }

        // 샘플 추출 (채널 평균, 0~255 정규화)
        uint32_t sample_sum = 0;
        uint8_t  sample_cnt = 0;

        for (uint8_t ch = 0; ch < channels && buf_pos < buf_avail; ch++) {
            if (bps == 8) {
                // 8-bit unsigned PCM (0~255)
                sample_sum += (uint8_t)buf[buf_pos++];
                data_left--;
            } else if (bps == 16) {
                // 16-bit signed PCM (-32768~32767) → 0~255 변환
                if (buf_pos + 1 < buf_avail) {
                    int16_t s16 = (int16_t)((uint8_t)buf[buf_pos] |
                                             ((uint8_t)buf[buf_pos+1] << 8));
                    sample_sum += (uint8_t)(((int32_t)s16 + 32768) >> 8);
                    buf_pos   += 2;
                    data_left -= 2;
                } else {
                    // 버퍼 경계 처리: 한 바이트 건너뜀
                    buf_pos++;
                    data_left--;
                }
            } else {
                // 지원하지 않는 비트 깊이: 1바이트 건너뜀
                buf_pos++;
                data_left--;
            }
            sample_cnt++;
        }

        if (sample_cnt == 0) break;
        uint8_t amplitude = (uint8_t)(sample_sum / sample_cnt); // 0~255

        // 진폭 → 주파수 매핑 (37Hz ~ 8000Hz)
        // 선형 보간: freq = 37 + amplitude * (8000 - 37) / 255
        uint32_t freq = 37 + ((uint32_t)amplitude * (8000 - 37)) / 255;

        // PIT 채널 2 업데이트 (직접 포트 쓰기로 오버헤드 최소화)
        uint32_t divisor = PIT_BASE_FREQ / freq;
        if (divisor > 0xFFFF) divisor = 0xFFFF;
        if (divisor < 1)      divisor = 1;
        outb(PIT_CMD_PORT,        0xB6);
        outb(PIT_CHANNEL2_PORT,   (uint8_t)(divisor & 0xFF));
        outb(PIT_CHANNEL2_PORT,   (uint8_t)((divisor >> 8) & 0xFF));

        // 다음 샘플까지 대기
        delay_us(us_per_sample);
    }

    // 스피커 끄기
    speaker_stop();
    fat_file_close(&file);

    klog_ok("[SPEAKER] Playback finished\n");
    return true;
}
