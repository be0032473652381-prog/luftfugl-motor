#include "pico/stdlib.h"

int main(void)
{
    const uint pin_22 = 22;
    const uint pin_25 = 25;

    gpio_init(pin_22);
    gpio_set_dir(pin_22, GPIO_OUT);
    gpio_init(pin_25);
    gpio_set_dir(pin_25, GPIO_OUT);
    for (;;) {
        gpio_put(pin_22, true);
        gpio_put(pin_25, true);
        sleep_ms(500);
        gpio_put(pin_22, false);
        gpio_put(pin_25, false);
        sleep_ms(500);
    }
}
