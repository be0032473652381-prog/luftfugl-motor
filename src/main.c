#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "motor.h"
#ifdef LUFTFUGL_DEBUG
#include "debug.h"
#endif

static bool on_tick(struct repeating_timer *timer)
{
    (void)timer;
    encoder_tick();
    controller_tick();
    return true;
}

int main(void)
{
    bool watchdog_reset = watchdog_caused_reboot();
    struct repeating_timer timer;
    console_init();
    if (watchdog_reset) console_watchdog_reset();
    motor_init();
#ifdef LUFTFUGL_DEBUG
    dbg_init();
#endif
    encoder_init();
    controller_init();
#ifdef LUFTFUGL_DEBUG
    dbg_restore_mode();
#endif
    motor_enable();
    watchdog_enable(100, true);
    add_repeating_timer_us(-1000, on_tick, NULL, &timer);
    for (;;) {
        console_poll();
        console_drain_events();
#ifdef LUFTFUGL_DEBUG
        dbg_poll();
#endif
        tight_loop_contents();
    }
}
