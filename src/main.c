#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "motor.h"
#include "led.h"
#include "power_monitor.h"
#ifdef LUFTFUGL_MONITOR
#include "debug.h"
#endif

static volatile bool tick_has_run;

static bool on_tick(struct repeating_timer *timer)
{
    (void)timer;
    encoder_tick();
    controller_tick();
    power_monitor_tick();
    tick_has_run = true;
    return true;
}

int main(void)
{
    bool watchdog_reset = watchdog_caused_reboot();
    struct repeating_timer timer;
    console_init();
    if (watchdog_reset) console_watchdog_reset();
    motor_init();
#ifdef LUFTFUGL_MONITOR
    dbg_init();
#endif
    encoder_init();
    led_init();
    power_monitor_init();
    controller_init();
#ifdef LUFTFUGL_MONITOR
    dbg_enter();
#endif
    motor_enable();
    if (!add_repeating_timer_us(-1000, on_tick, NULL, &timer)) {
        console_timer_alloc_failed();
    } else {
        while (!tick_has_run) tight_loop_contents();
        watchdog_enable(100, true);
    }
    for (;;) {
        console_poll();
        console_drain_events();
        led_update();
#ifdef LUFTFUGL_MONITOR
        dbg_poll();
        dbg_out_drain();
#endif
        tight_loop_contents();
    }
}
