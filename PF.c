#include <stdio.h>
#include "pico/stdlib.h"
#include "ssd1306.h"

#define JOY_Y_PIN 27
#define BTNA 5
#define BTNB 6



int main()
{
    stdio_init_all();

    //BOTÕES A E B
    gpio_init(BTNA);
    gpio_set_dir(BTNA, GPIO_IN);
    gpio_pull_up(BTNA);

    gpio_init(BTNB);
    gpio_set_dir(BTNB, GPIO_IN);
    gpio_pull_up(BTNB);


    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
//vlw zion