#ifndef LUFTFUGL_CONFIG_H
#define LUFTFUGL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION "1.0.0"

#define PIN_AIN1 2
#define PIN_AIN2 3
#define PIN_PWMA 14
#define PIN_STBY 15
#define PIN_SENSE 26
#define ADC_CHANNEL 0
#define PIN_UART_TX 0
#define PIN_UART_RX 1

#define PWM_WRAP 255
#define PWM_CLKDIV 97.6875f

#define DUTY_NORMAL 60
#define DUTY_APPROACH 30
#define DUTY_CREEP 25
#define DUTY_MIN 25

#define POS_1_ADC 372
#define POS_2_ADC 738
#define POS_3_ADC 1309
#define POS_4_ADC 2047
#define POS_5_ADC 2815
#define POS_WINDOW 80
#define APPROACH_COUNTS 200
#define ADC_SAFE_MIN 272
#define ADC_SAFE_MAX 2915
#define STALL_DELTA 8
#define STALL_WINDOW_MS 300
#define REVERSE_DELTA 30

#define TICK_HZ 1000
#define TICK_PERIOD_US 1000u
#define FILTER_DEPTH 5
#define DEBOUNCE_MS 12
#define BRAKE_HOLD_MS 100
#define TIMEOUT_STEP_MS 1500
#define TIMEOUT_HOME_MS 6000
#define JOG_MIN_COUNTS 10
#define JOG_MAX_COUNTS 500
#define JOG_TIMEOUT_MS 3000

#define CONSOLE_LINE_MAX 32
#define EVENT_QUEUE_DEPTH 8
#define UART_BAUD 115200

#define ADC_MAX_VALUE 4095u
#ifdef LUFTFUGL_MONITOR
#define DEBUG_INTERLOCK_TIMEOUT_MS 120000u
#define DEBUG_STREAM_MIN_HZ 1u
#define DEBUG_STREAM_DEFAULT_HZ 10u
#define DEBUG_STREAM_MAX_HZ 50u
#define DEBUG_ADC_MONITOR_PERIOD_MS 100u
#define DEBUG_PULSE_DEFAULT_MS 150u
#define DEBUG_PULSE_MIN_MS 10u
#define DEBUG_PULSE_MAX_MS 2000u
#define DEBUG_FINDMIN_DUTY_OFFSET 20u
#define DEBUG_FINDMIN_DUTY_STEP 5u
#define DEBUG_FINDMIN_PULSE_MS 150u
#define DEBUG_FINDMIN_DUTY_MAX 120u
#define DEBUG_FINDMIN_MARGIN_PERCENT 10u
#define DEBUG_CAL_SAMPLE_MS 500u
#define DEBUG_CAL_MIN_SEPARATION 200u
#define DEBUG_CAL_ROUND_MS 100u
#define DEBUG_CFG_TIMEOUT_MIN_MS 100u
#define DEBUG_OVERSHOOT_DUTY_HIGH 80u
#define DEBUG_OVERSHOOT_DUTY_MID 60u
#define DEBUG_OVERSHOOT_DUTY_LOW 40u
#define DEBUG_OVERSHOOT_TEST_COUNT 6u
#define DEBUG_NOMINAL_P3 1309u
#define DEBUG_HISTORY_DEPTH 16u
#define DEBUG_OUT_BUFFER 512u
#define DEBUG_SELFTEST_ADC_SAMPLES 10u
#define DEBUG_SELFTEST_SAMPLE_PERIOD_MS 1u
#define DEBUG_SELFTEST_WINDOW_MS 1000u
#define DEBUG_SELFTEST_TICK_TOLERANCE 20u
#define DEBUG_PERCENT_SCALE 100u
#define DEBUG_BENCH_PIN_PERIOD_MS 200u
#define DEBUG_GPIO_WALK_STEP_MS 1000u
#define DEBUG_TICK_HEALTH_WINDOW_MS 2000u
#define DEBUG_SIM_DEFAULT_ADC POS_1_ADC
#define DEBUG_SIM_MIN_POSITION_MS DEBOUNCE_MS
#define DEBUG_SIM_SWEEP_SETTLE_MS (FILTER_DEPTH + DEBOUNCE_MS)
#define DEBUG_MENU_BUFFER_SIZE 512u
#define DEBUG_HEADER_BUFFER_SIZE 256u
#define DEBUG_PWM_DIV_MASK 0x0fffu
#define DEBUG_PWM_FIXED_SCALE 16u
#define DEBUG_FREQ_TENTHS 10u
#define DEBUG_DECIMAL_4_SCALE 10000u
#define DEBUG_PWM_SPEC_HZ 5000u
#define DEBUG_PWM_TOLERANCE_PERCENT 1u
#define DEBUG_TICK_RATE_TOLERANCE_HZ 20u
#define DEBUG_GPIO_OP_ALL_LOW 0u
#define DEBUG_GPIO_OP_AIN1 1u
#define DEBUG_GPIO_OP_AIN2 2u
#define DEBUG_GPIO_OP_STBY 3u
#define DEBUG_SCREEN_REFRESH_MS 200u
#define DEBUG_SCREEN_UPTIME_MS 1000u
#define DEBUG_JOG_STEP_DEFAULT 100u
#define DEBUG_JOG_STEP_1 10u
#define DEBUG_JOG_STEP_2 25u
#define DEBUG_JOG_STEP_3 50u
#define DEBUG_JOG_STEP_4 100u
#define DEBUG_JOG_STEP_5 250u
#define DEBUG_JOG_STEP_6 500u
#endif

#define POS_UNKNOWN 0
#define POS_MIN 1
#define POS_MAX 5
#define POS_BETWEEN 6
typedef uint8_t position_t;

typedef enum { DIR_STOP = 0, DIR_FWD, DIR_REV } direction_t;
typedef enum {
  ST_BOOT = 0,
  ST_IDLE,
  ST_MOVING,
  ST_APPROACH,
  ST_HOMING,
  ST_FAULT,
#ifdef LUFTFUGL_DEBUG
  ST_DEBUG,
#endif
} sys_state_t;
typedef enum {
  REQ_NONE = 0,
  REQ_MOVE,
  REQ_STOP,
  REQ_HOME,
  REQ_JOG,
  REQ_SETPOS,
  REQ_RESET_POSITIONS
} request_kind_t;
typedef enum {
  MOVE_OK = 0,
  MOVE_ALREADY,
  MOVE_INVALID,
  MOVE_ENDSTOP,
  MOVE_BUSY,
  MOVE_POS_UNKNOWN,
  MOVE_FAULT
} move_result_t;
typedef enum {
  JOG_OK = 0,
  JOG_INVALID,
  JOG_ENDSTOP,
  JOG_OVERTRAVEL,
  JOG_BUSY,
  JOG_FAULT
} jog_result_t;
typedef enum {
  EV_PASS = 0,
  EV_ARRIVE,
  EV_TIMEOUT,
  EV_FAULT_HOME,
  EV_HOMING,
  EV_STOPPED_UNKNOWN,
  EV_FAULT_OVERTRAVEL,
  EV_FAULT_STALL,
  EV_FAULT_DIRECTION,
  EV_JOG_COMPLETE
} event_kind_t;

#ifdef LUFTFUGL_MONITOR
typedef enum {
  DBG_OP_NONE = 0,
  DBG_OP_ENTER,
  DBG_OP_EXIT,
  DBG_OP_DRIVE,
  DBG_OP_BRAKE,
  DBG_OP_COAST,
  DBG_OP_STANDBY,
  DBG_OP_FAULT_CLEAR,
  DBG_OP_SIM_ENABLE,
  DBG_OP_SIM_SET,
  DBG_OP_GPIO_SET,
  DBG_OP_GOTO_ADC
} dbg_op_t;
typedef struct {
  dbg_op_t op;
  direction_t dir;
  uint8_t duty;
  uint16_t ms;
  bool flag;
  uint16_t adc;
} dbg_request_t;

typedef struct {
  uint8_t duty_normal, duty_approach, duty_creep, duty_min;
  uint16_t pos_1_adc, pos_2_adc, pos_3_adc, pos_4_adc, pos_5_adc;
  uint16_t pos_window, approach_counts, adc_safe_min, adc_safe_max;
  uint16_t stall_delta, stall_window_ms, reverse_delta;
  uint16_t debounce_ms, brake_hold_ms;
  uint32_t timeout_step_ms, timeout_home_ms;
} cfg_t;
extern volatile cfg_t cfg;
void cfg_reset(void);
#define CFG_DUTY_NORMAL (cfg.duty_normal)
#define CFG_DUTY_APPROACH (cfg.duty_approach)
#define CFG_DUTY_CREEP (cfg.duty_creep)
#define CFG_DUTY_MIN (cfg.duty_min)
#define CFG_POS_1_ADC (cfg.pos_1_adc)
#define CFG_POS_2_ADC (cfg.pos_2_adc)
#define CFG_POS_3_ADC (cfg.pos_3_adc)
#define CFG_POS_4_ADC (cfg.pos_4_adc)
#define CFG_POS_5_ADC (cfg.pos_5_adc)
#define CFG_POS_WINDOW (cfg.pos_window)
#define CFG_APPROACH_COUNTS (cfg.approach_counts)
#define CFG_ADC_SAFE_MIN (cfg.adc_safe_min)
#define CFG_ADC_SAFE_MAX (cfg.adc_safe_max)
#define CFG_STALL_DELTA (cfg.stall_delta)
#define CFG_STALL_WINDOW_MS (cfg.stall_window_ms)
#define CFG_REVERSE_DELTA (cfg.reverse_delta)
#define CFG_DEBOUNCE_MS (cfg.debounce_ms)
#define CFG_BRAKE_HOLD_MS (cfg.brake_hold_ms)
#define CFG_TIMEOUT_STEP_MS (cfg.timeout_step_ms)
#define CFG_TIMEOUT_HOME_MS (cfg.timeout_home_ms)
#else
#define CFG_DUTY_NORMAL DUTY_NORMAL
#define CFG_DUTY_APPROACH DUTY_APPROACH
#define CFG_DUTY_CREEP DUTY_CREEP
#define CFG_DUTY_MIN DUTY_MIN
#define CFG_POS_1_ADC POS_1_ADC
#define CFG_POS_2_ADC POS_2_ADC
#define CFG_POS_3_ADC POS_3_ADC
#define CFG_POS_4_ADC POS_4_ADC
#define CFG_POS_5_ADC POS_5_ADC
#define CFG_POS_WINDOW POS_WINDOW
#define CFG_APPROACH_COUNTS APPROACH_COUNTS
#define CFG_ADC_SAFE_MIN ADC_SAFE_MIN
#define CFG_ADC_SAFE_MAX ADC_SAFE_MAX
#define CFG_STALL_DELTA STALL_DELTA
#define CFG_STALL_WINDOW_MS STALL_WINDOW_MS
#define CFG_REVERSE_DELTA REVERSE_DELTA
#define CFG_DEBOUNCE_MS DEBOUNCE_MS
#define CFG_BRAKE_HOLD_MS BRAKE_HOLD_MS
#define CFG_TIMEOUT_STEP_MS TIMEOUT_STEP_MS
#define CFG_TIMEOUT_HOME_MS TIMEOUT_HOME_MS
#endif

#endif
