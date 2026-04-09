#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "morse_decoder.h"
#include "audio.h"
#include "ssd1306.h"

// Pinos
#define BTN_A    5
#define BTN_B    6
#define JOY_CLK  22
#define JOY_X    26
#define JOY_Y    27
#define BUZZER   21
#define SDA      14
#define SCL      15

// Constantes TX
#define DOT_MS   80
#define DASH_MS  240
#define GAP_MS   80
#define CHAR_MS  240
#define WORD_MS  560
#define FREQ_HZ  700
#define MSG_MAX  32
#define DBNC_MS  200

static const char ABC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
#define ABC_LEN ((int)(sizeof(ABC) - 1))

// Telas
typedef enum { SCREEN_MENU, SCREEN_TX,
               SCREEN_RX_IDLE, SCREEN_RX_REC, SCREEN_RX_RESULT } screen_t;
static screen_t screen = SCREEN_MENU;

// Estado
static ssd1306_t disp;
static char tx_msg[MSG_MAX+1]         = "";
static int  sel                       = 0;
static char rx_history[MSG_MAX+1]     = "";
static char rx_current[MAX_SYMBOLS+1] = "";

// Flags IRQ
static volatile bool ev_a   = false;
static volatile bool ev_b   = false;
static volatile bool ev_clk = false;

static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;
    static uint32_t ta = 0, tb = 0, tclk = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (gpio == BTN_A   && now-ta   >= DBNC_MS) { ta   = now; ev_a   = true; }
    if (gpio == BTN_B   && now-tb   >= DBNC_MS) { tb   = now; ev_b   = true; }
    if (gpio == JOY_CLK && now-tclk >= DBNC_MS) { tclk = now; ev_clk = true; }
}

// Joystick
static int joy_axis(uint ch) {
    adc_select_input(ch);
    uint16_t v = adc_read();
    if (v > 2400) return +1;
    if (v < 1600) return -1;
    return 0;
}

// Display
static void disp_menu(int cur) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "== MorseLink ==");
    ssd1306_draw_string(&disp, 0, 22, 1, cur==0 ? "> TRANSMISSOR" : "  TRANSMISSOR");
    ssd1306_draw_string(&disp, 0, 38, 1, cur==1 ? "> RECEPTOR"    : "  RECEPTOR");
    ssd1306_draw_string(&disp, 0, 52, 1, "X=mover  A=ok");
    ssd1306_show(&disp);
}

static void disp_tx(void) {
    morse_symbol_t syms[MAX_SYMBOLS]; uint8_t n = 0;
    char mstr[MAX_SYMBOLS+1] = "---";
    if (morse_encode(ABC[sel], syms, &n)) {
        for (uint8_t i = 0; i < n; i++) mstr[i] = syms[i]==MORSE_DOT ? '.' : '-';
        mstr[n] = '\0';
    }
    char l1[24], l2[24];
    snprintf(l1, sizeof(l1), "> %c  %s", ABC[sel], mstr);
    snprintf(l2, sizeof(l2), "MSG:%s", tx_msg);
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "[TX] MorseLink");
    ssd1306_draw_string(&disp, 0, 16, 1, l1);
    ssd1306_draw_string(&disp, 0, 32, 1, l2);
    ssd1306_draw_string(&disp, 0, 48, 1, "A+ CLK=send B=menu");
    ssd1306_show(&disp);
}

static void disp_rx_idle(void) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "[RX] MorseLink");
    ssd1306_draw_string(&disp, 0, 20, 1, "Pressione A para");
    ssd1306_draw_string(&disp, 0, 32, 1, "comecar a gravar");
    ssd1306_draw_string(&disp, 0, 52, 1, "B=menu");
    ssd1306_show(&disp);
}

static void disp_rx_rec(void) {
    char l1[24];
    snprintf(l1, sizeof(l1), ">>> %s", audio_current_symbols());
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "[RX] Gravando...");
    ssd1306_draw_string(&disp, 0, 16, 1, l1);
    ssd1306_draw_string(&disp, 0, 32, 1, rx_history);
    ssd1306_draw_string(&disp, 0, 52, 1, "CLK=parar");
    ssd1306_show(&disp);
}

static void disp_rx_result(void) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "[RX] Resultado:");
    ssd1306_draw_string(&disp, 0, 20, 1, rx_history[0] ? rx_history : "(vazio)");
    ssd1306_draw_string(&disp, 0, 48, 1, "A=gravar  B=menu");
    ssd1306_show(&disp);
}

// Buzzer
static uint pwm_slice, pwm_chan;

static void buzz(bool on) {
    pwm_set_chan_level(pwm_slice, pwm_chan, on ? 500 : 0);
}

static void play(const char *s) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "[TX] Enviando...");
    ssd1306_draw_string(&disp, 0, 24, 1, s);
    ssd1306_show(&disp);
    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') { sleep_ms(WORD_MS - CHAR_MS); continue; }
        morse_symbol_t syms[MAX_SYMBOLS]; uint8_t n = 0;
        if (!morse_encode(s[i], syms, &n)) continue;
        for (uint8_t j = 0; j < n; j++) {
            buzz(true);
            sleep_ms(syms[j]==MORSE_DOT ? DOT_MS : DASH_MS);
            buzz(false);
            if (j < n-1) sleep_ms(GAP_MS);
        }
        sleep_ms(CHAR_MS);
    }
}

// RX helpers
static void rx_decode_and_flush(void) {
    const char *s = audio_current_symbols();
    uint8_t len = (uint8_t)strlen(s);
    if (len == 0) return;
    morse_symbol_t syms[MAX_SYMBOLS];
    for (uint8_t i = 0; i < len; i++)
        syms[i] = s[i] == '.' ? MORSE_DOT : MORSE_DASH;
    morse_result_t r = morse_decode(syms, len);
    if (r.valid) {
        size_t l = strlen(rx_history);
        if (l < MSG_MAX) { rx_history[l] = r.character; rx_history[l+1] = '\0'; }
    }
    audio_clear_symbols();
}

static void go_menu(int *cursor) {
    screen = SCREEN_MENU; *cursor = 0;
    memset(tx_msg,     0, sizeof(tx_msg));
    memset(rx_history, 0, sizeof(rx_history));
    memset(rx_current, 0, sizeof(rx_current));
    audio_reset();
    ev_a = ev_b = ev_clk = false;
    disp_menu(0);
}

// Main
int main(void) {
    stdio_init_all();

    i2c_init(i2c1, 400000);
    gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_pull_up(SDA);
    gpio_set_function(SCL, GPIO_FUNC_I2C); gpio_pull_up(SCL);
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);

    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(BUZZER);
    pwm_chan  = pwm_gpio_to_channel(BUZZER);
    pwm_set_clkdiv(pwm_slice, 64.0f);
    pwm_set_wrap(pwm_slice, 1953125 / FREQ_HZ - 1);
    pwm_set_enabled(pwm_slice, true);

    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);
    audio_init();

    uint btn_pins[] = {BTN_A, BTN_B, JOY_CLK};
    for (int i = 0; i < 3; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
        gpio_set_irq_enabled_with_callback(btn_pins[i], GPIO_IRQ_EDGE_FALL,
                                           true, &gpio_irq_handler);
    }

    morse_decoder_init();

    int  menu_cursor    = 0;
    bool joy_x_centered = true;
    disp_menu(0);

    while (true) {

        // MENU
        if (screen == SCREEN_MENU) {
            int dx = joy_axis(0);
            if (dx != 0 && joy_x_centered) {
                menu_cursor = (menu_cursor + 1) % 2;
                disp_menu(menu_cursor);
                joy_x_centered = false;
            }
            if (dx == 0) joy_x_centered = true;
            if (ev_a) {
                ev_a = false;
                if (menu_cursor == 0) { screen = SCREEN_TX;      disp_tx();      }
                else                  { screen = SCREEN_RX_IDLE; disp_rx_idle(); }
            }
            ev_b = ev_clk = false;
        }

        // TX
        else if (screen == SCREEN_TX) {
            int dy = joy_axis(1);
            if (dy != 0) { sel = (sel + dy + ABC_LEN) % ABC_LEN; disp_tx(); sleep_ms(180); }
            if (ev_a)  { ev_a  = false; size_t l = strlen(tx_msg); if (l < MSG_MAX) { tx_msg[l] = ABC[sel]; tx_msg[l+1] = '\0'; } disp_tx(); }
            if (ev_clk){ ev_clk= false; if (strlen(tx_msg) > 0) { play(tx_msg); memset(tx_msg, 0, sizeof(tx_msg)); } disp_tx(); }
            if (ev_b)  { ev_b  = false; go_menu(&menu_cursor); }
        }

        // RX IDLE
        else if (screen == SCREEN_RX_IDLE) {
            if (ev_a) {
                ev_a = false;
                memset(rx_history, 0, sizeof(rx_history));
                audio_reset();
                screen = SCREEN_RX_REC;
                disp_rx_rec();
            }
            if (ev_b) { ev_b = false; go_menu(&menu_cursor); }
            ev_clk = false;
        }

        // RX GRAVANDO
        else if (screen == SCREEN_RX_REC) {
            audio_event_t aev = audio_process();

            if (aev == AUDIO_SYMBOL) disp_rx_rec();

            if (aev == AUDIO_CHAR_READY) {
                rx_decode_and_flush();
                disp_rx_rec();
            }

            if (aev == AUDIO_WORD_END) {
                rx_decode_and_flush();
                size_t l = strlen(rx_history);
                if (l > 0 && l < MSG_MAX && rx_history[l-1] != ' ')
                    { rx_history[l] = ' '; rx_history[l+1] = '\0'; }
                disp_rx_rec();
            }

            if (ev_clk) {
                ev_clk = false;
                rx_decode_and_flush();
                // Remove espaço trailing ao parar
                size_t l = strlen(rx_history);
                if (l > 0 && rx_history[l-1] == ' ') rx_history[l-1] = '\0';
                screen = SCREEN_RX_RESULT;
                disp_rx_result();
            }
            ev_a = ev_b = false;
        }

        // RX RESULTADO
        else if (screen == SCREEN_RX_RESULT) {
            if (ev_a) {
                ev_a = false;
                memset(rx_history, 0, sizeof(rx_history));
                audio_reset();
                screen = SCREEN_RX_REC;
                disp_rx_rec();
            }
            if (ev_b) { ev_b = false; go_menu(&menu_cursor); }
            ev_clk = false;
        }

        sleep_ms(5);
    }
}