//
// src/sound/pc_speaker.h — PIT 채널 2 기반 PC 스피커 드라이버
//

#ifndef PARINOS_PC_SPEAKER_H
#define PARINOS_PC_SPEAKER_H

#include "sound.h"

/* PC 스피커 SoundDevice 싱글턴을 반환합니다.
 * snd_register() 에 전달하여 기본 장치로 등록하세요. */
SoundDevice *pc_speaker_device(void);

#endif /* PARINOS_PC_SPEAKER_H */
