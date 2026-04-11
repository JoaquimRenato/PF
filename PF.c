/*
 * MorseLink — Projeto Final EmbarcaTech Expansão
 * Placa: BitDogLab (RP2040)
 *
 * Modos de operação:
 *   TRANSMISSOR: seleciona chars com joystick Y, confirma com (A),
 *                apaga com (B), envia pelo buzzer com (OK).
 *   RECEPTOR:    grava sinal do microfone com (A), para com (OK),
 *                volta ao menu com (B).
 *
 * Controles resumidos:
 *   Joystick X   Navega no menu
 *   Joystick Y   Navega no alfabeto (modo TX)
 *   (A)          Confirma / Adiciona char / Inicia gravação
 *   (B)          Apaga char / Volta ao menu
 *   (OK)         Envia mensagem / Para gravação
 */

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

/* Pinos da BitDogLab */
#define BTN_A    5    /* Botão A */
#define BTN_B    6    /* Botão B */
#define JOY_CLK  22   /* Botão do joystick (OK) */
#define JOY_X    26   /* Eixo X do joystick — ADC0 */
#define JOY_Y    27   /* Eixo Y do joystick — ADC1 */
#define BUZZER   21   /* Buzzer PWM */
#define SDA      14   /* Display I2C — SDA */
#define SCL      15   /* Display I2C — SCL */

/* Temporização do buzzer no modo TX (em ms) */
#define DOT_MS    80   /* Duração de um ponto */
#define DASH_MS  240   /* Duração de um traço (3x ponto) */
#define GAP_MS    80   /* Pausa entre símbolos do mesmo char */
#define CHAR_MS  240   /* Pausa entre caracteres */
#define WORD_MS  560   /* Pausa entre palavras */

/* Configuração geral */
#define FREQ_HZ  700   /* Frequência do tom do buzzer */
#define MSG_MAX   32   /* Tamanho máximo da mensagem TX */
#define DBNC_MS  200   /* Tempo de debounce dos botões (ms) */

/* Alfabeto disponível no modo TX — inclui espaço no final para inserir pausas */
static const char ABC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
#define ABC_LEN ((int)(sizeof(ABC) - 1))

/* Identificadores das telas da aplicação */
typedef enum {
    SCREEN_MENU,       /* Menu inicial */
    SCREEN_TX,         /* Modo transmissor */
    SCREEN_RX_IDLE,    /* Receptor: aguardando início */
    SCREEN_RX_REC,     /* Receptor: gravando */
    SCREEN_RX_RESULT   /* Receptor: exibindo resultado */
} screen_t;

static screen_t screen = SCREEN_MENU;

/* Periférico de display */
static ssd1306_t disp;

/* Estado do modo TX */
static char tx_msg[MSG_MAX + 1]       = "";  /* Mensagem sendo composta */
static int  sel                       = 0;   /* Índice do char selecionado em ABC */

/* Estado do modo RX */
static char rx_history[MSG_MAX + 1]   = "";  /* Mensagem decodificada acumulada */
static char rx_current[MAX_SYMBOLS+1] = "";  /* Buffer auxiliar (reservado) */

/* Flags de eventos gerados pelas interrupções dos botões.
 * Declaradas volatile para garantir que o compilador sempre
 * releia o valor da memória, mesmo em otimizações. */
static volatile bool ev_a   = false;
static volatile bool ev_b   = false;
static volatile bool ev_clk = false;

/*
 * Handler único de interrupção para todos os botões.
 * Identifica qual pino gerou a borda de descida e seta
 * a flag correspondente, com debounce por tempo.
 */
static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;

    static uint32_t ta = 0, tb = 0, tclk = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (gpio == BTN_A   && now - ta   >= DBNC_MS) { ta   = now; ev_a   = true; }
    if (gpio == BTN_B   && now - tb   >= DBNC_MS) { tb   = now; ev_b   = true; }
    if (gpio == JOY_CLK && now - tclk >= DBNC_MS) { tclk = now; ev_clk = true; }
}

/*
 * Lê o eixo analógico do joystick no canal ADC informado.
 * Retorna +1 (cima/direita), -1 (baixo/esquerda) ou 0 (centro).
 */
static int joy_axis(uint ch) {
    adc_select_input(ch);
    uint16_t v = adc_read();
    if (v > 2400) return +1;
    if (v < 1600) return -1;
    return 0;
}

/* Tela do menu principal */
static void disp_menu(int cur) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "===== MorseLink =====");
    ssd1306_draw_string(&disp, 0, 20, 1, cur == 0 ? "> TRANSMISSOR" : "  TRANSMISSOR");
    ssd1306_draw_string(&disp, 0, 34, 1, cur == 1 ? "> RECEPTOR"    : "  RECEPTOR");
    ssd1306_draw_string(&disp, 0, 52, 1, "X) Mover   (A) Ok");
    ssd1306_show(&disp);
}

/* Tela do modo transmissor.
 * Linha 1: char selecionado e seu código Morse.
 * Linha 2: mensagem composta até agora.
 * Linha 3: legenda dos controles. */
static void disp_tx(void) {
    /* Converte o char selecionado para sua sequência Morse */
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
    snprintf(l2, sizeof(l2), "MSG: %s", tx_msg);

    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "* TRANSMISSOR *");
    ssd1306_draw_string(&disp, 0, 14, 1, l1);
    ssd1306_draw_string(&disp, 0, 28, 1, l2);
    ssd1306_draw_string(&disp, 0, 48, 1, "(A)+  (B)-  (OK)Env");
    ssd1306_show(&disp);
}

/* Tela de espera do receptor */
static void disp_rx_idle(void) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "* RECEPTOR *");
    ssd1306_draw_string(&disp, 0, 18, 1, "Pronto para");
    ssd1306_draw_string(&disp, 0, 30, 1, "receber sinal.");
    ssd1306_draw_string(&disp, 0, 48, 1, "(A) Gravar  (B) Menu");
    ssd1306_show(&disp);
}

/* Tela de gravação do receptor.
 * Linha 1: símbolos Morse do char atual chegando em tempo real.
 * Linha 2: mensagem decodificada acumulada. */
static void disp_rx_rec(void) {
    char l1[24];
    snprintf(l1, sizeof(l1), ">> %s", audio_current_symbols());

    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "* Gravando... *");
    ssd1306_draw_string(&disp, 0, 14, 1, l1);
    ssd1306_draw_string(&disp, 0, 30, 1, rx_history);
    ssd1306_draw_string(&disp, 0, 50, 1, "(OK) Parar");
    ssd1306_show(&disp);
}

/* Tela de resultado do receptor */
static void disp_rx_result(void) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "* Resultado *");
    ssd1306_draw_string(&disp, 0, 18, 1, rx_history[0] ? rx_history : "(vazio)");
    ssd1306_draw_string(&disp, 0, 48, 1, "(A) Novo  (B) Menu");
    ssd1306_show(&disp);
}

/* Slice e canal PWM do buzzer */
static uint pwm_slice, pwm_chan;

/* Liga ou desliga o tom do buzzer */
static void buzz(bool on) {
    pwm_set_chan_level(pwm_slice, pwm_chan, on ? 500 : 0);
}

/*
 * Reproduz uma string ASCII em código Morse pelo buzzer.
 * Espaços são tratados como pausa entre palavras.
 */
static void play(const char *s) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0,  0, 1, "* Enviando... *");
    ssd1306_draw_string(&disp, 0, 24, 1, s);
    ssd1306_show(&disp);

    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') {
            /* Pausa de palavra: 7 unidades no total.
             * A pausa de char (3 unidades) já foi aplicada antes,
             * então adicionamos apenas as 4 unidades restantes. */
            sleep_ms(WORD_MS - CHAR_MS);
            continue;
        }

        morse_symbol_t syms[MAX_SYMBOLS];
        uint8_t n = 0;
        if (!morse_encode(s[i], syms, &n)) continue;

        for (uint8_t j = 0; j < n; j++) {
            buzz(true);
            sleep_ms(syms[j] == MORSE_DOT ? DOT_MS : DASH_MS);
            buzz(false);
            if (j < n - 1) sleep_ms(GAP_MS); /* Pausa entre símbolos */
        }
        sleep_ms(CHAR_MS); /* Pausa entre caracteres */
    }
}

/*
 * Decodifica os símbolos Morse acumulados no buffer de áudio,
 * adiciona o caractere resultante ao histórico RX e limpa o buffer.
 * Chamada assim que AUDIO_CHAR_READY ou AUDIO_WORD_END é detectado,
 * garantindo que cada letra apareça no display imediatamente.
 */
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
        if (l < MSG_MAX) {
            rx_history[l]     = r.character;
            rx_history[l + 1] = '\0';
        }
    }

    audio_clear_symbols();
}

/*
 * Retorna ao menu principal, limpando todos os estados
 * e consumindo quaisquer eventos pendentes.
 */
static void go_menu(int *cursor) {
    screen  = SCREEN_MENU;
    *cursor = 0;
    memset(tx_msg,     0, sizeof(tx_msg));
    memset(rx_history, 0, sizeof(rx_history));
    memset(rx_current, 0, sizeof(rx_current));
    audio_reset();
    ev_a = ev_b = ev_clk = false;
    disp_menu(0);
}

int main(void) {
    /* Inicializa UART para log serial (115200 baud, pinos padrão GP0/GP1).
     * Não trava o boot pois USB stdio está desabilitado no CMakeLists. */
    stdio_init_all();

    /* Display OLED via I2C1 */
    i2c_init(i2c1, 400000);
    gpio_set_function(SDA, GPIO_FUNC_I2C); gpio_pull_up(SDA);
    gpio_set_function(SCL, GPIO_FUNC_I2C); gpio_pull_up(SCL);
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);

    /* Buzzer via PWM
     * Divisor de clock 64: 125MHz / 64 = ~1.95MHz
     * wrap = 1953125 / FREQ_HZ - 1  →  tom de 700 Hz */
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(BUZZER);
    pwm_chan  = pwm_gpio_to_channel(BUZZER);
    pwm_set_clkdiv(pwm_slice, 64.0f);
    pwm_set_wrap(pwm_slice, 1953125 / FREQ_HZ - 1);
    pwm_set_enabled(pwm_slice, true);

    /* ADC para joystick (X e Y) e microfone (inicializado pela audio_init) */
    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);
    audio_init();

    /* Botões: pull-up interno + IRQ por borda de descida */
    uint btn_pins[] = {BTN_A, BTN_B, JOY_CLK};
    for (int i = 0; i < 3; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
        gpio_set_irq_enabled_with_callback(
            btn_pins[i], GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    }

    morse_decoder_init();

    /* Log de inicialização */
    printf("[BOOT] MorseLink iniciado\n");
    printf("[BOOT] Buzzer: %d Hz\n", FREQ_HZ);
    printf("[BOOT] Threshold mic: ON=%d OFF=%d\n", AUDIO_THR_ON, AUDIO_THR_OFF);
    printf("[BOOT] Morse: dot_max=%dms char_gap=%dms word_gap=%dms\n",
           AUDIO_DOT_MAX, AUDIO_CHAR_GAP, AUDIO_WORD_GAP);

    int  menu_cursor    = 0;
    bool joy_x_centered = true;
    disp_menu(0);

    while (true) {

        /* MENU: joystick X alterna entre as opções; (A) confirma */
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
                if (menu_cursor == 0) {
                    screen = SCREEN_TX;
                    printf("[MENU] Modo: TRANSMISSOR\n");
                    disp_tx();
                } else {
                    screen = SCREEN_RX_IDLE;
                    printf("[MENU] Modo: RECEPTOR\n");
                    disp_rx_idle();
                }
            }
            ev_b = ev_clk = false;
        }

        /* TX: joystick Y navega; (A) adiciona char; (B) apaga; (OK) envia */
        else if (screen == SCREEN_TX) {
            int dy = joy_axis(1);
            if (dy != 0) {
                sel = (sel + dy + ABC_LEN) % ABC_LEN;
                disp_tx();
                sleep_ms(180);
            }

            if (ev_a) {
                ev_a = false;
                size_t l = strlen(tx_msg);
                if (l < MSG_MAX) {
                    tx_msg[l] = ABC[sel]; tx_msg[l + 1] = '\0';
                    printf("[TX] Char adicionado: '%c' | MSG: %s\n", ABC[sel], tx_msg);
                }
                disp_tx();
            }

            if (ev_clk) {
                ev_clk = false;
                if (strlen(tx_msg) > 0) {
                    printf("[TX] Enviando: %s\n", tx_msg);
                    play(tx_msg);
                    memset(tx_msg, 0, sizeof(tx_msg));
                    printf("[TX] Enviado.\n");
                }
                disp_tx();
            }

            if (ev_b) { ev_b = false; go_menu(&menu_cursor); }
        }

        /* RX IDLE: aguarda (A) para iniciar gravação ou (B) para voltar */
        else if (screen == SCREEN_RX_IDLE) {
            if (ev_a) {
                ev_a = false;
                memset(rx_history, 0, sizeof(rx_history));
                audio_reset();
                screen = SCREEN_RX_REC;
                printf("[RX] Gravacao iniciada\n");
                disp_rx_rec();
            }
            if (ev_b) { ev_b = false; go_menu(&menu_cursor); }
            ev_clk = false;
        }

        /* RX GRAVANDO: processa áudio continuamente.
         * AUDIO_SYMBOL     → novo símbolo detectado, atualiza display.
         * AUDIO_CHAR_READY → char completo, decodifica imediatamente.
         * AUDIO_WORD_END   → fim de palavra, decodifica e insere espaço.
         * (OK)             → para a gravação e exibe o resultado. */
        else if (screen == SCREEN_RX_REC) {
            audio_event_t aev = audio_process();

            /* Atualiza o display a cada novo símbolo para mostrar os pontos/traços
             * em tempo real, mas limita a frequência usando um contador simples.
             * Isso evita que o I2C bloqueie o loop a cada chamada de audio_process(). */
            static uint8_t sym_refresh = 0;
            if (aev == AUDIO_SYMBOL) {
                if (++sym_refresh >= 1) { sym_refresh = 0; disp_rx_rec(); }
            }

            if (aev == AUDIO_CHAR_READY) {
                rx_decode_and_flush();
                printf("[RX] Char: %s\n", rx_history);
                disp_rx_rec();
            }

            if (aev == AUDIO_WORD_END) {
                rx_decode_and_flush();
                size_t l = strlen(rx_history);
                if (l > 0 && l < MSG_MAX && rx_history[l - 1] != ' ')
                    { rx_history[l] = ' '; rx_history[l + 1] = '\0'; }
                printf("[RX] Palavra: %s\n", rx_history);
                disp_rx_rec();
            }

            if (ev_clk) {
                ev_clk = false;
                rx_decode_and_flush();
                size_t l = strlen(rx_history);
                if (l > 0 && rx_history[l - 1] == ' ') rx_history[l - 1] = '\0';
                printf("[RX] Gravacao parada | MSG: %s\n", rx_history);
                screen = SCREEN_RX_RESULT;
                disp_rx_result();
            }
            ev_a = ev_b = false;
        }

        /* RX RESULTADO: exibe a mensagem recebida.
         * (A) inicia nova gravação; (B) volta ao menu. */
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