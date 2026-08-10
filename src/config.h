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
#define FILTER_DEPTH 5
#define DEBOUNCE_MS 12
#define BRAKE_HOLD_MS 100
#define TIMEOUT_STEP_MS 1500
#define TIMEOUT_HOME_MS 6000
#define TIMEOUT_RECOVER_MS 2000

#define CONSOLE_LINE_MAX 32
#define EVENT_QUEUE_DEPTH 8
#define UART_BAUD 115200

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
    EV_HOMING
} event_kind_t;

#ifdef LUFTFUGL_DEBUG
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
