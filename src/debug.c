#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/time.h"
#include "console.h"
#include "controller.h"
#include "encoder.h"
#include "motor.h"

typedef enum { MENU_ROOT, MENU_STATUS, MENU_ADC, MENU_MOTOR, MENU_CAL, MENU_CFG, MENU_FAULT, MENU_TEST } menu_t;
typedef enum { PROMPT_NONE, PROMPT_ARM, PROMPT_COUPLED, PROMPT_CLEAR, PROMPT_RATE, PROMPT_DUTY, PROMPT_DURATION, PROMPT_CFG_KEY, PROMPT_CFG_VALUE } prompt_t;
typedef enum { ACT_NONE, ACT_FINDMIN_BASE, ACT_FINDMIN_PULSE, ACT_CAL_POS_WAIT, ACT_CAL_POS_SAMPLE, ACT_CAL_STEP, ACT_CAL_TRAVEL, ACT_CAL_OVER, ACT_SELFTEST } action_t;
static bool active, armed, coupled, streaming, monitoring, capturing;
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
static void action_poll(uint32_t now);

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }
static const char *state_text(sys_state_t s) { static const char *const n[]={"BOOT","IDLE","MOVING","APPROACH","HOMING","RECOVER","FAULT","DEBUG"}; return n[s]; }
static const char *dir_text(direction_t d) { return d==DIR_FWD?"FWD":d==DIR_REV?"REV":"STP"; }
static void line(const char *s) { console_debug_line(s); }
static void band_limits(position_t p,uint16_t *lo,uint16_t *hi){static const uint16_t maxv[5]={BAND_P1_MAX,BAND_P2_MAX,BAND_P3_MAX,BAND_P4_MAX,BAND_P5_MAX};if(p<1||p>5){*lo=CFG_BAND_P5_MAX+1u;*hi=4095u;return;}uint16_t active[5]={CFG_BAND_P1_MAX,CFG_BAND_P2_MAX,CFG_BAND_P3_MAX,CFG_BAND_P4_MAX,CFG_BAND_P5_MAX};*lo=p==1?0u:(uint16_t)(active[p-2u]+1u);*hi=active[p-1u];(void)maxv;}
static bool post(dbg_op_t op, direction_t dir, uint8_t duty, uint16_t ms, bool flag) { dbg_request_t r={op,dir,duty,ms,flag}; return controller_debug_request(&r); }
static void activity(void) { uint32_t n=ms_now(); if(armed)arm_deadline=n+120000u;if(coupled)coupled_deadline=n+120000u; }

void dbg_init(void) { cfg_reset(); active=armed=coupled=streaming=monitoring=capturing=false; menu=MENU_ROOT; prompt=PROMPT_NONE; pulse_duty=DUTY_CREEP; pulse_ms=150; stream_hz=10; action=ACT_NONE; }
void dbg_enter(void) { if(controller_state()==ST_FAULT){line("debug unavailable while faulted");return;} active=true; menu=MENU_ROOT; dbg_render(); }
void dbg_exit(void) { (void)post(DBG_OP_EXIT,DIR_STOP,0,0,false); dbg_motor_disarm(); dbg_coupled_clear(); streaming=monitoring=capturing=false; cfg_reset(); active=false; line("debug exited"); }
bool dbg_active(void) { return active; }
void dbg_render_header(void) { char b[112]; snprintf(b,sizeof b,"=== luftfugl debug 1.0 ===\r\nstate %s  pos %u  adc %u  armed %s  coupled %s",state_text(controller_state()),controller_position(),encoder_average(),armed?"YES":"NO",coupled?"YES":"NO"); line(b); }
void dbg_render(void) { dbg_render_header(); switch(menu){case MENU_ROOT:line(" 1 status & telemetry\r\n 2 encoder & adc\r\n 3 motor manual\r\n 4 calibration\r\n 5 configuration\r\n 6 faults & history\r\n 7 self-test\r\n x exit");break;case MENU_STATUS:line(" s state dump  t stream  r rate  k timing  z reset");break;case MENU_ADC:line(" a reading  m monitor  c capture  b bands  e margin");break;case MENU_MOTOR:line(" A arm  f forward  v reverse  d duty  t duration  b brake  c coast  s standby  n find minimum");break;case MENU_CAL:line(" p positions  s step time  w travel time  o overshoot  r report");break;case MENU_CFG:line(" l list  s set  d defaults  e export");break;case MENU_FAULT:line(" f last fault  h history  c counters  z reset  k clear");break;case MENU_TEST:line(" s static  m motion");break;} }
void dbg_abort(void) { if(action==ACT_CAL_OVER)cfg.duty_approach=action_saved_duty; action=ACT_NONE; (void)controller_request(REQ_STOP,0); dbg_motor_disarm(); dbg_coupled_clear(); line("ABORTED"); }

static void finish_prompt(void) { input[input_len]='\0'; if(prompt==PROMPT_ARM){if(!strcmp(input,"UNCOUPLED")){armed=true;coupled=false;arm_deadline=ms_now()+120000u;(void)post(DBG_OP_ENTER,DIR_STOP,0,0,false);line("manual drive armed");}else line("arming cancelled");}else if(prompt==PROMPT_COUPLED){if(!strcmp(input,"COUPLED")){coupled=true;armed=false;coupled_deadline=ms_now()+120000u;line("coupled motion confirmed");}else line("coupled confirmation cancelled");}else if(prompt==PROMPT_CLEAR){if(!strcmp(input,"CLEAR")){dbg_request_t r={.op=DBG_OP_FAULT_CLEAR};(void)controller_debug_request(&r);line("fault flag cleared; home required");}else line("fault clear cancelled");}
else if(prompt==PROMPT_RATE){long v=strtol(input,NULL,10);dbg_stream_set_rate((uint16_t)v);}
else if(prompt==PROMPT_DUTY){long v=strtol(input,NULL,10);if(v>=0&&v<=255){pulse_duty=(uint8_t)v;line(v<DUTY_MIN?"pulse duty updated; below DUTY_MIN":"pulse duty updated");}else line("pulse duty must be 0..255");}
else if(prompt==PROMPT_DURATION){long v=strtol(input,NULL,10);if(v>=10&&v<=2000){pulse_ms=(uint16_t)v;line("pulse duration updated");}else line("pulse duration must be 10..2000 ms");}
else if(prompt==PROMPT_CFG_KEY){strncpy(cfg_key,input,sizeof cfg_key);cfg_key[sizeof cfg_key-1]=0;line("value:");prompt=PROMPT_CFG_VALUE;input_len=0;return;}
else if(prompt==PROMPT_CFG_VALUE){long v=strtol(input,NULL,10);line(dbg_cfg_set(cfg_key,v)?"configuration override applied":"configuration override rejected");}
prompt=PROMPT_NONE;input_len=0; }
void dbg_handle_key(char c) { if(action!=ACT_NONE){ if(action==ACT_CAL_POS_WAIT&&c==' '){action=ACT_CAL_POS_SAMPLE;action_started=ms_now();action_deadline=action_started+500u;action_sum=action_count=0;action_min=4095;action_max=0;return;} dbg_abort(); return; } if(prompt!=PROMPT_NONE){if(c=='\r')return;if(c=='\n'){finish_prompt();return;}if(input_len<CONSOLE_LINE_MAX)input[input_len++]=c;return;} activity(); if(c=='x'){dbg_exit();return;}if(c=='?'){dbg_render();return;}if(c=='q'){menu=MENU_ROOT;dbg_render();return;}if(menu==MENU_ROOT&&c>='1'&&c<='7'){menu=(menu_t)(c-'0');dbg_render();return;}switch(menu){case MENU_STATUS:if(c=='s')dbg_status_dump();else if(c=='t')dbg_stream_toggle();else if(c=='r'){line("rate Hz (1..50):");prompt=PROMPT_RATE;input_len=0;}else if(c=='k')dbg_timing_stats();else if(c=='z')dbg_timing_reset();break;case MENU_ADC:if(c=='a')dbg_adc_read_once();else if(c=='m')dbg_adc_monitor_toggle();else if(c=='c')dbg_adc_capture_toggle();else if(c=='b')dbg_band_table();else if(c=='e')dbg_band_margin();break;case MENU_MOTOR:if(c=='A'){if(armed)dbg_motor_disarm();else dbg_motor_arm();}else if(c=='f')dbg_motor_pulse(DIR_FWD,pulse_duty,pulse_ms);else if(c=='v')dbg_motor_pulse(DIR_REV,pulse_duty,pulse_ms);else if(c=='d'){line("pulse duty (0..255):");prompt=PROMPT_DUTY;input_len=0;}else if(c=='t'){line("pulse duration ms (10..2000):");prompt=PROMPT_DURATION;input_len=0;}else if(c=='b')dbg_motor_brake();else if(c=='c')dbg_motor_coast();else if(c=='s')dbg_motor_standby(true);else if(c=='n')dbg_motor_find_min(DIR_FWD);break;case MENU_CAL:if(c=='p')dbg_cal_positions();else if(c=='s')dbg_cal_step_time();else if(c=='w')dbg_cal_travel_time();else if(c=='o')dbg_cal_overshoot();else if(c=='r')dbg_cal_report();break;case MENU_CFG:if(c=='l')dbg_cfg_list();else if(c=='s'){line("key:");prompt=PROMPT_CFG_KEY;input_len=0;}else if(c=='d')dbg_cfg_reset();else if(c=='e')dbg_cfg_export();break;case MENU_FAULT:if(c=='f')dbg_fault_show();else if(c=='h')dbg_history_dump();else if(c=='c')dbg_counters_show();else if(c=='z')dbg_counters_reset();else if(c=='k')dbg_fault_clear();break;case MENU_TEST:if(c=='s')dbg_selftest_static();else if(c=='m')dbg_selftest_motion();break;default:break;} }
void dbg_poll(void) { uint32_t n=ms_now(); action_poll(n);if(armed&&(int32_t)(n-arm_deadline)>=0)dbg_motor_disarm();if(coupled&&(int32_t)(n-coupled_deadline)>=0)dbg_coupled_clear();if(streaming&&(int32_t)(n-next_stream)>=0){char b[128];snprintf(b,sizeof b,"T %lu %s %u %u %s %u %u %u",(unsigned long)n,state_text(controller_state()),controller_position(),controller_target(),dir_text(motor_direction()),motor_duty(),encoder_raw(),encoder_average());line(b);next_stream=n+1000u/stream_hz;}if(monitoring&&(int32_t)(n-next_stream)>=0){dbg_adc_read_once();next_stream=n+100;}if(capturing){uint16_t v=encoder_average();if(!capture_samples||v<capture_min)capture_min=v;if(!capture_samples||v>capture_max)capture_max=v;capture_samples++;} }

void dbg_status_dump(void){char b[160];snprintf(b,sizeof b,"state %s pos %u target %u dir %s duty %u deadline %lu lastdir %s avg %u armed %s uptime %lu",state_text(controller_state()),controller_position(),controller_target(),dir_text(motor_direction()),motor_duty(),(unsigned long)controller_deadline_ms(),dir_text(controller_last_direction()),encoder_average(),armed?"YES":"NO",(unsigned long)ms_now());line(b);}
void dbg_stream_toggle(void){streaming=!streaming;next_stream=ms_now();line(streaming?"telemetry started":"telemetry stopped");}void dbg_stream_set_rate(uint16_t hz){if(hz>=1&&hz<=50)stream_hz=hz;line(hz>=1&&hz<=50?"stream rate updated":"stream rate must be 1..50");}void dbg_timing_stats(void){tick_stats_t s;char b[128];controller_timing_get(&s);snprintf(b,sizeof b,"TIMING min=%lu max=%lu mean=%llu overruns=%lu",(unsigned long)s.min_us,(unsigned long)s.max_us,s.count?(unsigned long long)(s.sum_us/s.count):0ull,(unsigned long)s.overruns);line(b);}void dbg_timing_reset(void){controller_timing_reset();line("timing statistics reset");}
void dbg_adc_read_once(void){char b[96];snprintf(b,sizeof b,"ADC raw=%u avg=%u band=%u confirmed=%s",encoder_raw(),encoder_average(),encoder_instant(),encoder_confirmed()==encoder_instant()?"YES":"NO");line(b);}void dbg_adc_monitor_toggle(void){monitoring=!monitoring;next_stream=ms_now();line(monitoring?"ADC monitor started":"ADC monitor stopped");}void dbg_adc_capture_toggle(void){capturing=!capturing;if(capturing){capture_samples=0;capture_min=4095;capture_max=0;line("ADC capture started");}else{char b[160];uint16_t lo,hi;position_t p=controller_position();band_limits(p,&lo,&hi);uint16_t margin=(capture_min>lo?capture_min-lo:0)<(hi>capture_max?hi-capture_max:0)?(capture_min>lo?capture_min-lo:0):(hi>capture_max?hi-capture_max:0);uint16_t width=hi-lo+1u;snprintf(b,sizeof b,"CAPTURE pos=%u samples=%lu min=%u max=%u ripple=%u band=%u..%u margin=%u/%u%%",p,(unsigned long)capture_samples,capture_min,capture_max,capture_max-capture_min,lo,hi,margin,(unsigned)(margin*100u/width));line(b);}}void dbg_band_table(void){char b[128];snprintf(b,sizeof b,"BANDS 1=0..%u 2=%u..%u 3=%u..%u 4=%u..%u 5=%u..%u",CFG_BAND_P1_MAX,CFG_BAND_P1_MAX+1,CFG_BAND_P2_MAX,CFG_BAND_P2_MAX+1,CFG_BAND_P3_MAX,CFG_BAND_P3_MAX+1,CFG_BAND_P4_MAX,CFG_BAND_P4_MAX+1,CFG_BAND_P5_MAX);line(b);}void dbg_band_margin(void){char b[112];uint16_t lo,hi,v=encoder_average();position_t p=encoder_instant();band_limits(p,&lo,&hi);uint16_t margin=(v-lo)<(hi-v)?v-lo:hi-v;snprintf(b,sizeof b,"BAND MARGIN pos=%u value=%u edge_distance=%u width=%u percent=%u",p,v,margin,hi-lo+1u,(unsigned)(margin*100u/(hi-lo+1u)));line(b);}
bool dbg_motor_arm(void){line("Manual drive bypasses position limits.\r\nThe mechanism has NO physical end-stops.\r\nType UNCOUPLED to confirm the motor is disconnected:");prompt=PROMPT_ARM;return false;}void dbg_motor_disarm(void){armed=false;(void)post(DBG_OP_EXIT,DIR_STOP,0,0,false);}bool dbg_motor_armed(void){return armed;}void dbg_motor_pulse(direction_t d,uint8_t duty,uint16_t ms){if(!armed){line("debug: not armed");return;}if(!post(DBG_OP_DRIVE,d,duty,ms,false))line("debug: busy");}void dbg_motor_brake(void){if(!post(DBG_OP_BRAKE,DIR_STOP,0,0,false))line("debug: brake request rejected");}void dbg_motor_coast(void){if(post(DBG_OP_COAST,DIR_STOP,0,0,false))line("motor coasting; mechanism may be moved by hand");}void dbg_motor_standby(bool on){(void)post(DBG_OP_STANDBY,DIR_STOP,0,0,on);}void dbg_motor_find_min(direction_t d){char b[48];if(!armed){line("debug: not armed");return;}snprintf(b,sizeof b,"FINDMIN dir=%s",dir_text(d));line(b);action=ACT_FINDMIN_BASE;action_stage=(uint8_t)d;action_duty=DUTY_MIN-20;action_started=ms_now();action_deadline=action_started+150u;action_min=4095;action_max=0;}
bool dbg_coupled_confirm(void){line("This test moves the mechanism under closed-loop control.\r\nPosition limits ARE enforced. The mechanism must be connected.\r\nType COUPLED to confirm:");prompt=PROMPT_COUPLED;return false;}void dbg_coupled_clear(void){coupled=false;}bool dbg_coupled(void){return coupled;}
void dbg_cal_positions(void){line("CAL POSITIONS\r\n Move to position 1, press SPACE (q to abort)");action=ACT_CAL_POS_WAIT;action_stage=1;}
void dbg_cal_step_time(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=3){line("CAL STEP requires position 3");return;}line("CAL STEP");action=ACT_CAL_STEP;action_stage=0;action_worst=0;action_started=0;}
void dbg_cal_travel_time(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=POS_MAX){line("travel calibration requires position 5");return;}line("CAL TRAVEL");action=ACT_CAL_TRAVEL;action_stage=0;action_started=0;}
void dbg_cal_overshoot(void){if(!coupled){dbg_coupled_confirm();return;}if(controller_position()!=2&&controller_position()!=4){line("CAL OVERSHOOT requires position 2 or 4");return;}line("CAL OVERSHOOT target=3 nominal=1309");action=ACT_CAL_OVER;action_stage=0;action_started=0;action_count=0;action_source=controller_position();action_saved_duty=cfg.duty_approach;action_duty=80;}
void dbg_cal_report(void){dbg_cfg_export();}
void dbg_cfg_list(void){dbg_cfg_export();}
bool dbg_cfg_set(const char *k,int32_t v){
    if(v<0)return false;
    if((!strncmp(k,"DUTY_",5)&&v>255)||(!strncmp(k,"BAND_",5)&&v>4094)||((!strcmp(k,"DEBOUNCE_MS")||!strcmp(k,"BRAKE_HOLD_MS"))&&v>65535))return false;
    cfg_t n; memcpy(&n,(const void *)&cfg,sizeof n);
    if(!strcmp(k,"DUTY_NORMAL"))n.duty_normal=v; else if(!strcmp(k,"DUTY_APPROACH"))n.duty_approach=v; else if(!strcmp(k,"DUTY_CREEP"))n.duty_creep=v; else if(!strcmp(k,"DUTY_MIN"))n.duty_min=v;
    else if(!strcmp(k,"BAND_P1_MAX"))n.band_p1_max=v; else if(!strcmp(k,"BAND_P2_MAX"))n.band_p2_max=v; else if(!strcmp(k,"BAND_P3_MAX"))n.band_p3_max=v; else if(!strcmp(k,"BAND_P4_MAX"))n.band_p4_max=v; else if(!strcmp(k,"BAND_P5_MAX"))n.band_p5_max=v;
    else if(!strcmp(k,"DEBOUNCE_MS"))n.debounce_ms=v; else if(!strcmp(k,"BRAKE_HOLD_MS"))n.brake_hold_ms=v; else if(!strcmp(k,"TIMEOUT_STEP_MS"))n.timeout_step_ms=v; else if(!strcmp(k,"TIMEOUT_HOME_MS"))n.timeout_home_ms=v; else if(!strcmp(k,"TIMEOUT_RECOVER_MS"))n.timeout_recover_ms=v; else return false;
    if(n.duty_min>n.duty_creep||n.duty_creep>n.duty_approach||n.duty_approach>n.duty_normal)return false;
    if(!(n.band_p1_max<n.band_p2_max&&n.band_p2_max<n.band_p3_max&&n.band_p3_max<n.band_p4_max&&n.band_p4_max<n.band_p5_max&&n.band_p5_max<4095))return false;
    if(n.debounce_ms==0||n.brake_hold_ms==0||n.timeout_step_ms<100||n.timeout_home_ms<100||n.timeout_recover_ms<100)return false;
    memcpy((void *)&cfg,&n,sizeof n);return true;
}
void dbg_cfg_reset(void){cfg_reset();line("configuration defaults restored");}
void dbg_cfg_export(void){char b[96];line("CONFIG EXPORT");
#define OUT(name,val) do{snprintf(b,sizeof b,"#define " name " %lu",(unsigned long)(val));line(b);}while(0)
OUT("DUTY_NORMAL",cfg.duty_normal);OUT("DUTY_APPROACH",cfg.duty_approach);OUT("DUTY_CREEP",cfg.duty_creep);OUT("DUTY_MIN",cfg.duty_min);OUT("BAND_P1_MAX",cfg.band_p1_max);OUT("BAND_P2_MAX",cfg.band_p2_max);OUT("BAND_P3_MAX",cfg.band_p3_max);OUT("BAND_P4_MAX",cfg.band_p4_max);OUT("BAND_P5_MAX",cfg.band_p5_max);OUT("DEBOUNCE_MS",cfg.debounce_ms);OUT("BRAKE_HOLD_MS",cfg.brake_hold_ms);OUT("TIMEOUT_STEP_MS",cfg.timeout_step_ms);OUT("TIMEOUT_HOME_MS",cfg.timeout_home_ms);OUT("TIMEOUT_RECOVER_MS",cfg.timeout_recover_ms);
#undef OUT
}
void dbg_fault_show(void){fault_record_t f;char b[160];controller_fault_get(&f);snprintf(b,sizeof b,"FAULT kind=%u ms=%lu state=%s pos=%u target=%u deadline=%lu",f.kind,(unsigned long)f.ms,state_text(f.state),f.pos,f.target,(unsigned long)f.deadline_ms);line(b);}void dbg_history_dump(void){line("HISTORY (newest last)");for(uint8_t i=0;i<controller_history_count();++i){hist_entry_t h;char b[64];if(controller_history_get(i,&h)){const char *k=h.kind==0?"PASS":h.kind==1?"ARR":"UNKNOWN";snprintf(b,sizeof b,"  %lu  %s %u",(unsigned long)h.ms,k,h.pos);line(b);}}}void dbg_counters_show(void){dbg_counters_t c;char b[240];controller_counters_get(&c);snprintf(b,sizeof b,"COUNTERS moves_ok=%lu moves_timeout=%lu recover_entered=%lu recover_ok=%lu faults=%lu limit_rejects=%lu pass_events=%lu tick_overruns=%lu",(unsigned long)c.moves_ok,(unsigned long)c.moves_timeout,(unsigned long)c.recover_entered,(unsigned long)c.recover_ok,(unsigned long)c.faults,(unsigned long)c.limit_rejects,(unsigned long)c.pass_events,(unsigned long)c.tick_overruns);line(b);}void dbg_counters_reset(void){controller_counters_reset();line("counters reset");}void dbg_fault_clear(void){line("Clearing a fault does not restore position.\r\nA home sequence will still be required before any move.\r\nType CLEAR to confirm:");prompt=PROMPT_CLEAR;}
bool dbg_selftest_static(void){bool ok=CFG_BAND_P1_MAX<CFG_BAND_P2_MAX&&CFG_BAND_P2_MAX<CFG_BAND_P3_MAX&&CFG_BAND_P3_MAX<CFG_BAND_P4_MAX&&CFG_BAND_P4_MAX<CFG_BAND_P5_MAX&&CFG_DUTY_MIN<=CFG_DUTY_CREEP&&CFG_DUTY_CREEP<=CFG_DUTY_APPROACH&&CFG_DUTY_APPROACH<=CFG_DUTY_NORMAL&&CFG_TIMEOUT_HOME_MS>=4u*CFG_TIMEOUT_STEP_MS;line(ok?"SELFTEST STATIC PASS":"SELFTEST STATIC FAIL");return ok;}bool dbg_selftest_motion(void){if(!coupled){dbg_coupled_confirm();return false;}line("SELFTEST MOTION");action=ACT_SELFTEST;action_stage=0;action_started=0;return true;}

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
    if (action == ACT_FINDMIN_BASE) {
        action_sample_adc();
        if ((int32_t)(now - action_deadline) < 0) return;
        noise_floor = action_max - action_min;
        action_start_adc = encoder_average();
        dbg_motor_pulse((direction_t)action_stage, action_duty, 150);
        action_started = now; action_deadline = now + 150u; action_min = 4095; action_max = 0;
        action = ACT_FINDMIN_PULSE;
        return;
    }
    if (action == ACT_FINDMIN_PULSE) {
        action_sample_adc();
        if ((int32_t)(now - action_deadline) < 0) return;
        uint16_t change = encoder_average() > action_start_adc ? encoder_average() - action_start_adc : action_start_adc - encoder_average();
        bool moved = change > noise_floor || (action_max - action_min) > noise_floor;
        snprintf(b,sizeof b,moved?"  duty %u  MOTION (adc %u -> %u)":"  duty %u  no motion",action_duty,action_start_adc,encoder_average()); line(b);
        if (moved) { uint16_t suggested=(uint16_t)((action_duty*11u+9u)/10u);snprintf(b,sizeof b,"FINDMIN result=%u  suggest DUTY_MIN=%u (result +10%% margin)",action_duty,suggested);line(b);action=ACT_NONE;return; }
        if (action_duty >= 120u) { line("FINDMIN stopped at duty 120 without motion"); action=ACT_NONE; return; }
        action_duty += 5u; action_start_adc=encoder_average();dbg_motor_pulse((direction_t)action_stage,action_duty,150);action_deadline=now+150u;action_min=4095;action_max=0;return;
    }
    if (action == ACT_CAL_POS_SAMPLE) {
        action_sample_adc();
        if ((int32_t)(now-action_deadline)<0) return;
        action_means[action_stage-1u]=(uint16_t)(action_sum/action_count);
        snprintf(b,sizeof b,"  pos%u mean=%lu spread=%u",action_stage,(unsigned long)(action_sum/action_count),action_max-action_min);line(b);
        if (action_stage < 5u) { ++action_stage; snprintf(b,sizeof b," Move to position %u, press SPACE",action_stage);line(b);action=ACT_CAL_POS_WAIT; }
        else if (action_stage == 5u) { ++action_stage; line(" Move between reeds, press SPACE"); action=ACT_CAL_POS_WAIT; }
        else { cfg.band_p1_max=(action_means[0]+action_means[1])/2u;cfg.band_p2_max=(action_means[1]+action_means[2])/2u;cfg.band_p3_max=(action_means[2]+action_means[3])/2u;cfg.band_p4_max=(action_means[3]+action_means[4])/2u;cfg.band_p5_max=(action_means[4]+action_means[5])/2u;line("COMPUTED BANDS");dbg_band_table();for(uint8_t i=1;i<6u;++i)if((uint16_t)(action_means[i]-action_means[i-1u])<200u)line("  MARGINAL adjacent separation under 200");action=ACT_NONE; }
        return;
    }
    if (action == ACT_CAL_STEP) {
        if (action_stage >= 4u) { uint32_t suggest=((action_worst*2u+99u)/100u)*100u;snprintf(b,sizeof b,"  worst=%lu  suggest TIMEOUT_STEP_MS=%lu (worst x2, rounded up)",(unsigned long)action_worst,(unsigned long)suggest);line(b);action=ACT_NONE;return; }
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
        static const uint8_t duties[] = {80,80,60,60,40,40};
        static const position_t sources[] = {2,4,2,4,2,4};
        if (action_stage >= 6u) { cfg.duty_approach=action_saved_duty; line("  suggest DUTY_APPROACH=60"); action=ACT_NONE; return; }
        position_t source=sources[action_stage]; action_duty=duties[action_stage];
        if (controller_state()==ST_FAULT) { cfg.duty_approach=action_saved_duty;line("CAL OVERSHOOT failed");action=ACT_NONE;return; }
        if (action_count==0u && controller_position()!=source) { if(controller_state()==ST_IDLE&&controller_request(REQ_MOVE,source)==MOVE_OK)action_count=1u;return; }
        if (action_count==1u) { if(controller_state()==ST_IDLE&&controller_position()==source)action_count=0u;else return; }
        if (!action_started) { cfg.duty_approach=action_duty;if(controller_request(REQ_MOVE,3)==MOVE_OK)action_started=now;return; }
        if(controller_state()==ST_IDLE&&controller_position()==3){int32_t off=(int32_t)encoder_average()-1309;snprintf(b,sizeof b,"  duty %u  settle=%u  offset=%+ld",action_duty,encoder_average(),(long)off);line(b);++action_stage;action_started=0;}
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
