#ifndef MORSE_DECODER_H
#define MORSE_DECODER_H

#include <stdint.h>

#define MAX_SYMBOLS 8   // máximo de símbolos Morse por caractere

typedef enum { MORSE_DOT = 0, MORSE_DASH = 1 } morse_symbol_t;

typedef struct {
    char    character;
    uint8_t valid;
} morse_result_t;

void           morse_decoder_init(void);
morse_result_t morse_decode(const morse_symbol_t *symbols, uint8_t len);
uint8_t        morse_encode(char c, morse_symbol_t *out, uint8_t *out_len);

#endif