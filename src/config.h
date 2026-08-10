#ifndef LUFTFUGL_CONFIG_H
#define LUFTFUGL_CONFIG_H

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
    REQ_NONE = 0, REQ_MOVE, REQ_STOP, REQ_HOME,
#ifdef LUFTFUGL_DEBUG
    REQ_DEBUG_ENTER, REQ_DEBUG_DRIVE, REQ_DEBUG_EXIT,
#endif
} request_kind_t;
typedef enum {
    MOVE_OK = 0, MOVE_ALREADY, MOVE_INVALID, MOVE_ENDSTOP, MOVE_BUSY,
    MOVE_POS_UNKNOWN, MOVE_FAULT
} move_result_t;
typedef enum {
    EV_PASS = 0, EV_ARRIVE, EV_TIMEOUT, EV_FAULT_HOME, EV_FAULT_RECOVER,
    EV_HOMING
} event_kind_t;

#endif
