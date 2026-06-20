//
// src/sound/sound.h — 커널 사운드 서브시스템 추상 인터페이스
//
// 사용 예:
//   snd_register(pc_speaker_device());   // 드라이버 등록
//   snd_tone(440, 200);                  // 440Hz 200ms
//   snd_wav("/0/beep.wav");              // WAV 파일 재생
//

#ifndef PARINOS_SOUND_H
#define PARINOS_SOUND_H

#include <stdint.h>
#include <stdbool.h>

/* ─── 추상 사운드 디바이스 (C OOP: vtable 패턴) ──────────────────────────── */

typedef struct SoundDevice SoundDevice;

typedef struct {
    bool  (*init)     (SoundDevice *self);
    void  (*play_tone)(SoundDevice *self, uint32_t freq_hz);
    void  (*stop)     (SoundDevice *self);
    bool  (*play_wav) (SoundDevice *self, const char *path);
} SoundOps;

struct SoundDevice {
    const char     *name;
    const SoundOps *ops;
    void           *priv;   /* 드라이버 전용 상태 */
};

/* vtable 호출 래퍼 — 타입 안전하게 메서드 호출 */
static inline bool snd_dev_init     (SoundDevice *d)                  { return d->ops->init(d); }
static inline void snd_dev_play_tone(SoundDevice *d, uint32_t hz)     { d->ops->play_tone(d, hz); }
static inline void snd_dev_stop     (SoundDevice *d)                  { d->ops->stop(d); }
static inline bool snd_dev_play_wav (SoundDevice *d, const char *p)   { return d->ops->play_wav(d, p); }

/* ─── 사운드 서브시스템 API ─────────────────────────────────────────────── */

/* 디바이스를 기본 사운드 장치로 등록하고 초기화합니다 */
void         snd_register(SoundDevice *dev);

/* 현재 등록된 기본 장치를 반환합니다 (없으면 NULL) */
SoundDevice *snd_default (void);

/* 기본 장치로 freq_hz 주파수 비프음을 ms 밀리초 동안 재생합니다 */
void snd_tone(uint32_t freq_hz, uint32_t ms);

/* 기본 장치로 WAV 파일을 재생합니다 */
bool snd_wav(const char *path);

#endif /* PARINOS_SOUND_H */
