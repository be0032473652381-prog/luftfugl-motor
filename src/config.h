#ifndef LUFTFUGL_CONFIG_H
#define LUFTFUGL_CONFIG_H

#include <stdint.h>

#define FW_VERSION "2.0.0"

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
  ST_FAULT
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

#endif
