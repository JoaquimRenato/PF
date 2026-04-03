#include "morse_decoder.h"
#include <string.h>

#define TREE_SIZE 128

static char morse_tree[TREE_SIZE];

static const struct { const char *seq; char ch; } TABLE[] = {
    {".-",   'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..",  'D'},
    {".",    'E'}, {"..-.", 'F'}, {"--.",  'G'}, {"....", 'H'},
    {"..",   'I'}, {".---", 'J'}, {"-.-",  'K'}, {".-..", 'L'},
    {"--",   'M'}, {"-.",   'N'}, {"---",  'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.",  'R'}, {"...",  'S'}, {"-",    'T'},
    {"..-",  'U'}, {"...-", 'V'}, {".--",  'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'},
    {".----",'1'}, {"..---",'2'}, {"...--",'3'}, {"....-",'4'},
    {".....",'5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
    {"----.", '9'}, {"-----",'0'},
    {NULL, 0}
};

void morse_decoder_init(void) {
    memset(morse_tree, 0, sizeof(morse_tree));
    for (int i = 0; TABLE[i].seq; i++) {
        int idx = 1;
        for (int s = 0; TABLE[i].seq[s]; s++)
            idx = TABLE[i].seq[s] == '.' ? 2*idx : 2*idx+1;
        if (idx < TREE_SIZE)
            morse_tree[idx] = TABLE[i].ch;
    }
}

morse_result_t morse_decode(const morse_symbol_t *symbols, uint8_t len) {
    morse_result_t r = {'\0', 0};
    int idx = 1;
    for (int i = 0; i < len; i++) {
        idx = symbols[i] == MORSE_DOT ? 2*idx : 2*idx+1;
        if (idx >= TREE_SIZE) return r;
    }
    if (morse_tree[idx]) { r.character = morse_tree[idx]; r.valid = 1; }
    return r;
}

uint8_t morse_encode(char c, morse_symbol_t *out, uint8_t *out_len) {
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; TABLE[i].seq; i++) {
        if (TABLE[i].ch == c) {
            *out_len = (uint8_t)strlen(TABLE[i].seq);
            for (uint8_t j = 0; j < *out_len; j++)
                out[j] = TABLE[i].seq[j] == '.' ? MORSE_DOT : MORSE_DASH;
            return 1;
        }
    }
    *out_len = 0;
    return 0;
}