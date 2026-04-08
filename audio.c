#include "audio.h"
#include "morse_decoder.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include <string.h>

static bool     was_active  = false;
static uint32_t tone_start  = 0;
static uint32_t sil_start   = 0;
static bool     in_seq      = false;
static char     sym_buf[MAX_SYMBOLS + 1] = "";
static uint8_t  sym_count   = 0;
static uint16_t last_peak   = 0;
static uint32_t last_dur    = 0;

// Contadores para confirmação de borda
static uint8_t confirm_on  = 0;
static uint8_t confirm_off = 0;

// Leitura simples: 1 amostra, desvio absoluto do centro.
// Igual ao que estava funcionando antes.
static void read_peak(void) {
    adc_select_input(AUDIO_ADC_CH);
    uint16_t v = adc_read();
    int dev = (int)v - 2048;
    last_peak = (uint16_t)(dev < 0 ? -dev : dev);
}

// Detecta estado com histéresis + confirmação de borda.
// Só muda de estado após AUDIO_CONFIRM leituras consecutivas no novo estado.
static bool detect_active(void) {
    read_peak();

    bool candidate = was_active
        ? (last_peak >= AUDIO_THR_OFF)   // já ativo: mantém com limiar baixo
        : (last_peak >= AUDIO_THR_ON);   // inativo: exige limiar alto para ligar

    if (candidate) {
        confirm_on++;
        confirm_off = 0;
    } else {
        confirm_off++;
        confirm_on = 0;
    }

    // Aceita transição OFF→ON após AUDIO_CONFIRM confirmações
    if (!was_active && confirm_on  >= AUDIO_CONFIRM) return true;
    // Aceita transição ON→OFF após AUDIO_CONFIRM confirmações
    if ( was_active && confirm_off >= AUDIO_CONFIRM) return false;

    // Sem transição confirmada: mantém estado atual
    return was_active;
}

void audio_init(void) { adc_gpio_init(28); }

void audio_reset(void) {
    was_active  = false;
    tone_start  = 0;
    sil_start   = 0;
    in_seq      = false;
    sym_count   = 0;
    sym_buf[0]  = '\0';
    last_dur    = 0;
    last_peak   = 0;
    confirm_on  = 0;
    confirm_off = 0;
}

audio_event_t audio_process(void) {
    bool     active = detect_active();
    uint32_t t      = to_ms_since_boot(get_absolute_time());

    // Borda de subida: silêncio → tom
    if (active && !was_active) {
        if (in_seq && sil_start > 0) {
            uint32_t gap = t - sil_start;

            if (gap >= AUDIO_WORD_GAP && sym_count > 0) {
                // Não inicia o tom ainda — retorna o evento primeiro.
                // Na próxima chamada, sil_start==0 e o tom começa normalmente.
                sil_start = 0;
                return AUDIO_WORD_END;
            }
            if (gap >= AUDIO_CHAR_GAP && sym_count > 0) {
                sil_start = 0;
                return AUDIO_CHAR_READY;
            }
        }
        // Só agora aceita o tom
        was_active = true;
        confirm_on = 0;
        tone_start = t;
        return AUDIO_IDLE;
    }

    // Borda de descida: tom → silêncio
    if (!active && was_active) {
        was_active  = false;
        confirm_off = 0;
        sil_start   = t;
        in_seq      = true;
        last_dur    = t - tone_start;

        if (last_dur < AUDIO_MIN_TONE) return AUDIO_IDLE;

        char sym = (last_dur < AUDIO_DOT_MAX) ? '.' : '-';
        if (sym_count < MAX_SYMBOLS) {
            sym_buf[sym_count++] = sym;
            sym_buf[sym_count]   = '\0';
        }
        return AUDIO_SYMBOL;
    }

    // Timeout passivo
    if (!active && in_seq && sil_start > 0) {
        uint32_t gap = t - sil_start;
        if (gap >= AUDIO_WORD_GAP && sym_count > 0) {
            sil_start = 0; in_seq = false; return AUDIO_WORD_END;
        }
        if (gap >= AUDIO_CHAR_GAP && sym_count > 0) {
            sil_start = 0; return AUDIO_CHAR_READY;
        }
    }

    return AUDIO_IDLE;
}

const char *audio_current_symbols(void) { return sym_buf; }
void        audio_clear_symbols(void)   { sym_count = 0; sym_buf[0] = '\0'; }
uint16_t    audio_peak(void)            { return last_peak; }
uint32_t    audio_last_duration(void)   { return last_dur; }