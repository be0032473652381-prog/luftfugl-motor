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

#define DUTY_NORMAL 200
#define DUTY_APPROACH 60
#define DUTY_CREEP 50
#define DUTY_MIN 45

#define BAND_P1_MAX 555
#define BAND_P2_MAX 1023
#define BAND_P3_MAX 1678
#define BAND_P4_MAX 2431
#define BAND_P5_MAX 3455

#define TICK_HZ 1000
#define TICK_PERIOD_US 1000u
#define FILTER_DEPTH 5
#define DEBOUNCE_MS 12
#define BRAKE_HOLD_MS 100
#define TIMEOUT_STEP_MS 1500
#define TIMEOUT_HOME_MS 6000
#define TIMEOUT_RECOVER_MS 2000

#define CONSOLE_LINE_MAX 32
#define EVENT_QUEUE_DEPTH 8
#define UART_BAUD 115200

#define ADC_MAX_VALUE 4095u
#ifdef LUFTFUGL_DEBUG
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
#define DEBUG_SELFTEST_ADC_SAMPLES 10u
#define DEBUG_SELFTEST_SAMPLE_PERIOD_MS 1u
#define DEBUG_SELFTEST_WINDOW_MS 1000u
#define DEBUG_SELFTEST_TICK_TOLERANCE 20u
#define DEBUG_PERCENT_SCALE 100u
#define DEBUG_BENCH_PIN_PERIOD_MS 200u
#define DEBUG_GPIO_WALK_STEP_MS 1000u
#define DEBUG_TICK_HEALTH_WINDOW_MS 2000u
#define DEBUG_SIM_DEFAULT_ADC 372u
#define DEBUG_SIM_OPEN_ADC ADC_MAX_VALUE
#define DEBUG_SIM_NOMINAL_P1 372u
#define DEBUG_SIM_NOMINAL_P2 738u
#define DEBUG_SIM_NOMINAL_P3 1309u
#define DEBUG_SIM_NOMINAL_P4 2047u
#define DEBUG_SIM_NOMINAL_P5 2815u
#define DEBUG_SIM_MIN_BAND_MS DEBOUNCE_MS
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
#define DEBUG_SIM_EXPECTED_REGIONS 6u
#endif

#define POS_UNKNOWN 0
#define POS_MIN 1
#define POS_MAX 5
typedef uint8_t position_t;

typedef enum { DIR_STOP = 0, DIR_FWD, DIR_REV } direction_t;
typedef enum {
    ST_BOOT = 0, ST_IDLE, ST_MOVING, ST_APPROACH, ST_HOMING, ST_RECOVER,
    ST_FAULT,
#ifdef LUFTFUGL_DEBUG
    ST_DEBUG,
#endif
} sys_state_t;
typedef enum {
    REQ_NONE = 0, REQ_MOVE, REQ_STOP, REQ_HOME
} request_kind_t;
typedef enum {
    MOVE_OK = 0, MOVE_ALREADY, MOVE_INVALID, MOVE_ENDSTOP, MOVE_BUSY,
    MOVE_POS_UNKNOWN, MOVE_FAULT
} move_result_t;
typedef enum {
    EV_PASS = 0, EV_ARRIVE, EV_TIMEOUT, EV_FAULT_HOME, EV_FAULT_RECOVER,
    EV_HOMING, EV_STOPPED_UNKNOWN
} event_kind_t;

#ifdef LUFTFUGL_DEBUG
typedef enum { DBG_OP_NONE = 0, DBG_OP_ENTER, DBG_OP_EXIT, DBG_OP_DRIVE, DBG_OP_BRAKE, DBG_OP_COAST, DBG_OP_STANDBY, DBG_OP_FAULT_CLEAR, DBG_OP_SIM_ENABLE, DBG_OP_GPIO_SET } dbg_op_t;
typedef struct { dbg_op_t op; direction_t dir; uint8_t duty; uint16_t ms; bool flag; } dbg_request_t;

typedef struct {
    uint8_t duty_normal, duty_approach, duty_creep, duty_min;
    uint16_t band_p1_max, band_p2_max, band_p3_max, band_p4_max, band_p5_max;
    uint16_t debounce_ms, brake_hold_ms;
    uint32_t timeout_step_ms, timeout_home_ms, timeout_recover_ms;
} cfg_t;
extern volatile cfg_t cfg;
void cfg_reset(void);
#define CFG_DUTY_NORMAL (cfg.duty_normal)
#define CFG_DUTY_APPROACH (cfg.duty_approach)
#define CFG_DUTY_CREEP (cfg.duty_creep)
#define CFG_DUTY_MIN (cfg.duty_min)
#define CFG_BAND_P1_MAX (cfg.band_p1_max)
#define CFG_BAND_P2_MAX (cfg.band_p2_max)
#define CFG_BAND_P3_MAX (cfg.band_p3_max)
#define CFG_BAND_P4_MAX (cfg.band_p4_max)
#define CFG_BAND_P5_MAX (cfg.band_p5_max)
#define CFG_DEBOUNCE_MS (cfg.debounce_ms)
#define CFG_BRAKE_HOLD_MS (cfg.brake_hold_ms)
#define CFG_TIMEOUT_STEP_MS (cfg.timeout_step_ms)
#define CFG_TIMEOUT_HOME_MS (cfg.timeout_home_ms)
#define CFG_TIMEOUT_RECOVER_MS (cfg.timeout_recover_ms)
#else
#define CFG_DUTY_NORMAL DUTY_NORMAL
#define CFG_DUTY_APPROACH DUTY_APPROACH
#define CFG_DUTY_CREEP DUTY_CREEP
#define CFG_DUTY_MIN DUTY_MIN
#define CFG_BAND_P1_MAX BAND_P1_MAX
#define CFG_BAND_P2_MAX BAND_P2_MAX
#define CFG_BAND_P3_MAX BAND_P3_MAX
#define CFG_BAND_P4_MAX BAND_P4_MAX
#define CFG_BAND_P5_MAX BAND_P5_MAX
#define CFG_DEBOUNCE_MS DEBOUNCE_MS
#define CFG_BRAKE_HOLD_MS BRAKE_HOLD_MS
#define CFG_TIMEOUT_STEP_MS TIMEOUT_STEP_MS
#define CFG_TIMEOUT_HOME_MS TIMEOUT_HOME_MS
#define CFG_TIMEOUT_RECOVER_MS TIMEOUT_RECOVER_MS
#endif

#endif
