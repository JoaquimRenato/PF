#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "morse_decoder.h"
#include "ssd1306.h"

// ── Pinos ─────────────────────────────────────────────────────────────────────
#define BTN_A    5
#define BTN_B    6
#define JOY_CLK  22
#define JOY_Y    27
#define BUZZER   21
#define SDA      14
#define SCL      15

//Constantes
#define DOT_MS   80
#define DASH_MS  240
#define GAP_MS   80
#define CHAR_MS  240
#define WORD_MS  560
#define FREQ_HZ  700
#define MSG_MAX  32
#define DBNC_MS  200

//Alfabeto e estado
static const char ABC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
#define ABC_LEN ((int)(sizeof(ABC) - 1))

static ssd1306_t disp;
static char msg[MSG_MAX + 1] = "";
static int  sel = 0;

//Debounce
static bool dbnc(uint32_t *t) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - *t < DBNC_MS) return false;
    *t = now;
    return true;
}

// Display
static void disp_update(void) {
    morse_symbol_t syms[MAX_SYMBOLS];
    uint8_t n = 0;
    char mstr[MAX_SYMBOLS + 1] = "---";
    if (morse_encode(ABC[sel], syms, &n)) {
        for (uint8_t i = 0; i < n; i++)
            mstr[i] = syms[i] == MORSE_DOT ? '.' : '-';
        mstr[n] = '\0';
    }

    char l1[24], l2[24];
    snprintf(l1, sizeof(l1), "> %c  %s", ABC[sel], mstr);
    snprintf(l2, sizeof(l2), "MSG:%s", msg);

    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "MorseLink");
    ssd1306_draw_string(&disp, 0, 16, 1, l1);
    ssd1306_draw_string(&disp, 0, 32, 1, l2);
    ssd1306_draw_string(&disp, 0, 48, 1, "A+ B- OK=send");
    ssd1306_show(&disp);
}

//Buzzer
static uint slice, chan;

static void buzz(bool on) {
    pwm_set_chan_level(slice, chan, on ? 500 : 0);
}

static void play(const char *s) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "MorseLink");
    ssd1306_draw_string(&disp, 0, 20, 1, "Enviando...");
    ssd1306_draw_string(&disp, 0, 36, 1, s);
    ssd1306_show(&disp);

    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') { sleep_ms(WORD_MS - CHAR_MS); continue; }

        morse_symbol_t syms[MAX_SYMBOLS];
        uint8_t n = 0;
        if (!morse_encode(s[i], syms, &n)) continue;

        for (uint8_t j = 0; j < n; j++) {
            buzz(true);
            sleep_ms(syms[j] == MORSE_DOT ? DOT_MS : DASH_MS);
            buzz(false);
            if (j < n - 1) sleep_ms(GAP_MS);
        }
        sleep_ms(CHAR_MS);
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(void) {
    stdio_init_all();

    // Display
    i2c_init(i2c1, 400000);
    gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_pull_up(SDA);
    gpio_set_function(SCL, GPIO_FUNC_I2C); gpio_pull_up(SCL);
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);

    // Buzzer — clkdiv=64 para wrap caber em uint16_t
    // 125MHz / 64 = 1953125 Hz → wrap = 1953125/700-1 ≈ 2789
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(BUZZER);
    chan  = pwm_gpio_to_channel(BUZZER);
    pwm_set_clkdiv(slice, 64.0f);
    pwm_set_wrap(slice, 1953125 / FREQ_HZ - 1);
    pwm_set_enabled(slice, true);

    // ADC (joystick)
    adc_init();
    adc_gpio_init(JOY_Y);

    // Botões
    uint btn_pins[] = {BTN_A, BTN_B, JOY_CLK};
    for (int i = 0; i < 3; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
    }

    morse_decoder_init();
    disp_update();

    uint32_t ta = 0, tb = 0, tc = 0;

    while (true) {
        // Joystick: navega
        adc_select_input(1);
        uint16_t v = adc_read();
        if (v > 2400 || v < 1600) {
            sel = (sel + (v > 2400 ? 1 : -1) + ABC_LEN) % ABC_LEN;
            disp_update();
            sleep_ms(180);
        }

        // Botão A: adiciona char
        if (!gpio_get(BTN_A) && dbnc(&ta)) {
            size_t l = strlen(msg);
            if (l < MSG_MAX) { msg[l] = ABC[sel]; msg[l+1] = '\0'; }
            disp_update();
        }

        // Botão B: apaga último char
        if (!gpio_get(BTN_B) && dbnc(&tb)) {
            size_t l = strlen(msg);
            if (l > 0) msg[l-1] = '\0';
            disp_update();
        }

        // Joystick-click: envia
        if (!gpio_get(JOY_CLK) && dbnc(&tc)) {
            if (strlen(msg) > 0) {
                play(msg);
                memset(msg, 0, sizeof(msg));
            }
            disp_update();
        }

        sleep_ms(10);
    }
}