//
// src/sound/sound.c — 사운드 서브시스템 구현
//

#include "sound.h"
#include "../drivers/timer.h"
#include <stddef.h>

static SoundDevice *g_default = NULL;

void snd_register(SoundDevice *dev) {
    if (!dev || !dev->ops) return;
    if (dev->ops->init(dev))
        g_default = dev;
}

SoundDevice *snd_default(void) { return g_default; }

void snd_tone(uint32_t freq_hz, uint32_t ms) {
    if (!g_default) return;
    snd_dev_play_tone(g_default, freq_hz);
    sleep(ms);
    snd_dev_stop(g_default);
}

bool snd_wav(const char *path) {
    if (!g_default) return false;
    return snd_dev_play_wav(g_default, path);
}
