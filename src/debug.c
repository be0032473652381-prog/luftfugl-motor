#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/time.h"
#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "motor.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/pwm.h"
#include "hardware/watchdog.h"

typedef enum { MENU_ROOT, MENU_STATUS, MENU_ADC, MENU_MOTOR, MENU_CAL, MENU_CFG, MENU_FAULT, MENU_TEST, MENU_BENCH, MENU_SIM } menu_t;
typedef enum { PROMPT_NONE, PROMPT_ARM, PROMPT_COUPLED, PROMPT_CLEAR, PROMPT_RATE, PROMPT_DUTY, PROMPT_DURATION, PROMPT_CFG_KEY, PROMPT_CFG_VALUE, PROMPT_SIM_VALUE, PROMPT_SIM_BAND, PROMPT_SIM_FROM, PROMPT_SIM_TO, PROMPT_SIM_MS, PROMPT_SIM_DRIFT } prompt_t;
typedef enum { ACT_NONE, ACT_STATIC, ACT_FINDMIN_BASE, ACT_FINDMIN_PULSE, ACT_CAL_POS_WAIT, ACT_CAL_POS_SAMPLE, ACT_CAL_STEP, ACT_CAL_TRAVEL, ACT_CAL_OVER, ACT_SELFTEST, ACT_BENCH_PINS, ACT_GPIO_WALK, ACT_TICK_HEALTH, ACT_SIM_TRAVEL, ACT_SIM_PARK, ACT_SIM_DRIFT, ACT_SIM_SWEEP } action_t;
static bool active, armed, coupled, streaming, monitoring, capturing, echo_enabled;
static menu_t menu;
static prompt_t prompt;
static char input[CONSOLE_LINE_MAX + 1];
static char cfg_key[CONSOLE_LINE_MAX + 1];
static uint8_t input_len;
static uint8_t pulse_duty;
static uint16_t pulse_ms, stream_hz;
static uint32_t arm_deadline, coupled_deadline, next_stream;
static uint16_t capture_min, capture_max;
static uint32_t capture_samples;
static action_t action;
static uint8_t action_stage, action_duty;
static uint32_t action_started, action_deadline, action_sum, action_count, action_worst;
static uint16_t action_min, action_max, action_start_adc, noise_floor;
static uint16_t action_means[6];
static uint8_t action_source, action_saved_duty;
static position_t sim_from, sim_to;
static uint16_t sim_band_ms;
static uint32_t bench_tick_start;
static uint16_t self_adc[DEBUG_SELFTEST_ADC_SAMPLES];
static uint32_t self_tick_start;

static bool self_all_full;
static bool action_pass;
static position_t action_last_pos;
static void action_poll(uint32_t now);
static void line(const char *s);
static void dbg_help(void);

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }
static void dbg_help(void){switch(menu){case MENU_ROOT:line("HELP: choose 1-9; w shows currently runnable tests; x exits debug.");break;case MENU_STATUS:line("HELP: inspect state or stream telemetry; streaming is motion-free.");break;case MENU_ADC:line("HELP: read, monitor, capture, or inspect bands; no motion required.");break;case MENU_MOTOR:line("HELP: manual outputs require typing UNCOUPLED; simulation must be off.");break;case MENU_CAL:line("HELP: position sampling is motion-free; motion calibration requires COUPLED.");break;case MENU_CFG:line("HELP: overrides are volatile RAM values and reset on debug exit.");break;case MENU_FAULT:line("HELP: inspect faults/history/counters; clearing a fault still requires home.");break;case MENU_TEST:line("HELP: static test is motion-free; motion test requires COUPLED and known position.");break;case MENU_BENCH:line("HELP: all tests are bare-board safe; GPIO walk additionally requires UNCOUPLED.");break;case MENU_SIM:line("HELP: enable simulation first; motor inhibit has no override; any key aborts a sequence.");break;}}
static const char *state_text(sys_state_t s) { static const char *const n[]={"BOOT","IDLE","MOVING","APPROACH","HOMING","RECOVER","FAULT","DEBUG"}; return n[s]; }
static const char *dir_text(direction_t d) { return d==DIR_FWD?"FWD":d==DIR_REV?"REV":"STP"; }
static void line(const char *s) { console_debug_line(s); }
static void band_limits(position_t p,uint16_t *lo,uint16_t *hi){static const uint16_t maxv[5]={BAND_P1_MAX,BAND_P2_MAX,BAND_P3_MAX,BAND_P4_MAX,BAND_P5_MAX};if(p<1||p>5){*lo=CFG_BAND_P5_MAX+1u;*hi=ADC_MAX_VALUE;return;}uint16_t active[5]={CFG_BAND_P1_MAX,CFG_BAND_P2_MAX,CFG_BAND_P3_MAX,CFG_BAND_P4_MAX,CFG_BAND_P5_MAX};*lo=p==1?0u:(uint16_t)(active[p-2u]+1u);*hi=active[p-1u];(void)maxv;}
static bool post(dbg_op_t op, direction_t dir, uint8_t duty, uint16_t ms, bool flag) { dbg_request_t r={op,dir,duty,ms,flag}; return controller_debug_request(&r); }
static void activity(void) { uint32_t n=ms_now(); if(armed)arm_deadline=n+DEBUG_INTERLOCK_TIMEOUT_MS;if(coupled)coupled_deadline=n+DEBUG_INTERLOCK_TIMEOUT_MS; }

void dbg_init(void) { cfg_reset(); active=armed=coupled=streaming=monitoring=capturing=false; menu=MENU_ROOT; prompt=PROMPT_NONE; pulse_duty=DUTY_CREEP; pulse_ms=DEBUG_PULSE_DEFAULT_MS; stream_hz=DEBUG_STREAM_DEFAULT_HZ; action=ACT_NONE; echo_enabled=false; }
void dbg_enter(void) { if(controller_state()==ST_FAULT){line("debug unavailable while faulted");return;} echo_enabled=true; active=true; menu=MENU_ROOT; dbg_render(); }
void dbg_exit(void) { echo_enabled=false; (void)post(DBG_OP_EXIT,DIR_STOP,0,0,false); armed=coupled=false; streaming=monitoring=capturing=false; action=ACT_NONE; cfg_reset(); active=false; line("debug exited"); }
bool dbg_active(void) { return active; }
void dbg_render_header(void) { char b[DEBUG_HEADER_BUFFER_SIZE];dbg_counters_t c;uint16_t lo,hi,v=encoder_average();position_t p=encoder_instant();band_limits(p,&lo,&hi);uint16_t edge=(uint16_t)((v-lo)<(hi-v)?v-lo:hi-v);uint16_t percent=(uint16_t)(edge*DEBUG_PERCENT_SCALE/(hi-lo+1u));controller_counters_get(&c);snprintf(b,sizeof b,"=== luftfugl debug 1.0 ============================\r\n state %s   pos %u   adc %u (band %u, margin %u%%%%)\r\n armed %s     coupled %s     sim %s     faults %lu\r\n===================================================",state_text(controller_state()),controller_position(),v,p,percent,armed?"YES":"NO",coupled?"YES":"NO",encoder_sim_active()?"\033[7mON\033[0m":"OFF",(unsigned long)c.faults);line(b);}
void dbg_render(void) { char b[DEBUG_MENU_BUFFER_SIZE];dbg_render_header();switch(menu){
case MENU_ROOT:snprintf(b,sizeof b," 1  status & telemetry .... stream %s\r\n 2  encoder & adc ......... avg %u\r\n 3  motor manual .......... %s\r\n 4  calibration ........... %s\r\n 5  configuration ......... defaults/overrides\r\n 6  faults & history ...... state %s\r\n 7  self-test ............. static ready\r\n 8  bench tests ........... bare-board ready\r\n 9  simulation ............ %s\r\n w  what can run now\r\n x  exit",streaming?"ON":"OFF",encoder_average(),armed?"ARMED":"needs UNCOUPLED",coupled?"COUPLED":"needs COUPLED",state_text(controller_state()),encoder_sim_active()?"ON":"OFF");line(b);break;
case MENU_STATUS:snprintf(b,sizeof b," s  state dump ............ %s\r\n t  telemetry stream ...... %s\r\n r  stream rate ........... %u Hz\r\n k  tick timing ........... measured\r\n z  reset timing stats\r\n ?  help   w available   q back   x exit",state_text(controller_state()),streaming?"ON":"OFF",stream_hz);line(b);break;
case MENU_ADC:snprintf(b,sizeof b," a  adc reading ........... raw %u avg %u\r\n m  live monitor .......... %s\r\n c  min/max capture ....... %s\r\n b  band table ............ active\r\n e  band margin ........... band %u\r\n ? help   w available   q back   x exit",encoder_raw(),encoder_average(),monitoring?"ON":"OFF",capturing?"ON":"OFF",encoder_instant());line(b);break;
case MENU_MOTOR:snprintf(b,sizeof b," A  arm/disarm ............ %s\r\n f  pulse forward ......... duty %u, %u ms\r\n v  pulse reverse ......... duty %u, %u ms\r\n d  pulse duty ............ %u / %u\r\n t  pulse duration ........ %u ms\r\n b  brake   c coast   s standby   n find minimum",armed?"ARMED":"needs UNCOUPLED",pulse_duty,pulse_ms,pulse_duty,pulse_ms,pulse_duty,PWM_WRAP,pulse_ms);line(b);break;
case MENU_CAL:snprintf(b,sizeof b," p  position readings ..... motion-free\r\n s  step time ............. %s\r\n w  full travel ........... %s\r\n o  overshoot ............. %s\r\n r  calibration report .... session RAM",coupled?"ready":"needs COUPLED",coupled?"ready":"needs COUPLED",coupled?"ready":"needs COUPLED");line(b);break;
case MENU_CFG:snprintf(b,sizeof b," l  list constants ........ DUTY_NORMAL %u\r\n s  set override .......... volatile RAM\r\n d  reset defaults\r\n e  export config.h",CFG_DUTY_NORMAL);line(b);break;
case MENU_FAULT:snprintf(b,sizeof b," f  last fault ............ state %s\r\n h  position history ...... %u entries\r\n c  counters\r\n z  reset counters\r\n k  clear fault ........... home still required",state_text(controller_state()),controller_history_count());line(b);break;
case MENU_TEST:snprintf(b,sizeof b," s  static self-test ...... ready\r\n m  motion self-test ...... %s",coupled?"ready":"needs COUPLED");line(b);break;
case MENU_BENCH:snprintf(b,sizeof b," p  pin state readout ..... %s\r\n g  gpio walk ............. %s\r\n f  pwm report ............ slice %u\r\n t  tick/watchdog ......... ready\r\n r  reset reason .......... uptime %lu ms\r\n o  protocol strings\r\n e  character echo ........ %s",action==ACT_BENCH_PINS?"STREAMING":"5 Hz",armed?"ready":"needs UNCOUPLED",pwm_gpio_to_slice_num(PIN_PWMA),(unsigned long)ms_now(),echo_enabled?"ON":"OFF");line(b);break;
case MENU_SIM:snprintf(b,sizeof b," e  simulation ............ %s\r\n v  adc value ............. %u / %u\r\n b  jump to band .......... current %u\r\n t  travel sequence ....... prompted\r\n p  park between reeds\r\n l  drift past limit\r\n s  sweep boundaries",encoder_sim_active()?"ON, MOTOR INHIBITED":"OFF",encoder_sim_value(),ADC_MAX_VALUE,encoder_instant());line(b);break;
}}
void dbg_abort(void) { action_t stopped=action;if(action==ACT_CAL_OVER)cfg.duty_approach=action_saved_duty; action=ACT_NONE; (void)controller_request(REQ_STOP,0);if(stopped==ACT_GPIO_WALK)(void)post(DBG_OP_GPIO_SET,DIR_STOP,DEBUG_GPIO_OP_ALL_LOW,0,false);else (void)post(DBG_OP_SIM_ENABLE,DIR_STOP,0,0,false); encoder_sim_enable(false); armed=coupled=false; line("ABORTED"); }

static void finish_prompt(void) { input[input_len]='\0'; if(prompt==PROMPT_ARM){if(!strcmp(input,"UNCOUPLED")){if(encoder_sim_active()){line("rejected: disable simulation first");prompt=PROMPT_NONE;input_len=0;return;}armed=true;coupled=false;arm_deadline=ms_now()+DEBUG_INTERLOCK_TIMEOUT_MS;(void)post(DBG_OP_ENTER,DIR_STOP,0,0,false);line("manual drive armed");}else line("arming cancelled");}else if(prompt==PROMPT_COUPLED){if(!strcmp(input,"COUPLED")){if(encoder_sim_active()){line("rejected: disable simulation first");prompt=PROMPT_NONE;input_len=0;return;}coupled=true;armed=false;coupled_deadline=ms_now()+DEBUG_INTERLOCK_TIMEOUT_MS;line("coupled motion confirmed");}else line("coupled confirmation cancelled");}else if(prompt==PROMPT_CLEAR){if(!strcmp(input,"CLEAR")){dbg_request_t r={.op=DBG_OP_FAULT_CLEAR};(void)controller_debug_request(&r);line("fault flag cleared; home required");}else line("fault clear cancelled");}
else if(prompt==PROMPT_RATE){long v=strtol(input,NULL,10);dbg_stream_set_rate((uint16_t)v);}
else if(prompt==PROMPT_DUTY){long v=strtol(input,NULL,10);if(v>=0&&v<=PWM_WRAP){pulse_duty=(uint8_t)v;line(v<DUTY_MIN?"pulse duty updated; below DUTY_MIN":"pulse duty updated");}else line("pulse duty must be 0..255");}
else if(prompt==PROMPT_DURATION){long v=strtol(input,NULL,10);if(v>=(long)DEBUG_PULSE_MIN_MS&&v<=(long)DEBUG_PULSE_MAX_MS){pulse_ms=(uint16_t)v;line("pulse duration updated");}else line("pulse duration must be 10..2000 ms");}
else if(prompt==PROMPT_CFG_KEY){strncpy(cfg_key,input,sizeof cfg_key);cfg_key[sizeof cfg_key-1]=0;line("value (integer, unit and range depend on key): ");prompt=PROMPT_CFG_VALUE;input_len=0;return;}
else if(prompt==PROMPT_CFG_VALUE){long v=strtol(input,NULL,10);line(dbg_cfg_set(cfg_key,v)?"configuration override applied":"configuration override rejected");}
else if(prompt==PROMPT_SIM_VALUE){long v=strtol(input,NULL,10);if(v>=0&&v<=(long)ADC_MAX_VALUE)dbg_sim_set_value((uint16_t)v);else line("rejected: adc must be 0-4095 counts");}
else if(prompt==PROMPT_SIM_BAND){long v=strtol(input,NULL,10);if(v>=0&&v<=POS_MAX)dbg_sim_set_band((position_t)v);else line("rejected: band must be 0-5");}
else if(prompt==PROMPT_SIM_FROM){long v=strtol(input,NULL,10);if(v>=POS_MIN&&v<=POS_MAX){sim_from=(position_t)v;line("to band (1-5): ");prompt=PROMPT_SIM_TO;input_len=0;return;}else line("rejected: from band must be 1-5");}
else if(prompt==PROMPT_SIM_TO){long v=strtol(input,NULL,10);if(v>=POS_MIN&&v<=POS_MAX){sim_to=(position_t)v;line("milliseconds per band (12-65535 ms): ");prompt=PROMPT_SIM_MS;input_len=0;return;}else line("rejected: to band must be 1-5");}
else if(prompt==PROMPT_SIM_MS){long v=strtol(input,NULL,10);if(v>=DEBUG_SIM_MIN_BAND_MS&&v<=UINT16_MAX)dbg_sim_travel(sim_from,sim_to,(uint16_t)v);else line("rejected: rate is below debounce window or out of range");}
else if(prompt==PROMPT_SIM_DRIFT){long v=strtol(input,NULL,10);if(v==POS_MIN||v==POS_MAX)dbg_sim_drift((position_t)v);else line("rejected: limit must be 1 or 5");}
prompt=PROMPT_NONE;input_len=0; }
void dbg_handle_key(char c) { if((c=='\b'||c==127)&&prompt!=PROMPT_NONE){if(input_len){--input_len;if(echo_enabled)console_debug_write("\b \b");}return;}if(echo_enabled){if(c=='\n')console_debug_write("\r\n");else if(c!='\r'){char e[2]={c,0};console_debug_write(e);}} if(action!=ACT_NONE){ if(action==ACT_BENCH_PINS){action=ACT_NONE;line("PIN STATE stopped");return;} if(action==ACT_CAL_POS_WAIT&&c==' '){action=ACT_CAL_POS_SAMPLE;action_started=ms_now();action_deadline=action_started+DEBUG_CAL_SAMPLE_MS;action_sum=action_count=0;action_min=ADC_MAX_VALUE;action_max=0;return;} dbg_abort(); return; } if(prompt!=PROMPT_NONE){if(c=='\r')return;if(c=='\n'){finish_prompt();return;}if(input_len<CONSOLE_LINE_MAX)input[input_len++]=c;return;} activity(); if(c=='x'){dbg_exit();return;}if(c=='w'){dbg_what_can_run();return;}if(c=='?'){dbg_help();return;}if(c=='q'){menu=MENU_ROOT;dbg_render();return;}if(menu==MENU_ROOT&&c>='1'&&c<='9'){menu=(menu_t)(c-'0');dbg_render();return;}switch(menu){case MENU_STATUS:if(c=='s')dbg_status_dump();else if(c=='t')dbg_stream_toggle();else if(c=='r'){{char b[64];snprintf(b,sizeof b,"rate (1-50 Hz, current %u Hz):",stream_hz);line(b);}prompt=PROMPT_RATE;input_len=0;}else if(c=='k')dbg_timing_stats();else if(c=='z')dbg_timing_reset();break;case MENU_ADC:if(c=='a')dbg_adc_read_once();else if(c=='m')dbg_adc_monitor_toggle();else if(c=='c')dbg_adc_capture_toggle();else if(c=='b')dbg_band_table();else if(c=='e')dbg_band_margin();break;case MENU_MOTOR:if(c=='A'){if(armed)dbg_motor_disarm();else dbg_motor_arm();}else if(c=='f')dbg_motor_pulse(DIR_FWD,pulse_duty,pulse_ms);else if(c=='v')dbg_motor_pulse(DIR_REV,pulse_duty,pulse_ms);else if(c=='d'){{char b[64];snprintf(b,sizeof b,"duty (0-255, current %u): ",pulse_duty);line(b);}prompt=PROMPT_DUTY;input_len=0;}else if(c=='t'){{char b[72];snprintf(b,sizeof b,"duration (10-2000 ms, current %u ms): ",pulse_ms);line(b);}prompt=PROMPT_DURATION;input_len=0;}else if(c=='b')dbg_motor_brake();else if(c=='c')dbg_motor_coast();else if(c=='s')dbg_motor_standby(true);else if(c=='n')dbg_motor_find_min(DIR_FWD);break;case MENU_CAL:if(c=='p')dbg_cal_positions();else if(c=='s')dbg_cal_step_time();else if(c=='w')dbg_cal_travel_time();else if(c=='o')dbg_cal_overshoot();else if(c=='r')dbg_cal_report();break;case MENU_CFG:if(c=='l')dbg_cfg_list();else if(c=='s'){line("key:");prompt=PROMPT_CFG_KEY;input_len=0;}else if(c=='d')dbg_cfg_reset();else if(c=='e')dbg_cfg_export();break;case MENU_FAULT:if(c=='f')dbg_fault_show();else if(c=='h')dbg_history_dump();else if(c=='c')dbg_counters_show();else if(c=='z')dbg_counters_reset();else if(c=='k')dbg_fault_clear();break;case MENU_TEST:if(c=='s')dbg_selftest_static();else if(c=='m')dbg_selftest_motion();break;case MENU_BENCH:if(c=='p')dbg_bench_pins();else if(c=='g')dbg_bench_gpio_walk();else if(c=='f')dbg_bench_pwm_report();else if(c=='t')dbg_bench_tick_health();else if(c=='r')dbg_bench_reset_reason();else if(c=='o')dbg_bench_protocol_list();else if(c=='e')dbg_bench_echo_toggle();break;case MENU_SIM:if(c=='e')dbg_sim_toggle();else if(c=='v'){line("adc value (0-4095, unit counts): ");prompt=PROMPT_SIM_VALUE;input_len=0;}else if(c=='b'){line("band (0-5, 0=open): ");prompt=PROMPT_SIM_BAND;input_len=0;}else if(c=='t'){line("from band (1-5): ");prompt=PROMPT_SIM_FROM;input_len=0;}else if(c=='p')dbg_sim_park();else if(c=='l'){line("limit (1 or 5): ");prompt=PROMPT_SIM_DRIFT;input_len=0;}else if(c=='s')dbg_sim_sweep();break;default:break;} }
void dbg_poll(void) { uint32_t n=ms_now(); action_poll(n);if(armed&&(int32_t)(n-arm_deadline)>=0)dbg_motor_disarm();if(coupled&&(int32_t)(n-coupled_deadline)>=0)dbg_coupled_clear();if(streaming&&(int32_t)(n-next_stream)>=0){char b[128];snprintf(b,sizeof b,"T %lu %s %u %u %s %u %u %u",(unsigned long)n,state_text(controller_state()),controller_position(),controller_target(),dir_text(motor_direction()),motor_duty(),encoder_raw(),encoder_average());line(b);next_stream=n+DEBUG_SELFTEST_WINDOW_MS/stream_hz;}if(monitoring&&(int32_t)(n-next_stream)>=0){dbg_adc_read_once();next_stream=n+DEBUG_ADC_MONITOR_PERIOD_MS;}if(capturing){uint16_t v=encoder_average();if(!capture_samples||v<capture_min)capture_min=v;if(!capture_samples||v>capture_max)capture_max=v;capture_samples++;} }

void dbg_status_dump(void){char b[160];snprintf(b,sizeof b,"state %s pos %u target %u dir %s duty %u deadline %lu lastdir %s avg %u armed %s uptime %lu",state_text(controller_state()),controller_position(),controller_target(),dir_text(motor_direction()),motor_duty(),(unsigned long)controller_deadline_ms(),dir_text(controller_last_direction()),encoder_average(),armed?"YES":"NO",(unsigned long)ms_now());line(b);}
void dbg_stream_toggle(void){streaming=!streaming;next_stream=ms_now();line(streaming?"telemetry started":"telemetry stopped");}void dbg_stream_set_rate(uint16_t hz){if(hz>=DEBUG_STREAM_MIN_HZ&&hz<=DEBUG_STREAM_MAX_HZ)stream_hz=hz;line(hz>=DEBUG_STREAM_MIN_HZ&&hz<=DEBUG_STREAM_MAX_HZ?"stream rate updated":"stream rate must be 1..50");}void dbg_timing_stats(void){tick_stats_t s;char b[128];controller_timing_get(&s);snprintf(b,sizeof b,"TIMING min=%lu max=%lu mean=%llu overruns=%lu",(unsigned long)s.min_us,(unsigned long)s.max_us,s.count?(unsigned long long)(s.sum_us/s.count):0ull,(unsigned long)s.overruns);line(b);}void dbg_timing_reset(void){controller_timing_reset();line("timing statistics reset");}
void dbg_adc_read_once(void){char b[96];snprintf(b,sizeof b,"ADC raw=%u avg=%u band=%u confirmed=%s",encoder_raw(),encoder_average(),encoder_instant(),encoder_confirmed()==encoder_instant()?"YES":"NO");line(b);}void dbg_adc_monitor_toggle(void){monitoring=!monitoring;next_stream=ms_now();line(monitoring?"ADC monitor started":"ADC monitor stopped");}void dbg_adc_capture_toggle(void){capturing=!capturing;if(capturing){capture_samples=0;capture_min=ADC_MAX_VALUE;capture_max=0;line("ADC capture started");}else{char b[160];uint16_t lo,hi;position_t p=controller_position();band_limits(p,&lo,&hi);uint16_t margin=(capture_min>lo?capture_min-lo:0)<(hi>capture_max?hi-capture_max:0)?(capture_min>lo?capture_min-lo:0):(hi>capture_max?hi-capture_max:0);uint16_t width=hi-lo+1u;snprintf(b,sizeof b,"CAPTURE pos=%u samples=%lu min=%u max=%u ripple=%u band=%u..%u margin=%u/%u%%",p,(unsigned long)capture_samples,capture_min,capture_max,capture_max-capture_min,lo,hi,margin,(unsigned)(margin*DEBUG_PERCENT_SCALE/width));line(b);}}void dbg_band_table(void){char b[128];snprintf(b,sizeof b,"BANDS 1=0..%u 2=%u..%u 3=%u..%u 4=%u..%u 5=%u..%u",CFG_BAND_P1_MAX,CFG_BAND_P1_MAX+1,CFG_BAND_P2_MAX,CFG_BAND_P2_MAX+1,CFG_BAND_P3_MAX,CFG_BAND_P3_MAX+1,CFG_BAND_P4_MAX,CFG_BAND_P4_MAX+1,CFG_BAND_P5_MAX);line(b);}void dbg_band_margin(void){char b[112];uint16_t lo,hi,v=encoder_average();position_t p=encoder_instant();band_limits(p,&lo,&hi);uint16_t margin=(v-lo)<(hi-v)?v-lo:hi-v;snprintf(b,sizeof b,"BAND MARGIN pos=%u value=%u edge_distance=%u width=%u percent=%u",p,v,margin,hi-lo+1u,(unsigned)(margin*DEBUG_PERCENT_SCALE/(hi-lo+1u)));line(b);}
bool dbg_motor_arm(void){line("Manual drive bypasses position limits.\r\nThe mechanism has NO physical end-stops.\r\nType UNCOUPLED to confirm the motor is disconnected:");prompt=PROMPT_ARM;return false;}void dbg_motor_disarm(void){armed=false;(void)post(DBG_OP_EXIT,DIR_STOP,0,0,false);}bool dbg_motor_armed(void){return armed;}void dbg_motor_pulse(direction_t d,uint8_t duty,uint16_t ms){if(!armed){line("debug: not armed");return;}if(!post(DBG_OP_DRIVE,d,duty,ms,false))line("debug: busy");}void dbg_motor_brake(void){if(!post(DBG_OP_BRAKE,DIR_STOP,0,0,false))line("debug: brake request rejected");}void dbg_motor_coast(void){if(post(DBG_OP_COAST,DIR_STOP,0,0,false))line("motor coasting; mechanism may be moved by hand");}void dbg_motor_standby(bool on){(void)post(DBG_OP_STANDBY,DIR_STOP,0,0,on);}void dbg_motor_find_min(direction_t d){char b[48];if(!armed){line("debug: not armed");return;}snprintf(b,sizeof b,"FINDMIN dir=%s",dir_text(d));line(b);action=ACT_FINDMIN_BASE;action_stage=(uint8_t)d;action_duty=DUTY_MIN-DEBUG_FINDMIN_DUTY_OFFSET;action_started=ms_now();action_deadline=action_started+DEBUG_FINDMIN_PULSE_MS;action_min=ADC_MAX_VALUE;action_max=0;}
bool dbg_coupled_confirm(void){line("This test moves the mechanism under closed-loop control.\r\nPosition limits ARE enforced. The mechanism must be connected.\r\nType COUPLED to confirm:");prompt=PROMPT_COUPLED;return false;}void dbg_coupled_clear(void){coupled=false;}bool dbg_coupled(void){return coupled;}
void dbg_cal_positions(void){line("CAL POSITIONS\r\n Move to position 1, press SPACE (q to abort)");action=ACT_CAL_POS_WAIT;action_stage=1;}
void dbg_cal_step_time(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=3){line("CAL STEP requires position 3");return;}line("CAL STEP");action=ACT_CAL_STEP;action_stage=0;action_worst=0;action_started=0;}
void dbg_cal_travel_time(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=POS_MAX){line("travel calibration requires position 5");return;}line("CAL TRAVEL");action=ACT_CAL_TRAVEL;action_stage=0;action_started=0;}
void dbg_cal_overshoot(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=2&&controller_position()!=4){line("CAL OVERSHOOT requires position 2 or 4");return;}{char b[64];snprintf(b,sizeof b,"CAL OVERSHOOT target=3 nominal=%u",DEBUG_NOMINAL_P3);line(b);}action=ACT_CAL_OVER;action_stage=0;action_started=0;action_count=0;action_source=controller_position();action_saved_duty=cfg.duty_approach;action_duty=DEBUG_OVERSHOOT_DUTY_HIGH;}
void dbg_cal_report(void){dbg_cfg_export();}
void dbg_cfg_list(void){dbg_cfg_export();}
bool dbg_cfg_set(const char *k,int32_t v){
    if(v<0)return false;
    if((!strncmp(k,"DUTY_",5)&&v>PWM_WRAP)||(!strncmp(k,"BAND_",5)&&v>=(int32_t)ADC_MAX_VALUE)||((!strcmp(k,"DEBOUNCE_MS")||!strcmp(k,"BRAKE_HOLD_MS"))&&v>UINT16_MAX))return false;
    cfg_t n; memcpy(&n,(const void *)&cfg,sizeof n);
    if(!strcmp(k,"DUTY_NORMAL"))n.duty_normal=v; else if(!strcmp(k,"DUTY_APPROACH"))n.duty_approach=v; else if(!strcmp(k,"DUTY_CREEP"))n.duty_creep=v; else if(!strcmp(k,"DUTY_MIN"))n.duty_min=v;
    else if(!strcmp(k,"BAND_P1_MAX"))n.band_p1_max=v; else if(!strcmp(k,"BAND_P2_MAX"))n.band_p2_max=v; else if(!strcmp(k,"BAND_P3_MAX"))n.band_p3_max=v; else if(!strcmp(k,"BAND_P4_MAX"))n.band_p4_max=v; else if(!strcmp(k,"BAND_P5_MAX"))n.band_p5_max=v;
    else if(!strcmp(k,"DEBOUNCE_MS"))n.debounce_ms=v; else if(!strcmp(k,"BRAKE_HOLD_MS"))n.brake_hold_ms=v; else if(!strcmp(k,"TIMEOUT_STEP_MS"))n.timeout_step_ms=v; else if(!strcmp(k,"TIMEOUT_HOME_MS"))n.timeout_home_ms=v; else if(!strcmp(k,"TIMEOUT_RECOVER_MS"))n.timeout_recover_ms=v; else return false;
    if(n.duty_min>n.duty_creep||n.duty_creep>n.duty_approach||n.duty_approach>n.duty_normal)return false;
    if(!(n.band_p1_max<n.band_p2_max&&n.band_p2_max<n.band_p3_max&&n.band_p3_max<n.band_p4_max&&n.band_p4_max<n.band_p5_max&&n.band_p5_max<ADC_MAX_VALUE))return false;
    if(n.debounce_ms==0||n.brake_hold_ms==0||n.timeout_step_ms<DEBUG_CFG_TIMEOUT_MIN_MS||n.timeout_home_ms<DEBUG_CFG_TIMEOUT_MIN_MS||n.timeout_recover_ms<DEBUG_CFG_TIMEOUT_MIN_MS)return false;
    memcpy((void *)&cfg,&n,sizeof n);return true;
}
void dbg_cfg_reset(void){cfg_reset();line("configuration defaults restored");}
void dbg_cfg_export(void){char b[96];line("CONFIG EXPORT");
#define OUT(name,val) do{snprintf(b,sizeof b,"#define " name " %lu",(unsigned long)(val));line(b);}while(0)
OUT("DUTY_NORMAL",cfg.duty_normal);OUT("DUTY_APPROACH",cfg.duty_approach);OUT("DUTY_CREEP",cfg.duty_creep);OUT("DUTY_MIN",cfg.duty_min);OUT("BAND_P1_MAX",cfg.band_p1_max);OUT("BAND_P2_MAX",cfg.band_p2_max);OUT("BAND_P3_MAX",cfg.band_p3_max);OUT("BAND_P4_MAX",cfg.band_p4_max);OUT("BAND_P5_MAX",cfg.band_p5_max);OUT("DEBOUNCE_MS",cfg.debounce_ms);OUT("BRAKE_HOLD_MS",cfg.brake_hold_ms);OUT("TIMEOUT_STEP_MS",cfg.timeout_step_ms);OUT("TIMEOUT_HOME_MS",cfg.timeout_home_ms);OUT("TIMEOUT_RECOVER_MS",cfg.timeout_recover_ms);
#undef OUT
}
void dbg_fault_show(void){fault_record_t f;char b[160];controller_fault_get(&f);snprintf(b,sizeof b,"FAULT kind=%u ms=%lu state=%s pos=%u target=%u deadline=%lu",f.kind,(unsigned long)f.ms,state_text(f.state),f.pos,f.target,(unsigned long)f.deadline_ms);line(b);}void dbg_history_dump(void){line("HISTORY (newest last)");for(uint8_t i=0;i<controller_history_count();++i){hist_entry_t h;char b[64];if(controller_history_get(i,&h)){const char *k=h.kind==0?"PASS":h.kind==1?"ARR":"UNKNOWN";snprintf(b,sizeof b,"  %lu  %s %u",(unsigned long)h.ms,k,h.pos);line(b);}}}void dbg_counters_show(void){dbg_counters_t c;char b[240];controller_counters_get(&c);snprintf(b,sizeof b,"COUNTERS moves_ok=%lu moves_timeout=%lu recover_entered=%lu recover_ok=%lu faults=%lu limit_rejects=%lu pass_events=%lu tick_overruns=%lu",(unsigned long)c.moves_ok,(unsigned long)c.moves_timeout,(unsigned long)c.recover_entered,(unsigned long)c.recover_ok,(unsigned long)c.faults,(unsigned long)c.limit_rejects,(unsigned long)c.pass_events,(unsigned long)c.tick_overruns);line(b);}void dbg_counters_reset(void){controller_counters_reset();line("counters reset");}void dbg_fault_clear(void){line("Clearing a fault does not restore position.\r\nA home sequence will still be required before any move.\r\nType CLEAR to confirm:");prompt=PROMPT_CLEAR;}
bool dbg_selftest_static(void){tick_stats_t s;controller_timing_get(&s);self_tick_start=s.count;self_all_full=console_event_queue_full();action=ACT_STATIC;action_stage=0;action_count=0;action_deadline=ms_now();line("SELFTEST STATIC");return true;}bool dbg_selftest_motion(void){if(!coupled){dbg_coupled_confirm();return false;}line("SELFTEST MOTION");action=ACT_SELFTEST;action_stage=0;action_started=0;return true;}

static uint16_t sim_nominal(position_t pos)
{
    static const uint16_t values[] = { DEBUG_SIM_OPEN_ADC, DEBUG_SIM_NOMINAL_P1, DEBUG_SIM_NOMINAL_P2, DEBUG_SIM_NOMINAL_P3, DEBUG_SIM_NOMINAL_P4, DEBUG_SIM_NOMINAL_P5 };
    return values[pos <= POS_MAX ? pos : POS_UNKNOWN];
}

static bool sim_history_ok(position_t from, position_t to)
{
    uint8_t distance=(uint8_t)(from>to?from-to:to-from);uint8_t used=controller_history_count();if(used<distance)return false;uint8_t first=(uint8_t)(used-distance);
    for(uint8_t i=0;i<distance;++i){hist_entry_t h;if(!controller_history_get((uint8_t)(first+i),&h))return false;position_t expected=to>from?(position_t)(from+i+1u):(position_t)(from-i-1u);uint8_t kind=(uint8_t)(i+1u==distance?1u:0u);if(h.pos!=expected||h.kind!=kind)return false;}return true;
}

void dbg_what_can_run(void)
{
    line("AVAILABLE NOW"); line("  2/a  adc reading            ready"); line("  7/s  static self-test       ready"); line("  8/*  bench tests            ready"); line("  9/*  simulation             ready"); if(armed)line("  3/*  manual drive           ready");if(coupled && controller_position()!=POS_UNKNOWN){line("  4/s  step time              ready");line("  7/m  motion self-test       ready");}line("BLOCKED");
    if(!armed)line("  3/*  manual drive           needs UNCOUPLED (menu 3, key A)");
    if(!(coupled && controller_position()!=POS_UNKNOWN))line("  4/s  step time              needs COUPLED and a known position");
    if(!(coupled && controller_position()!=POS_UNKNOWN))line("  7/m  motion self-test       needs COUPLED and a known position");
}

void dbg_bench_pins(void){line("PIN STATE (any key to stop)");action=ACT_BENCH_PINS;action_deadline=ms_now();}
void dbg_bench_gpio_walk(void){if(!armed){line("rejected: needs UNCOUPLED (menu 3, key A)");return;}if(encoder_sim_active()){line("rejected: disable simulation first");return;}line("GPIO WALK  (UNCOUPLED confirmed, PWMA held 0)");action=ACT_GPIO_WALK;action_stage=0;action_deadline=ms_now();}
void dbg_bench_pwm_report(void)
{
    char b[128]; uint slice=pwm_gpio_to_slice_num(PIN_PWMA); uint chan=pwm_gpio_to_channel(PIN_PWMA); uint32_t clock=clock_get_hz(clk_sys); uint32_t raw=pwm_hw->slice[slice].div & DEBUG_PWM_DIV_MASK; uint32_t wrap=pwm_hw->slice[slice].top; uint32_t hz10=(uint32_t)(((uint64_t)clock*DEBUG_PWM_FIXED_SCALE*DEBUG_FREQ_TENTHS)/(raw*(wrap+1u))); bool pass=hz10>=DEBUG_PWM_SPEC_HZ*DEBUG_FREQ_TENTHS*(DEBUG_PERCENT_SCALE-DEBUG_PWM_TOLERANCE_PERCENT)/DEBUG_PERCENT_SCALE && hz10<=DEBUG_PWM_SPEC_HZ*DEBUG_FREQ_TENTHS*(DEBUG_PERCENT_SCALE+DEBUG_PWM_TOLERANCE_PERCENT)/DEBUG_PERCENT_SCALE;
    line("PWM CONFIG");snprintf(b,sizeof b,"  slice        %u, channel %c (GP%u)",slice,chan?'B':'A',PIN_PWMA);line(b);snprintf(b,sizeof b,"  clk_sys      %lu Hz",(unsigned long)clock);line(b);snprintf(b,sizeof b,"  clkdiv       %lu.%04lu  (raw 0x%03lX, 8.4 fixed point)",(unsigned long)(raw/DEBUG_PWM_FIXED_SCALE),(unsigned long)((raw%DEBUG_PWM_FIXED_SCALE)*DEBUG_DECIMAL_4_SCALE/DEBUG_PWM_FIXED_SCALE),(unsigned long)raw);line(b);snprintf(b,sizeof b,"  wrap         %lu",(unsigned long)wrap);line(b);snprintf(b,sizeof b,"  frequency    %lu.%lu Hz",(unsigned long)(hz10/DEBUG_FREQ_TENTHS),(unsigned long)(hz10%DEBUG_FREQ_TENTHS));line(b);snprintf(b,sizeof b,"  spec         %u Hz, tolerance %u%%   %s",DEBUG_PWM_SPEC_HZ,DEBUG_PWM_TOLERANCE_PERCENT,pass?"PASS":"FAIL");line(b);
}
void dbg_bench_tick_health(void){tick_stats_t s;controller_timing_get(&s);bench_tick_start=s.count;action_started=ms_now();action_deadline=action_started+DEBUG_TICK_HEALTH_WINDOW_MS;action=ACT_TICK_HEALTH;line("TICK HEALTH");}
void dbg_bench_reset_reason(void){char b[96];line("RESET REASON");line(watchdog_caused_reboot()?"  watchdog reset":"  power-on or debug reset");snprintf(b,sizeof b,"  uptime %lu ms",(unsigned long)ms_now());line(b);}
void dbg_bench_protocol_list(void){line("PROTOCOL STRINGS (listing only, not emitted)\r\n  commands   pos | move N | stop | status | home\r\n  ok         OK: moving to N\r\n             OK: already at N\r\n             OK: stopped\r\n             OK: homing\r\n  errors     ERR: unknown command\r\n             ERR: invalid target\r\n             ERR: at end-stop\r\n             ERR: busy\r\n             ERR: position unknown\r\n             ERR: fault\r\n             ERR: line too long\r\n             ERR: timeout\r\n             ERR: fault home timeout\r\n             ERR: fault recover timeout\r\n             ERR: watchdog reset\r\n  events     PASS:N | ARR:N | OK: homing | STOPPED: position unknown");}
void dbg_bench_echo_toggle(void){echo_enabled=!echo_enabled;line(echo_enabled?"character echo ON":"character echo OFF");}

void dbg_sim_toggle(void){if(encoder_sim_active()){if(post(DBG_OP_SIM_ENABLE,DIR_STOP,0,0,false))line("SIM DISABLE\r\n  motor inhibit released\r\n  adc source: hardware\r\n  sim OFF");return;}armed=coupled=false;if(post(DBG_OP_SIM_ENABLE,DIR_STOP,0,0,true))line("SIM ENABLE\r\n  motor inhibited (STBY forced low)\r\n  adc source: simulated, starting at 372 (position 1)\r\n  sim ON");else line("rejected: debug mailbox busy");}
void dbg_sim_set_value(uint16_t adc){char b[112];if(!encoder_sim_active()){line("rejected: enable simulation first");return;}encoder_sim_set(adc);snprintf(b,sizeof b,"SIM ADC %u (classification updates through filter and debounce)",adc);line(b);}
void dbg_sim_set_band(position_t pos){char b[96];if(!encoder_sim_active()){line("rejected: enable simulation first");return;}encoder_sim_set(sim_nominal(pos));snprintf(b,sizeof b,"SIM BAND %u, adc %u",pos,sim_nominal(pos));line(b);}
void dbg_sim_travel(position_t from,position_t to,uint16_t ms){char b[96];if(!encoder_sim_active()){line("rejected: enable simulation first");return;}if(from==to){line("rejected: from and to must differ");return;}sim_from=from;sim_to=to;sim_band_ms=ms;encoder_sim_set(sim_nominal(from));snprintf(b,sizeof b,"SIM TRAVEL %u -> %u, %u ms/band",from,to,ms);line(b);action=ACT_SIM_TRAVEL;action_stage=0;action_started=ms_now();action_deadline=action_started+DEBUG_SIM_SWEEP_SETTLE_MS;action_count=0;action_pass=true;}
void dbg_sim_park(void){if(!encoder_sim_active()){line("rejected: enable simulation first");return;}line("SIM PARK BETWEEN REEDS");encoder_sim_set(DEBUG_SIM_OPEN_ADC);action=ACT_SIM_PARK;action_started=ms_now();action_stage=0;}
void dbg_sim_drift(position_t limit){char b[64];if(!encoder_sim_active()){line("rejected: enable simulation first");return;}snprintf(b,sizeof b,"SIM DRIFT PAST LIMIT %u",limit);line(b);sim_from=limit;encoder_sim_set(sim_nominal(limit));action=ACT_SIM_DRIFT;action_stage=0;action_deadline=ms_now()+DEBUG_SIM_SWEEP_SETTLE_MS;}
void dbg_sim_sweep(void){if(!encoder_sim_active()){line("rejected: enable simulation first");return;}line("SIM SWEEP");encoder_sim_set(0);action=ACT_SIM_SWEEP;action_count=0;action_last_pos=POS_UNKNOWN;action_pass=true;action_deadline=ms_now()+DEBUG_SIM_SWEEP_SETTLE_MS;}

static void action_sample_adc(void)
{
    uint16_t v = encoder_average();
    if (v < action_min) action_min = v;
    if (v > action_max) action_max = v;
    action_sum += v;
    ++action_count;
}

static void action_poll(uint32_t now)
{
    static const position_t step_targets[] = {4, 3, 2, 3};
    static const position_t self_targets[] = {2, 3, 2, 1};
    char b[128];
    if (action == ACT_NONE || action == ACT_CAL_POS_WAIT) return;
    if (action == ACT_BENCH_PINS) {
        if ((int32_t)(now-action_deadline)<0) return;
        uint slice=pwm_gpio_to_slice_num(PIN_PWMA); uint32_t cc=pwm_hw->slice[slice].cc; uint16_t level=(uint16_t)(pwm_gpio_to_channel(PIN_PWMA)?cc>>16:cc);
        snprintf(b,sizeof b,"  GP%u AIN1 %u  GP%u AIN2 %u  GP%u PWMA pwm, level %u / %u",PIN_AIN1,gpio_get(PIN_AIN1),PIN_AIN2,gpio_get(PIN_AIN2),PIN_PWMA,level,PWM_WRAP);line(b); snprintf(b,sizeof b,"  GP%u STBY %u  GP%u SENSE adc %u",PIN_STBY,gpio_get(PIN_STBY),PIN_SENSE,encoder_raw());line(b); action_deadline=now+DEBUG_BENCH_PIN_PERIOD_MS; return;
    }
    if (action == ACT_GPIO_WALK) {
        if ((int32_t)(now-action_deadline)<0) return;
        if(action_stage==0u){(void)post(DBG_OP_GPIO_SET,DIR_STOP,DEBUG_GPIO_OP_AIN1,0,false);line("  AIN1 high ...");}
        else if(action_stage==1u){(void)post(DBG_OP_GPIO_SET,DIR_STOP,DEBUG_GPIO_OP_AIN2,0,false);line("  AIN1 low\r\n  AIN2 high ...");}
        else if(action_stage==2u){(void)post(DBG_OP_GPIO_SET,DIR_STOP,DEBUG_GPIO_OP_STBY,0,false);line("  AIN2 low\r\n  STBY high ...");}
        else {(void)post(DBG_OP_GPIO_SET,DIR_STOP,DEBUG_GPIO_OP_ALL_LOW,0,false);line("  STBY low\r\n  done");action=ACT_NONE;return;}
        ++action_stage;action_deadline=now+DEBUG_GPIO_WALK_STEP_MS;return;
    }
    if (action == ACT_TICK_HEALTH) {
        if ((int32_t)(now-action_deadline)<0) return;
        tick_stats_t s;controller_timing_get(&s);uint32_t elapsed=now-action_started,ticks=s.count-bench_tick_start,rate10=(ticks*DEBUG_FREQ_TENTHS*DEBUG_SELFTEST_WINDOW_MS)/elapsed;bool pass=rate10>=(TICK_HZ-DEBUG_TICK_RATE_TOLERANCE_HZ)*DEBUG_FREQ_TENTHS && rate10<=(TICK_HZ+DEBUG_TICK_RATE_TOLERANCE_HZ)*DEBUG_FREQ_TENTHS;
        snprintf(b,sizeof b,"  measured rate   %lu.%lu Hz over %lu.%02lu s     %s (1000 +/- 20)",(unsigned long)(rate10/10u),(unsigned long)(rate10%10u),(unsigned long)(elapsed/1000u),(unsigned long)((elapsed%1000u)/10u),pass?"PASS":"FAIL");line(b);snprintf(b,sizeof b,"  duration        min %lu us  max %lu us  mean %llu us",(unsigned long)s.min_us,(unsigned long)s.max_us,s.count?(unsigned long long)(s.sum_us/s.count):0ull);line(b);snprintf(b,sizeof b,"  overruns        %lu",(unsigned long)s.overruns);line(b);line("  watchdog        enabled, 100 ms, pause_on_debug true");snprintf(b,sizeof b,"  time to expiry  %lu ms at last read",(unsigned long)(watchdog_get_time_remaining_ms()));line(b);action=ACT_NONE;return;
    }
    if (action == ACT_SIM_TRAVEL) {
        if(action_stage==0u){if(encoder_confirmed()!=sim_from||(int32_t)(now-action_deadline)<0)return;if(controller_request(REQ_MOVE,sim_to)!=MOVE_OK){line("  RESULT: FAIL  (move request rejected)");action=ACT_NONE;return;}snprintf(b,sizeof b,"  > move %u\r\n  OK: moving to %u",sim_to,sim_to);line(b);action_last_pos=sim_from;action_stage=1u;action_deadline=now+sim_band_ms;return;}
        if(action_stage==1u){if((int32_t)(now-action_deadline)<0)return;position_t next=action_last_pos+(sim_to>sim_from?1:-1);encoder_sim_set(sim_nominal(next));action_last_pos=next;action_deadline=now+sim_band_ms;if(next==sim_to)action_stage=2u;return;}
        if(controller_state()==ST_FAULT){line("  RESULT: FAIL  (controller fault)");action=ACT_NONE;return;}if(controller_state()==ST_IDLE && controller_position()==sim_to){uint32_t expected=sim_from>sim_to?sim_from-sim_to:sim_to-sim_from;action_pass=sim_history_ok(sim_from,sim_to);snprintf(b,sizeof b,"  final state IDLE, pos %u",sim_to);line(b);snprintf(b,sizeof b,"  RESULT: %s  (%lu events expected, order checked by controller history)",action_pass?"PASS":"FAIL",(unsigned long)expected);line(b);action=ACT_NONE;}return;
    }
    if (action == ACT_SIM_PARK) {
        if(action_stage==0u && controller_state()==ST_RECOVER){line("  state RECOVER");action_stage=1u;return;}if(controller_state()==ST_FAULT){line(action_stage==1u?"  RESULT: PASS  (recovery timeout escalated to fault)":"  RESULT: FAIL  (fault without observed recovery)");action=ACT_NONE;}return;
    }
    if (action == ACT_SIM_DRIFT) {
        direction_t expected=sim_from==POS_MIN?DIR_FWD:DIR_REV; if(action_stage==0u){if((int32_t)(now-action_deadline)<0||encoder_confirmed()!=sim_from)return;snprintf(b,sizeof b,"  established pos %u (adc %u)",sim_from,sim_nominal(sim_from));line(b);encoder_sim_set(DEBUG_SIM_OPEN_ADC);line("  injected open band (adc 4095)");action_stage=1u;return;}if(controller_state()!=ST_RECOVER)return;line("  state RECOVER");snprintf(b,sizeof b,"  recovery direction: %s",dir_text(motor_direction()));line(b);snprintf(b,sizeof b,"  EXPECTED: %s (inward, away from the %s limit)",dir_text(expected),sim_from==POS_MIN?"lower":"upper");line(b);line(motor_direction()==expected?"  RESULT: PASS":"  RESULT: FAIL");action=ACT_NONE;return;
    }
    if (action == ACT_SIM_SWEEP) {
        if((int32_t)(now-action_deadline)<0)return;
        uint16_t value=(uint16_t)action_count;position_t expected=value<=CFG_BAND_P1_MAX?1:value<=CFG_BAND_P2_MAX?2:value<=CFG_BAND_P3_MAX?3:value<=CFG_BAND_P4_MAX?4:value<=CFG_BAND_P5_MAX?5:POS_UNKNOWN;position_t got=encoder_confirmed();if(got!=expected)action_pass=false;if(action_count==0u){action_last_pos=got;action_started=0u;}else if(got!=action_last_pos){snprintf(b,sizeof b,"  %lu..%lu      -> %s",(unsigned long)action_started,(unsigned long)(action_count-1u),action_last_pos==POS_UNKNOWN?"?":action_last_pos==1?"1":action_last_pos==2?"2":action_last_pos==3?"3":action_last_pos==4?"4":"5");line(b);action_started=action_count;action_last_pos=got;}if(action_count==ADC_MAX_VALUE){snprintf(b,sizeof b,"  %lu..%u   -> %s",(unsigned long)action_started,ADC_MAX_VALUE,action_last_pos==POS_UNKNOWN?"?":action_last_pos==1?"1":action_last_pos==2?"2":action_last_pos==3?"3":action_last_pos==4?"4":"5");line(b);snprintf(b,sizeof b,"  RESULT: %s, %u regions, matches config, no gaps or overlaps",action_pass?"PASS":"FAIL",DEBUG_SIM_EXPECTED_REGIONS);line(b);action=ACT_NONE;return;}++action_count;encoder_sim_set((uint16_t)action_count);action_deadline=now+DEBUG_SIM_SWEEP_SETTLE_MS;return;
    }
    if (action == ACT_STATIC) {
        if (action_stage == 0u) {
            if ((int32_t)(now - action_deadline) < 0) return;
            self_adc[action_count++] = encoder_raw();
            self_all_full = self_all_full && console_event_queue_full();
            if (action_count < DEBUG_SELFTEST_ADC_SAMPLES) {
                action_deadline = now + DEBUG_SELFTEST_SAMPLE_PERIOD_MS;
                return;
            }
            tick_stats_t stats;
            controller_timing_get(&stats);
            self_tick_start = stats.count;
            action_started = now;
            action_deadline = now + DEBUG_SELFTEST_WINDOW_MS;
            action_stage = 1u;
            return;
        }
        if ((int32_t)(now - action_deadline) < 0) {
            self_all_full = self_all_full && console_event_queue_full();
            return;
        }
        tick_stats_t stats;
        controller_timing_get(&stats);
        uint32_t elapsed = now - action_started;
        uint32_t expected = (elapsed * TICK_HZ) / DEBUG_SELFTEST_WINDOW_MS;
        uint32_t ticks = stats.count - self_tick_start;
        bool adc_ok = true, all_same = true;
        for (uint8_t i = 0; i < DEBUG_SELFTEST_ADC_SAMPLES; ++i) {
            if (i && self_adc[i] != self_adc[0]) all_same = false;
            if (self_adc[i] == 0u || (self_adc[i] == ADC_MAX_VALUE && encoder_instant() != POS_UNKNOWN)) adc_ok = false;
        }
        adc_ok = adc_ok && !all_same;
        bool ordered = CFG_BAND_P1_MAX < CFG_BAND_P2_MAX && CFG_BAND_P2_MAX < CFG_BAND_P3_MAX && CFG_BAND_P3_MAX < CFG_BAND_P4_MAX && CFG_BAND_P4_MAX < CFG_BAND_P5_MAX;
        bool coverage = CFG_BAND_P5_MAX < ADC_MAX_VALUE;
        bool duties = CFG_DUTY_MIN <= CFG_DUTY_CREEP && CFG_DUTY_CREEP <= CFG_DUTY_APPROACH && CFG_DUTY_APPROACH <= CFG_DUTY_NORMAL;
        bool timeouts = CFG_TIMEOUT_HOME_MS >= 4u * CFG_TIMEOUT_STEP_MS;
        bool tick_ok = ticks + DEBUG_SELFTEST_TICK_TOLERANCE >= expected && ticks <= expected + DEBUG_SELFTEST_TICK_TOLERANCE;
        bool queue_ok = !self_all_full;
        position_t pos = encoder_confirmed();
        bool position_ok = pos == POS_UNKNOWN || (pos >= POS_MIN && pos <= POS_MAX);
        snprintf(b,sizeof b,"  ADC responds %s",adc_ok?"PASS":"FAIL");line(b);
        snprintf(b,sizeof b,"  bands ordered %s  coverage %s",ordered?"PASS":"FAIL",coverage?"PASS":"FAIL");line(b);
        snprintf(b,sizeof b,"  duties %s  timeouts %s",duties?"PASS":"FAIL",timeouts?"PASS":"FAIL");line(b);
        snprintf(b,sizeof b,"  tick %lu/%lu %s  queue %s  position %s",(unsigned long)ticks,(unsigned long)expected,tick_ok?"PASS":"FAIL",queue_ok?"PASS":"FAIL",position_ok?"PASS":"FAIL");line(b);
        line(adc_ok&&ordered&&coverage&&duties&&timeouts&&tick_ok&&queue_ok&&position_ok?"SELFTEST STATIC PASS":"SELFTEST STATIC FAIL");
        action = ACT_NONE;
        return;
    }
    if (action == ACT_FINDMIN_BASE) {
        action_sample_adc();
        if ((int32_t)(now - action_deadline) < 0) return;
        noise_floor = action_max - action_min;
        action_start_adc = encoder_average();
        dbg_motor_pulse((direction_t)action_stage, action_duty, DEBUG_FINDMIN_PULSE_MS);
        action_started = now; action_deadline = now + DEBUG_FINDMIN_PULSE_MS; action_min = ADC_MAX_VALUE; action_max = 0;
        action = ACT_FINDMIN_PULSE;
        return;
    }
    if (action == ACT_FINDMIN_PULSE) {
        action_sample_adc();
        if ((int32_t)(now - action_deadline) < 0) return;
        uint16_t change = encoder_average() > action_start_adc ? encoder_average() - action_start_adc : action_start_adc - encoder_average();
        bool moved = change > noise_floor || (action_max - action_min) > noise_floor;
        snprintf(b,sizeof b,moved?"  duty %u  MOTION (adc %u -> %u)":"  duty %u  no motion",action_duty,action_start_adc,encoder_average()); line(b);
        if (moved) { uint16_t suggested=(uint16_t)((action_duty*(DEBUG_PERCENT_SCALE+DEBUG_FINDMIN_MARGIN_PERCENT)+DEBUG_PERCENT_SCALE-1u)/DEBUG_PERCENT_SCALE);snprintf(b,sizeof b,"FINDMIN result=%u  suggest DUTY_MIN=%u (result +10%% margin)",action_duty,suggested);line(b);action=ACT_NONE;return; }
        if (action_duty >= DEBUG_FINDMIN_DUTY_MAX) { line("FINDMIN stopped at duty 120 without motion"); action=ACT_NONE; return; }
        action_duty += DEBUG_FINDMIN_DUTY_STEP; action_start_adc=encoder_average();dbg_motor_pulse((direction_t)action_stage,action_duty,DEBUG_FINDMIN_PULSE_MS);action_deadline=now+DEBUG_FINDMIN_PULSE_MS;action_min=ADC_MAX_VALUE;action_max=0;return;
    }
    if (action == ACT_CAL_POS_SAMPLE) {
        action_sample_adc();
        if ((int32_t)(now-action_deadline)<0) return;
        action_means[action_stage-1u]=(uint16_t)(action_sum/action_count);
        snprintf(b,sizeof b,"  pos%u mean=%lu spread=%u",action_stage,(unsigned long)(action_sum/action_count),action_max-action_min);line(b);
        if (action_stage < POS_MAX) { ++action_stage; snprintf(b,sizeof b," Move to position %u, press SPACE",action_stage);line(b);action=ACT_CAL_POS_WAIT; }
        else if (action_stage == POS_MAX) { ++action_stage; line(" Move between reeds, press SPACE"); action=ACT_CAL_POS_WAIT; }
        else { cfg.band_p1_max=(action_means[0]+action_means[1])/2u;cfg.band_p2_max=(action_means[1]+action_means[2])/2u;cfg.band_p3_max=(action_means[2]+action_means[3])/2u;cfg.band_p4_max=(action_means[3]+action_means[4])/2u;cfg.band_p5_max=(action_means[4]+action_means[5])/2u;line("COMPUTED BANDS");dbg_band_table();for(uint8_t i=1;i<6u;++i)if((uint16_t)(action_means[i]-action_means[i-1u])<DEBUG_CAL_MIN_SEPARATION)line("  MARGINAL adjacent separation under 200");action=ACT_NONE; }
        return;
    }
    if (action == ACT_CAL_STEP) {
        if (action_stage >= 4u) { uint32_t suggest=((action_worst*2u+DEBUG_CAL_ROUND_MS-1u)/DEBUG_CAL_ROUND_MS)*DEBUG_CAL_ROUND_MS;snprintf(b,sizeof b,"  worst=%lu  suggest TIMEOUT_STEP_MS=%lu (worst x2, rounded up)",(unsigned long)action_worst,(unsigned long)suggest);line(b);action=ACT_NONE;return; }
        position_t dst=step_targets[action_stage];
        if (!action_started) { if(controller_request(REQ_MOVE,dst)==MOVE_OK)action_started=now; return; }
        if (controller_state()==ST_FAULT) { line("CAL STEP failed"); action=ACT_NONE; return; }
        if (controller_state()==ST_IDLE&&controller_position()==dst) { uint32_t elapsed=now-action_started;if(elapsed>action_worst)action_worst=elapsed;static const position_t srcs[]={3,4,3,2};snprintf(b,sizeof b,"  %u->%u  %lu ms",srcs[action_stage],dst,(unsigned long)elapsed);line(b);++action_stage;action_started=0; }
        return;
    }
    if (action == ACT_CAL_TRAVEL) {
        if (!action_started) { if(controller_request(REQ_HOME,0)==MOVE_OK)action_started=now;return; }
        if(controller_state()==ST_IDLE&&controller_position()==1){uint32_t elapsed=now-action_started;snprintf(b,sizeof b,"  5->1 %lu ms  suggest TIMEOUT_HOME_MS=%lu",(unsigned long)elapsed,(unsigned long)(elapsed*2u));line(b);action=ACT_NONE;}else if(controller_state()==ST_FAULT){line("CAL TRAVEL failed");action=ACT_NONE;}return;
    }
    if (action == ACT_CAL_OVER) {
        static const uint8_t duties[] = {DEBUG_OVERSHOOT_DUTY_HIGH,DEBUG_OVERSHOOT_DUTY_HIGH,DEBUG_OVERSHOOT_DUTY_MID,DEBUG_OVERSHOOT_DUTY_MID,DEBUG_OVERSHOOT_DUTY_LOW,DEBUG_OVERSHOOT_DUTY_LOW};
        static const position_t sources[] = {2,4,2,4,2,4};
        if (action_stage >= DEBUG_OVERSHOOT_TEST_COUNT) { cfg.duty_approach=action_saved_duty; line("  suggest DUTY_APPROACH=60"); action=ACT_NONE; return; }
        position_t source=sources[action_stage]; action_duty=duties[action_stage];
        if (controller_state()==ST_FAULT) { cfg.duty_approach=action_saved_duty;line("CAL OVERSHOOT failed");action=ACT_NONE;return; }
        if (action_count==0u && controller_position()!=source) { if(controller_state()==ST_IDLE&&controller_request(REQ_MOVE,source)==MOVE_OK)action_count=1u;return; }
        if (action_count==1u) { if(controller_state()==ST_IDLE&&controller_position()==source)action_count=0u;else return; }
        if (!action_started) { cfg.duty_approach=action_duty;if(controller_request(REQ_MOVE,3)==MOVE_OK)action_started=now;return; }
        if(controller_state()==ST_IDLE&&controller_position()==3){int32_t off=(int32_t)encoder_average()-DEBUG_NOMINAL_P3;snprintf(b,sizeof b,"  duty %u  settle=%u  offset=%+ld",action_duty,encoder_average(),(long)off);line(b);++action_stage;action_started=0;}
        return;
    }
    if (action == ACT_SELFTEST) {
        if (action_stage==0&&!action_started){if(controller_request(REQ_HOME,0)==MOVE_OK)action_started=now;return;}
        if(controller_state()==ST_FAULT){line("RESULT: motion self-test FAIL");action=ACT_NONE;return;}
        if(action_stage==0&&controller_state()==ST_IDLE&&controller_position()==1){snprintf(b,sizeof b,"  home        ARR 1   %lu ms  PASS",(unsigned long)(now-action_started));line(b);action_stage=1;action_started=0;return;}
        if(action_stage>=1&&action_stage<=4){position_t dst=self_targets[action_stage-1u];if(!action_started){if(controller_request(REQ_MOVE,dst)==MOVE_OK)action_started=now;return;}if(controller_state()==ST_IDLE&&controller_position()==dst){snprintf(b,sizeof b,"  step        ARR %u   %lu ms  PASS",dst,(unsigned long)(now-action_started));line(b);++action_stage;action_started=0;return;}}
        if(action_stage==5){line("RESULT: 5/5 PASS");action=ACT_NONE;}return;
    }
}
