# MorseLink 📡

Comunicador Morse bidirecional embarcado na **BitDogLab (RP2040)**.  
Transmite mensagens em código Morse pelo buzzer e decodifica sinais Morse captados pelo microfone, exibindo tudo em tempo real no display OLED.

---

## Demonstração

```
== MorseLink ==        * TRANSMISSOR *       * Gravando... *
> TRANSMISSOR          > S  ...              >> .-
  RECEPTOR             MSG: SOS              SOS
X) Mover  (A) Ok       (A)+  (B)-  (OK)Env  (OK) Parar
```

---

## Funcionalidades

- **Menu inicial** — navegação por joystick, seleção por botão
- **Modo Transmissor** — compõe mensagens char a char e envia pelo buzzer em Morse
- **Modo Receptor** — detecta e decodifica sinais Morse captados pelo microfone
- **Decodificação em tempo real** — cada letra aparece no display assim que o gap de caractere é atingido
- **Log serial via UART** — todos os eventos são registrados a 115200 baud

---

## Hardware

| Componente | Pino | Função |
|---|---|---|
| Display OLED SSD1306 128×64 | GP14 (SDA) / GP15 (SCL) | Interface visual |
| Botão A | GP5 | Adiciona char / Confirma / Inicia gravação |
| Botão B | GP6 | Apaga char / Volta ao menu |
| Joystick — Eixo X | GP26 (ADC0) | Navega no menu |
| Joystick — Eixo Y | GP27 (ADC1) | Percorre o alfabeto no TX |
| Joystick — Click | GP22 | Envia mensagem / Para gravação |
| Buzzer | GP21 (PWM) | Reproduz Morse em áudio |
| Microfone electret | GP28 (ADC2) | Captura sinal sonoro para RX |
| UART | GP0 (TX) / GP1 (RX) | Log serial 115200 baud |

---

## Controles

| Controle | Modo TX | Modo RX | Menu |
|---|---|---|---|
| Joystick X | — | — | Alterna opções |
| Joystick Y | Percorre A-Z, 0-9 | — | — |
| Botão (A) | Adiciona char | Inicia gravação | Confirma |
| Botão (B) | Apaga último char | Volta ao menu | — |
| Botão (OK) | Envia pelo buzzer | Para gravação | — |

---

## Estrutura do Projeto

```
morselink/
├── PF.c               # Arquivo principal — menu, TX, RX, IRQ, buzzer
├── audio.c            # Biblioteca de captura e análise do microfone
├── audio.h
├── morse_decoder.c    # Árvore binária de decodificação Morse (ITU-R M.1677)
├── morse_decoder.h
├── ssd1306.c          # Driver do display OLED (licença MIT)
├── ssd1306.h
├── font.h             # Fonte bitmap 8×5
├── CMakeLists.txt
└── pico_sdk_import.cmake
```

---

## Como compilar e gravar

### Pré-requisitos

- [pico-sdk](https://github.com/raspberrypi/pico-sdk) v2.0+
- CMake 3.13+
- GCC ARM Toolchain (`arm-none-eabi-gcc`)

### Compilação

```bash
mkdir build && cd build
cmake ..
make -j4
```

### Gravar na placa

Segure o botão **BOOTSEL** da BitDogLab, conecte o USB e solte o botão.  
Copie o arquivo gerado:

```bash
cp build/PF.uf2 /media/$USER/RPI-RP2/
```

---

## Calibração do microfone

O receptor usa um microfone electret direto no ADC. Os parâmetros de detecção ficam em `audio.h`:

```c
#define AUDIO_THR_ON   80   // pico mínimo para detectar tom (ligar)
#define AUDIO_THR_OFF  40   // pico mínimo para manter tom ativo (histéresis)
#define AUDIO_DOT_MAX  220  // abaixo = ponto, acima = traço (ms)
#define AUDIO_MIN_TONE  75  // tons menores que isso são descartados (ruído)
#define AUDIO_CHAR_GAP 250  // silêncio que fecha um caractere (ms)
#define AUDIO_WORD_GAP 1000 // silêncio que fecha uma palavra (ms)
```

**Procedimento rápido:**
1. Em silêncio, observe o pico do ADC — `AUDIO_THR_ON` deve ser maior que esse valor
2. Toque um **ponto** isolado e anote a duração
3. Toque um **traço** isolado e anote a duração
4. `AUDIO_DOT_MAX` = (duração_ponto + duração_traço) / 2

---

## Log UART

Conecte um adaptador USB-Serial nos pinos GP0/GP1 (115200 baud, 8N1):

```
[BOOT] MorseLink iniciado
[BOOT] Buzzer: 700 Hz
[BOOT] Threshold mic: ON=80 OFF=40
[MENU] Modo: RECEPTOR
[RX] Gravacao iniciada
[RX] Char: S
[RX] Palavra: SOS
[RX] Gravacao parada | MSG: SOS OI
[TX] Char adicionado: 'O' | MSG: OI
[TX] Enviando: OI
[TX] Enviado.
```

---

## Dependências

| Biblioteca | Autor | Licença |
|---|---|---|
| [pico-sdk](https://github.com/raspberrypi/pico-sdk) | Raspberry Pi Foundation | BSD-3-Clause |
| [pico-ssd1306](https://github.com/daschr/pico-ssd1306) | David Schramm | MIT |

---

## Licença

MIT License — livre para uso educacional e pessoal.

---

## Créditos

Desenvolvido por **Joaquim** como Projeto Final do programa **EmbarcaTech Expansão** (2025).  
