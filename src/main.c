#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "motor.h"
#include "encoder.h"
#include "controller.h"
#include "console.h"
static bool tick(struct repeating_timer *t){(void)t;encoder_tick();controller_tick();return true;}
int main(void){motor_init();encoder_init();controller_init();console_init();watchdog_enable(100,true);struct repeating_timer timer;add_repeating_timer_ms(-1,&tick,NULL,&timer);for(;;){console_poll();console_drain_events();tight_loop_contents();}}
