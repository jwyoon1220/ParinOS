//
// src/drivers/speaker.c
// PC 스피커 드라이버 구현
// PIT 채널 2를 이용한 방형파(square-wave) 출력 + WAV 재생
//

#include "speaker.h"
#include "../hal/io.h"
#include "../hal/vga.h"
#include "../drivers/timer.h"
#include "../fs/fat.h"

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
//   1. WAV 청크를 동적으로 스캔하여 fmt/data를 찾습니다 (고정 구조체 미사용).
//      - fmt 크기 16 초과(WAVEFORMATEX/WAVEFORMATEXTENSIBLE)나
//        fmt~data 사이 임의 청크(LIST, JUNK, bext 등)를 올바르게 처리합니다.
//   2. 각 샘플을 0~255 범위(8-bit 정규화)로 변환합니다.
//   3. 샘플 값을 주파수(Hz)로 매핑합니다 (37Hz ~ 8000Hz)
//   4. 각 샘플마다 PIT 채널 2를 업데이트하고 busy-wait 합니다.
// ─────────────────────────────────────────────────────────────────────────────

// busy-wait: 마이크로초 단위 딜레이 (클럭 기반 근사)
static void delay_us(uint32_t us) {
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

// 버퍼 리필 헬퍼 매크로
#define REFILL_BUF(file_ptr, buf, buf_avail, buf_pos, data_left, on_fail) \
    do { \
        uint32_t _tr = ((data_left) < 512u) ? (data_left) : 512u; \
        int _err2 = fat_file_read((file_ptr), (buf), (int)_tr, &(buf_avail)); \
        if (_err2 != FAT_ERR_NONE || (buf_avail) <= 0) { on_fail; } \
        (buf_pos) = 0; \
    } while (0)

bool speaker_play_wav(const char* path) {
    File file;
    int bytes_read;
    int err;

    klog_info("[SPEAKER] Opening: %s\n", path);

    // ── 1. 파일 열기 ──
    err = fat_file_open(&file, path, FAT_READ);
    if (err != FAT_ERR_NONE) {
        klog_error("[SPEAKER] Cannot open '%s': %s\n", path, fat_get_error(err));
        return false;
    }

    // ── 2. RIFF 프리앰블 읽기 (12 바이트: "RIFF" + size + "WAVE") ──
    {
        uint8_t preamble[12];
        err = fat_file_read(&file, preamble, 12, &bytes_read);
        if (err != FAT_ERR_NONE || bytes_read < 12) {
            klog_error("[SPEAKER] Failed to read RIFF preamble\n");
            fat_file_close(&file);
            return false;
        }
        if (preamble[0] != 'R' || preamble[1] != 'I' ||
            preamble[2] != 'F' || preamble[3] != 'F') {
            klog_error("[SPEAKER] Not a RIFF file\n");
            fat_file_close(&file);
            return false;
        }
        if (preamble[8] != 'W' || preamble[9]  != 'A' ||
            preamble[10] != 'V' || preamble[11] != 'E') {
            klog_error("[SPEAKER] Not a WAVE file\n");
            fat_file_close(&file);
            return false;
        }
    }

    // ── 3. 청크 스캔 루프: fmt와 data를 찾을 때까지 반복 ──
    //
    // 고정 44바이트 헤더 구조체를 사용하지 않는 이유:
    //   • fmt 청크 크기가 16을 초과 가능 (WAVEFORMATEX: 18, WAVEFORMATEXTENSIBLE: 40)
    //   • fmt와 data 사이에 LIST, JUNK, bext, smpl 등 임의의 청크 삽입 가능
    wav_info_t info;
    info.sample_rate     = 0;
    info.num_channels    = 0;
    info.bits_per_sample = 0;
    info.data_size       = 0;

    int found_fmt  = 0;
    int found_data = 0;

    while (!found_data) {
        uint8_t  chunk_hdr[8];
        err = fat_file_read(&file, chunk_hdr, 8, &bytes_read);
        if (err != FAT_ERR_NONE || bytes_read < 8) break;

        uint32_t chunk_size = (uint32_t) chunk_hdr[4]
                            | ((uint32_t)chunk_hdr[5] <<  8)
                            | ((uint32_t)chunk_hdr[6] << 16)
                            | ((uint32_t)chunk_hdr[7] << 24);

        if (chunk_hdr[0]=='f' && chunk_hdr[1]=='m' &&
            chunk_hdr[2]=='t' && chunk_hdr[3]==' ') {
            // ── fmt 청크: 최소 16바이트만 읽고 나머지는 seek로 건너뜀 ──
            if (chunk_size < 16) {
                klog_error("[SPEAKER] fmt chunk too small (%u bytes)\n",
                           (unsigned)chunk_size);
                fat_file_close(&file);
                return false;
            }
            uint8_t fmt_buf[16];
            err = fat_file_read(&file, fmt_buf, 16, &bytes_read);
            if (err != FAT_ERR_NONE || bytes_read < 16) {
                klog_error("[SPEAKER] Failed to read fmt chunk\n");
                fat_file_close(&file);
                return false;
            }

            uint16_t audio_format = (uint16_t)(fmt_buf[0] | (fmt_buf[1] << 8));
            if (audio_format != 1) {
                klog_error("[SPEAKER] Only PCM WAV (format=1) supported, got %d\n",
                           (int)audio_format);
                fat_file_close(&file);
                return false;
            }
            info.num_channels    = (uint16_t)(fmt_buf[2]  | (fmt_buf[3]  << 8));
            info.sample_rate     = (uint32_t)(fmt_buf[4]  | (fmt_buf[5]  << 8)
                                            | (fmt_buf[6] << 16) | ((uint32_t)fmt_buf[7] << 24));
            // fmt_buf 오프셋: [0-1]=audio_format [2-3]=num_channels [4-7]=sample_rate
            //                 [8-11]=byte_rate   [12-13]=block_align [14-15]=bits_per_sample
            info.bits_per_sample = (uint16_t)(fmt_buf[14] | (fmt_buf[15] << 8));

            // fmt 확장 바이트 건너뜀
            if (chunk_size > 16) {
                fat_file_seek(&file, (int)(chunk_size - 16), FAT_SEEK_CURR);
            }
            found_fmt = 1;

        } else if (chunk_hdr[0]=='d' && chunk_hdr[1]=='a' &&
                   chunk_hdr[2]=='t' && chunk_hdr[3]=='a') {
            // ── data 청크: 파일 커서가 이제 첫 번째 오디오 샘플을 가리킴 ──
            if (!found_fmt) {
                klog_error("[SPEAKER] 'data' chunk before 'fmt' chunk\n");
                fat_file_close(&file);
                return false;
            }
            info.data_size = chunk_size;
            found_data = 1;

        } else {
            // 알 수 없는 청크 건너뜀 (LIST, JUNK, bext, smpl, id3  등)
            if (chunk_size > 0)
                fat_file_seek(&file, (int)chunk_size, FAT_SEEK_CURR);
        }

        // RIFF 규약: 청크 크기가 홀수면 1바이트 패딩
        if (!found_data && (chunk_size & 1u))
            fat_file_seek(&file, 1, FAT_SEEK_CURR);
    }

    if (!found_fmt || !found_data || info.data_size == 0) {
        klog_error("[SPEAKER] WAV: fmt or data chunk not found\n");
        fat_file_close(&file);
        return false;
    }

    klog_info("[SPEAKER] WAV: %dHz, %dch, %d-bit, %d bytes\n",
              (int)info.sample_rate,
              (int)info.num_channels,
              (int)info.bits_per_sample,
              (int)info.data_size);

    // ── 4. 재생 파라미터 설정 ──
    uint32_t sample_rate = info.sample_rate ? info.sample_rate : 8000u;
    uint32_t us_per_sample = 1000000UL / sample_rate;

    uint16_t channels  = info.num_channels;
    uint16_t bps       = info.bits_per_sample;
    uint32_t data_left = info.data_size;

    // ── 5. 읽기 버퍼 ──
    uint8_t buf[512];
    int     buf_avail = 0;
    int     buf_pos   = 0;

    // 스피커 활성화
    uint8_t ctrl = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, ctrl | SPEAKER_ENABLE_BITS);

    // ── 6. 샘플 재생 루프 ──
    while (data_left > 0) {
        // 버퍼가 비었으면 채우기
        if (buf_pos >= buf_avail) {
            REFILL_BUF(&file, buf, buf_avail, buf_pos, data_left, break);
        }

        uint32_t sample_sum = 0;
        uint8_t  sample_cnt = 0;

        for (uint16_t ch = 0; ch < channels; ch++) {
            if (bps == 8) {
                // ── 8-bit unsigned PCM (0~255) ──
                if (buf_pos >= buf_avail) {
                    if (data_left == 0) goto playback_done;
                    REFILL_BUF(&file, buf, buf_avail, buf_pos, data_left, goto playback_done);
                }
                sample_sum += buf[buf_pos++];
                data_left--;

            } else if (bps == 16) {
                // ── 16-bit signed PCM → 0~255 변환 ──
                //
                // 버퍼 경계 처리: 샘플의 하위 바이트(LSB)와 상위 바이트(MSB)가
                // 서로 다른 512바이트 읽기 버퍼에 걸칠 수 있습니다.
                // 기존 코드는 이 경우 1바이트만 건너뛰어 스트림이 어긋났습니다.
                // 이제 필요시 버퍼를 리필하여 항상 온전한 2바이트를 읽습니다.
                if (data_left < 2) goto playback_done;

                // LSB 읽기
                if (buf_pos >= buf_avail) {
                    REFILL_BUF(&file, buf, buf_avail, buf_pos, data_left, goto playback_done);
                }
                uint8_t lo = buf[buf_pos++];
                data_left--;

                // MSB 읽기 (경계 넘어가면 리필)
                if (buf_pos >= buf_avail) {
                    REFILL_BUF(&file, buf, buf_avail, buf_pos, data_left, goto playback_done);
                }
                uint8_t hi = buf[buf_pos++];
                data_left--;

                int16_t s16 = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
                sample_sum += (uint8_t)(((int32_t)s16 + 32768) >> 8);

            } else {
                // 지원하지 않는 비트 깊이: 1바이트 건너뜀
                if (buf_pos >= buf_avail) {
                    REFILL_BUF(&file, buf, buf_avail, buf_pos, data_left, goto playback_done);
                }
                buf_pos++;
                data_left--;
            }
            sample_cnt++;
        }

        if (sample_cnt == 0) break;
        uint8_t amplitude = (uint8_t)(sample_sum / sample_cnt);

        // 진폭 → 주파수 매핑 (37Hz ~ 8000Hz, 선형 보간)
        uint32_t freq = 37u + ((uint32_t)amplitude * (8000u - 37u)) / 255u;

        // PIT 채널 2 업데이트
        uint32_t divisor = PIT_BASE_FREQ / freq;
        if (divisor > 0xFFFF) divisor = 0xFFFF;
        if (divisor < 1)      divisor = 1;
        outb(PIT_CMD_PORT,      0xB6);
        outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xFF));
        outb(PIT_CHANNEL2_PORT, (uint8_t)((divisor >> 8) & 0xFF));

        delay_us(us_per_sample);
    }

playback_done:
    speaker_stop();
    fat_file_close(&file);
    klog_ok("[SPEAKER] Playback finished\n");
    return true;
}
