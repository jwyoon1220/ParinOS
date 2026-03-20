//
// src/drivers/speaker.h
// PC 스피커 드라이버 (PIT 채널 2 기반)
// WAV 파일 재생 및 단순 비프음 지원
//

#ifndef PARINOS_SPEAKER_H
#define PARINOS_SPEAKER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────────────────
// PC 스피커 I/O 포트
// ─────────────────────────────────────────────────────────────────────────────
#define PIT_CHANNEL2_PORT  0x42   // PIT 채널 2 데이터 포트
#define PIT_CMD_PORT       0x43   // PIT 명령어 포트
#define PC_SPEAKER_PORT    0x61   // 시스템 컨트롤 포트 (PC 스피커 on/off)

// PC 스피커 활성화 비트 (bit 0: Gate2, bit 1: Speaker data)
#define SPEAKER_ENABLE_BITS 0x03

// PIT 기본 주파수 (Hz)
#define PIT_BASE_FREQ      1193180UL

// ─────────────────────────────────────────────────────────────────────────────
// WAV 파싱 결과 구조체
// (고정 바이트 오프셋 대신 청크 스캔으로 채워짐 — speaker.c 참고)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint32_t sample_rate;      // 예: 8000, 22050, 44100
    uint16_t num_channels;     // 1 = mono, 2 = stereo
    uint16_t bits_per_sample;  // 8 또는 16
    uint32_t data_size;        // audio data 바이트 수
} wav_info_t;

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

/** PC 스피커를 초기화합니다. */
void speaker_init(void);

/**
 * 지정된 주파수(Hz)의 비프음을 시작합니다.
 * speaker_stop()을 호출하기 전까지 계속 소리가 납니다.
 *
 * @param frequency  재생할 주파수 (Hz, 37~32767)
 */
void speaker_beep(uint32_t frequency);

/** PC 스피커 출력을 중지합니다. */
void speaker_stop(void);

/**
 * 지정된 주파수로 ms 밀리초 동안 비프음을 냅니다.
 *
 * @param frequency  주파수 (Hz)
 * @param ms         지속 시간 (밀리초)
 */
void speaker_beep_ms(uint32_t frequency, uint32_t ms);

/**
 * WAV 파일 경로를 받아 PC 스피커로 재생합니다.
 * 8-bit/16-bit, mono/stereo PCM WAV를 지원합니다.
 * (스테레오는 좌우 채널 평균으로 다운믹스)
 *
 * @param path  FAT32 파일 경로 (예: "/0/sound.wav")
 * @return      true = 성공, false = 실패
 */
bool speaker_play_wav(const char* path);

#endif // PARINOS_SPEAKER_H
