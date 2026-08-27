#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "motor.h"
#include "led.h"
#include "buzzer.h"
#include "co2.h"
#include "power_monitor.h"
#ifdef LUFTFUGL_MONITOR
#include "debug.h"
#endif

static volatile bool tick_has_run;

static uint16_t adc_distance(uint16_t a, uint16_t b)
{
    return a > b ? a - b : b - a;
}

static void coordinate_sdc41_warmup(void)
{
    bool pending = co2_startup_pending();
    bool warming = co2_warming_up();

    controller_set_station1_lock(pending || warming);
    if (!pending && !warming) return;

    uint16_t current = encoder_average();
    uint16_t station1 = encoder_nominal(POS_MIN);
    bool at_station1 = adc_distance(current, station1) <= CFG_POS_WINDOW;
    if (controller_state() != ST_IDLE) return;

    if (!at_station1) {
        (void)controller_request(REQ_HOME, POS_MIN);
        return;
    }
    if (pending) (void)co2_begin_initial_warmup();
}

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
    led_power_init();
    console_init();
    if (watchdog_reset) console_watchdog_reset();
    motor_init();
    buzzer_init();
#ifdef LUFTFUGL_MONITOR
    dbg_init();
#endif
    encoder_init();
    led_init();
    power_monitor_init();
    co2_init();
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
        coordinate_sdc41_warmup();
        led_update();
        buzzer_tick();
        co2_tick();
#ifdef LUFTFUGL_MONITOR
        dbg_poll();
        dbg_out_drain();
#endif
        tight_loop_contents();
    }
}
