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
// WAV 파일 헤더 구조체 (PCM, little-endian)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
typedef struct {
    // RIFF 청크
    char     riff_id[4];    // "RIFF"
    uint32_t file_size;     // 파일 전체 크기 - 8
    char     wave_id[4];    // "WAVE"
    // fmt 청크
    char     fmt_id[4];     // "fmt "
    uint32_t fmt_size;      // fmt 청크 크기 (PCM = 16)
    uint16_t audio_format;  // PCM = 1
    uint16_t num_channels;  // 1 = mono, 2 = stereo
    uint32_t sample_rate;   // 예: 8000, 22050, 44100
    uint32_t byte_rate;     // sample_rate * num_channels * bits/8
    uint16_t block_align;   // num_channels * bits/8
    uint16_t bits_per_sample; // 8 또는 16
    // data 청크
    char     data_id[4];    // "data"
    uint32_t data_size;     // 오디오 데이터 크기
} wav_header_t;
#pragma pack(pop)

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
