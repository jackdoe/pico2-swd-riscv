#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define LED_PIN 25
#define PATTERN_ADDR ((volatile uint32_t *)0x20010000)
#define PATTERN_SIZE 16

static uint32_t fibonacci(uint32_t n) {
    uint32_t a = 0, b = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t t = a + b;
        a = b;
        b = t;
    }
    return a;
}

static void fill_pattern(void) {
    for (uint32_t i = 0; i < PATTERN_SIZE; i++)
        PATTERN_ADDR[i] = fibonacci(i);
}

int main() {
    stdio_init_all();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    fill_pattern();

    uint32_t counter = 0;

    for (;;) {
        gpio_put(LED_PIN, counter & 1);
        PATTERN_ADDR[PATTERN_SIZE] = counter;
        PATTERN_ADDR[PATTERN_SIZE + 1] = fibonacci(counter % 32);
        counter++;
        sleep_ms(500);
    }
}
