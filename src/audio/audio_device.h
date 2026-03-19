/*
 * 0xFX — Audio device manager (standalone only)
 */
#ifndef FX_AUDIO_DEVICE_H
#define FX_AUDIO_DEVICE_H

#include <stdbool.h>

bool fx_audio_init(void);
void fx_audio_shutdown(void);

#endif /* FX_AUDIO_DEVICE_H */
