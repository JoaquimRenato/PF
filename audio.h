#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_ADC_CH   2

#define AUDIO_THR_ON   80
#define AUDIO_THR_OFF  40

#define AUDIO_DOT_MAX  240
#define AUDIO_MIN_TONE  75

#define AUDIO_CHAR_GAP  280
#define AUDIO_WORD_GAP  1000

// Confirmação de borda: quantas leituras consecutivas para aceitar ON ou OFF.
// 2 = elimina spikes sem atrasar a detecção.
#define AUDIO_CONFIRM   2

typedef enum {
    AUDIO_IDLE,
    AUDIO_SYMBOL,
    AUDIO_CHAR_READY,
    AUDIO_WORD_END
} audio_event_t;

void           audio_init(void);
audio_event_t  audio_process(void);
const char    *audio_current_symbols(void);
void           audio_clear_symbols(void);
void           audio_reset(void);
uint16_t       audio_peak(void);
uint32_t       audio_last_duration(void);

#endif