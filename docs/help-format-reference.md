# Debug help reference output

These are host-rendered outputs from the actual C document renderer and live-help preparation functions. They are not UART captures. Configuration values use compiled defaults; the host battery snapshot is 1.500–5.500 V, warning 3.600 V, critical 3.300 V. Live values on the device may differ. Lines are wrapped to the firmware text width. Each response starts with a blank line below `Command >`.

## help (page 1)

```text

HELP — Shows examples, limits and plain-language notes.

SYNTAX    help [<command>]
          help <setting>

NOTE
one command name, or none
Example: help jog
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
Commands (use help <command> for its examples and limits):
help              diag              sel               jog
step              save              stations          limits
lowendstop        highendstop       export            reset
bootsel           move              pos               goto
home              stop              status            adc
angle             led               led brightness    led zone
buzzer            buzzer crips-1    buzzer crips-2    buzzer crips-3
buzzer crips-4    buzzer crips-5    buzzer tone-2     buzzer tone-3
page              selftest          tick              trace
pins              pwm               cfg               sim
cal               arm               disarm            drive
findmin           batt              batt raw          batt res
batt log          batt events       batt reset        batt sim
batt sim range    batt sim warning  batt sim critical batt chirp
batt chirp time   load              ina               ds3231
ds3231 start      ds3231 temp       ds3231 stop       ds3231 timer
ds3231 timeset    adc0offset        co2               co2living
co2sleeping       co2cfg            co2sim            co2limit
co2save           co2defaults       ready             serial
asc               offset            altitude          mode
sdc41             menu              clean             clear
plain             exit
```

## diag (page 1)

```text

DIAG — Shows temporary UART receive and main-loop timing counters.

SYNTAX    diag

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## sel (page 1)

```text

SEL — Chooses which station save will update.

SYNTAX    sel <1..6>

NOTE
station 1 to 6
Example: sel 3
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## jog (page 1)

```text

JOG — jog by an ADC count offset

SYNTAX    jog <-4095..+4095>

NOTE
-4095 to +4095 counts
Example: jog +2000
Creep speed only; 100 counts is roughly 7 degrees.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## step (page 1)

```text

STEP — Changes the suggested calibration step.

SYNTAX    step <10|25|100|250|500>

NOTE
10, 25, 100, 250 or 500
Example: step 250
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## save (page 1)

```text

SAVE — Without a number, saves the selected station.

SYNTAX    save [<1..6>]

NOTE
station 1 to 6
Example: save 3
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## stations (page 1)

```text

STATIONS — Shows stored readings and difference from now.

SYNTAX    stations

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## limits (page 1)

```text

LIMITS — show live movement limits

SYNTAX    limits

NOTE
read-only
One count is about 0.09 degrees; values come from the live configuration.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## lowendstop (page 1)

```text

LOWENDSTOP — set the lower potentiometer travel limit

SYNTAX    lowendstop=<0..4095>

INTERACTIONS, most consequential first
- Sets the lower potentiometer travel limit in RAM; it takes effect
  immediately.

NOTE
ADC 0 to 4095; must be below high end-stop and station 1
Example: lowendstop=100
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
active low end-stop 100; accepted range 0..4095, must remain below station 1
  (200) and the high end-stop
```

## highendstop (page 1)

```text

HIGHENDSTOP — set the upper potentiometer travel limit

SYNTAX    highendstop=<0..4095>

INTERACTIONS, most consequential first
- Sets the upper potentiometer travel limit in RAM; it takes effect
  immediately.

NOTE
ADC 0 to 4095; must be above low end-stop and at or above station 6
Example: highendstop=3000
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
active high end-stop 3000; accepted range 0..4095, must remain at or above
  station 6 (3000) and the low end-stop
```

## export (page 1)

```text

EXPORT — Prints values ready to paste into config.h.

SYNTAX    export

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## reset (page 1)

```text

RESET — Restarts from flash; prints resetting before rebooting.

SYNTAX    reset
          reset stations

NOTE
no arguments; reset stations restores calibration
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## bootsel (page 1)

```text

BOOTSEL — Restarts into the USB bootloader for recovery.

SYNTAX    bootsel

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## move (page 1)

```text

MOVE — Uses closed-loop position control.

SYNTAX    move <1..6>

NOTE
station 1 to 6
Example: move 2
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## pos (page 1)

```text

POS — move through the normal closed-loop controller path

SYNTAX    pos <1-6>

INTERACTIONS, most consequential first
- rejects an invalid station, unknown starting position, or a controller that
  is already moving
- no wrap-around; configured motion range is station 1 through station 6, with
  station 6 reserved for CO2 errors
- every station uses target-window braking and settled arrival confirmation;
  station 1 homing also uses directional crossing protection

NOTE
Alias for move; moves to one configured station through the normal closed-
  loop, filtered and limit-enforced controller path.
Example: pos 6. Station 1 to 6.
targets: 1=200, 2=611, 3=1022, 4=1433, 5=1844, 6=3000 ADC counts
target band is nominal +/-20 counts; sensing uses a 5-sample rolling average
the same station band must persist for 12 ms before arrival is confirmed
within 300 counts, duty changes from 50 to 25; limits use creep duty 25
```

## goto (page 1)

```text

GOTO — Moves directly to one raw ADC target; movement is not wrap-aware.

SYNTAX    goto <0..4095>

NOTE
ADC range 0 to 4095
Example: goto 4000
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## home (page 1)

```text

HOME — Returns to station 1 through the guarded home path.

SYNTAX    home

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## stop (page 1)

```text

STOP — Brakes immediately; a period works without Enter.

SYNTAX    stop

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## status (page 1)

```text

STATUS — Shows the full controller state.

SYNTAX    status

NOTE
Read-only.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## adc (page 1)

```text

ADC — Shows raw, filtered and classified sensing.

SYNTAX    adc

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## angle (page 1)

```text

ANGLE — Shows the filtered ADC reading converted to degrees.

SYNTAX    angle

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## led (page 1)

```text

LED — control pixel indication and power

SYNTAX    led
          led <on|off|auto>
          led rgbw <on|off>
          led raw <hex>
          led brightness [<parameter> <0..100>|reset]
          led zone [<zone> <0..2000>|reset]

PARAMETER   MEANING                       DEFAULT  EXAMPLE
auto        automatic station indication  -        led auto
on          station-5 red test colour     -        led on
off         force dark                    -        led off
rgbw on     SK6812 G-R-B-W                -        led rgbw on
rgbw off    WS2812 G-R-B                  -        led rgbw off
raw         raw wire-order colour         -        led raw 00ff0000
brightness  indication base %             -        help led brightness
zone        ambient multiplier %          -        help led zone

INTERACTIONS, most consequential first
- Moving: LED off, GP0 LOW.
- GP0 follows pixel demand. Non-zero color: GP0 HIGH; wait 300 us; transmit
  pixel data. Dark/off: GP0 LOW, placing the SK6812 in its unpowered sleep
  state.
- led auto enables automatic station indication and power management. led on
  forces the station-5 red test color; GP0 goes HIGH. led off forces the LED
  dark; GP0 goes LOW.
- Stations: 3% base; all indication percentages share ambient scaling. Raw
  test: explicit wire bytes bypass ambient scaling.
- Ambient: VEML7700 0x10; battery-cycle sampling; software shutdown. Zones:
  hysteresis and consecutive samples confirm brightness changes.
- Reset/startup: GP0 LOW with the internal pull-down enabled.
- Station 2/3/4/5 play 1/2/3/4 bird calls on arrival.

NOTE
led: show pixel state, ambient lux, zone, scale and sensor shutdown.
led brightness: set/list station, warning, critical, error, sample or breathe
  %. led zone: set/list Night, Dim, Indoor or Bright ambient multiplier %.
Brightness commands are RAM-only and affect the named indication or ambient-
  zone multiplier.
RGBW enabled: led raw GGRRBBWW - exactly 8 hexadecimal digits. RGBW disabled:
  led raw GGRRBB - exactly 6 hexadecimal digits.
GP0: direct SK6812 power supply, active HIGH. GP18: SK6812 serial data at 800
  kHz.
Station 1: Green/mint; Station 2: Yellow-green; Station 3: Yellow; Station 4:
  Pink.
Examples: led | led auto | led on | led off | led rgbw on | led raw 00ff0000
help led brightness: full base-percentage reference and examples. help led
  zone: full ambient-zone multiplier reference and examples.
GP0 switches LED power; GP18 carries data; battery warning below 3.600 V,
  critical below 3.300 V
```

## led brightness (page 1)

```text

LED BRIGHTNESS — base % for one automatic indication

SYNTAX    led brightness <parameter> <0..100>
          led brightness -> lists all six current RAM values
          led brightness reset -> restores all compiled defaults

PARAMETER  MEANING                        DEFAULT  EXAMPLE
station    all five CO2 station colours   3%       led brightness station 5
warning    battery-warning flash          30%      led brightness warning 40
critical   battery-critical flash         30%      led brightness critical 40
error      CO2 sensor-error red flash     30%      led brightness error 25
sample     startup accepted-sample flash  10%      led brightness sample 15
breathe    SCD41 warm-up maximum          10%      led brightness breathe 7

INTERACTIONS, most consequential first
- The selected base is multiplied by the confirmed VEML7700 zone multiplier.
  The final channel value is rounded and saturated at 100%; alert pulses may
  saturate.
- Station brightness remains below the alert base through the station ceiling.
- LED is still forced off while moving, between stations, or in forced-off
  mode.
- led raw <hex> is a diagnostic wire word and bypasses this percentage
  control.

NOTE
Values are RAM-only and return to defaults after reset; no flash is written.
Warning uses an orange double flash; critical uses an orange hazard flash; the
  startup accepted-sample flash is warm-white.
```

## led zone (page 1)

```text

LED ZONE — set the VEML7700 multiplier used by every indication

SYNTAX    led zone <zone> <0..2000>
          led zone -> lists all four current RAM multipliers
          led zone reset -> restores all four compiled multipliers

PARAMETER  MEANING            DEFAULT       EXAMPLE
night      0 to <10 lux       100% (1.00x)  led zone night 100
dim        10 to <100 lux     200% (2.00x)  led zone dim 200
indoor     100 to <500 lux    400% (4.00x)  led zone indoor 400
bright     500 lux and above  800% (8.00x)  led zone bright 800

INTERACTIONS, most consequential first
- The multiplier applies to stations, battery warning/critical, CO2 error,
  sample flash and breathing.
- Final output is saturated at 100%; station output is capped below the 30%
  alert base.
- 20% hysteresis prevents boundary flicker: upward thresholds are 12/120/600
  lux.
- Downward thresholds are 8/80/400 lux; three consecutive samples confirm a
  change.
- A failed or anomalous sample retains the last confirmed zone and brightness.
- The sensor is sampled on the existing battery cycle and shut down between
  readings.

NOTE
Valid range: 0..2000%. Example: led zone bright 900. Example: led zone.
Values are RAM-only and return to defaults after reset; no flash is written.
```

## buzzer (page 1)

```text

BUZZER — play the bird warning

SYNTAX    buzzer [on|off|status]
          buzzer crips-1 <1..200>
          buzzer crips-2 <1..200>
          buzzer crips-3 <1..200>
          buzzer crips-4 <1..200>
          buzzer crips-5 <1..200>
          buzzer tone-2 <100..10000 Hz> <1..5 seconds>
          buzzer tone-3 <100..10000 Hz> <1..5 seconds>

PARAMETER  MEANING                      DEFAULT  EXAMPLE
on         start bird warning           -        buzzer on
off        stop playback                -        buzzer off
status     report playback state        -        buzzer status
crips-1    1..200 complete calls/bouts  -        buzzer crips-1 3
crips-2    1..200 complete calls/bouts  -        buzzer crips-2 3
crips-3    1..200 complete calls/bouts  -        buzzer crips-3 3
crips-4    1..200 complete calls/bouts  -        buzzer crips-4 3
crips-5    1..200 complete calls/bouts  -        buzzer crips-5 3
tone-2     continuous sine, Hz and s    -        buzzer tone-2 6000 3
tone-3     continuous sine, Hz and s    -        buzzer tone-3 6000 3

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
on, off, crips-1..crips-5 1..200, or status
Example: buzzer crips-2 3
Plays the randomized bird warning on BO1/BO2; BIN1/BIN2 GP6/GP7, PWMB GP16.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
play/play-2: 1..200 calls, 200 ms gaps; play-2 uses DDS sine at 100 kHz PWM;
  off stops playback; original play uses square waves
```

## buzzer crips-1 (page 1)

```text

BUZZER CRIPS-1 — play the original square-wave station chirp

SYNTAX    buzzer crips-1 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
1..200 complete calls; off stops
Example: buzzer crips-1 3
Original square-wave station chirp; fresh random composition each repeat; 200
  ms gaps.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer crips-2 (page 1)

```text

BUZZER CRIPS-2 — play the original composition with DDS sine

SYNTAX    buzzer crips-2 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
1..200 complete calls; off stops
Example: buzzer crips-2 3
Original composition with clean DDS sine; fixed composition repeated; 200 ms
  gaps.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer crips-3 (page 1)

```text

BUZZER CRIPS-3 — Listening test only: three-part clean-sine sequence.

SYNTAX    buzzer crips-3 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
Count is 1..200 complete calls. Example: buzzer crips-3 10.
Listening test only: three-part clean-sine sequence; 8/8/12 bursts, 50 ms
  breaks; 444 ms total, 100 ms repeat gap.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer crips-4 (page 1)

```text

BUZZER CRIPS-4 — play the three-phrase listening test

SYNTAX    buzzer crips-4 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
1..200 complete bouts; off stops
Example: buzzer crips-4 3
Listening test: ~5 s, three phrases at 2 s onsets; two 100 ms intro notes then
  6/7/8 bursts; 2/4.3 kHz sine.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer crips-5 (page 1)

```text

BUZZER CRIPS-5 — play the compact crips-4 composition

SYNTAX    buzzer crips-5 <1..200>

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
1..200 complete bouts; off stops
Example: buzzer crips-5 3
Compact crips-4: same 27 notes, frequencies and sound durations; short gaps
  retained, phrase/repeat pauses halved again; ~2.7 s bout.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer tone-2 (page 1)

```text

BUZZER TONE-2 — play a continuous sine through the crips-2 renderer

SYNTAX    buzzer tone-2 <100..10000 Hz> <1..5 seconds>

PARAMETER  MEANING        DEFAULT  EXAMPLE
frequency  100..10000 Hz  -        buzzer tone-2 6000 3
duration   1..5 seconds   -        buzzer tone-2 6000 3

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
100..10000 Hz; 1..5 seconds
Example: buzzer tone-2 6000 3
Continuous full-scale sine through crips-2's renderer and DMA; compare with
  tone-3 at identical settings. off stops.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## buzzer tone-3 (page 1)

```text

BUZZER TONE-3 — play a continuous sine through the crips-3 renderer

SYNTAX    buzzer tone-3 <100..10000 Hz> <1..5 seconds>

PARAMETER  MEANING        DEFAULT  EXAMPLE
frequency  100..10000 Hz  -        buzzer tone-3 6000 3
duration   1..5 seconds   -        buzzer tone-3 6000 3

INTERACTIONS, most consequential first
- buzzer off stops playback.

NOTE
100..10000 Hz; 1..5 seconds
Example: buzzer tone-3 6000 3
Continuous full-scale sine through crips-3's renderer and DMA; no burst gaps
  or pitch changes. off stops.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## page (page 1)

```text

PAGE — list or select a debug page

SYNTAX    page -> lists pages
          page <1..8>

NOTE
no argument lists pages; page 1 to 8 selects
Example: page 2
Lists or selects general, motor, positions, battery, CO2, commands, data-log,
  or ambient-light pages.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
pages 1 general, 2 motor, 3 positions, 4 battery, 5 CO2, 6 commands, 7 data
  log, 8 ambient light
```

## selftest (page 1)

```text

SELFTEST — Checks configuration, ADC and the 1 kHz tick.

SYNTAX    selftest

NOTE
no motion
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## tick (page 1)

```text

TICK — Shows loop timing and watchdog health.

SYNTAX    tick

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## trace (page 1)

```text

TRACE — dump the latest move trace

SYNTAX    trace

NOTE
read-only
Dumps the latest move at 50 ms intervals: time, ADC, direction and duty.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## pins (page 1)

```text

PINS — Shows live motor and sensor pin levels.

SYNTAX    pins

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## pwm (page 1)

```text

PWM — Shows PWM configuration and calculated frequency.

SYNTAX    pwm

NOTE
read-only
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
motor channel A is GP14/PWMA; buzzer channel B is GP6/GP7 with GP16/PWMB
  enabled while sounding; report is read-only
```

## cfg (page 1)

```text

CFG — inspect or change runtime configuration

SYNTAX    cfg
          cfg <setting> <value>
          cfg reset

PARAMETER         MEANING          DEFAULT  EXAMPLE
DUTY_NORMAL       runtime setting  -        help DUTY_NORMAL
DUTY_APPROACH     runtime setting  -        help DUTY_APPROACH
DUTY_CREEP        runtime setting  -        help DUTY_CREEP
DUTY_MIN          runtime setting  -        help DUTY_MIN
APPROACH_COUNTS   runtime setting  -        help APPROACH_COUNTS
POS_WINDOW        runtime setting  -        help POS_WINDOW
DEBOUNCE_MS       runtime setting  -        help DEBOUNCE_MS
BRAKE_HOLD_MS     runtime setting  -        help BRAKE_HOLD_MS
POS_1_ADC         runtime setting  -        help POS_1_ADC
POS_2_ADC         runtime setting  -        help POS_2_ADC
POS_3_ADC         runtime setting  -        help POS_3_ADC
POS_4_ADC         runtime setting  -        help POS_4_ADC
POS_5_ADC         runtime setting  -        help POS_5_ADC
POS_6_ADC         runtime setting  -        help POS_6_ADC
LOW_ENDSTOP_ADC   runtime setting  -        help LOW_ENDSTOP_ADC
HIGH_ENDSTOP_ADC  runtime setting  -        help HIGH_ENDSTOP_ADC

NOTE
validated RAM values
Example: cfg DUTY_CREEP 30
Changes are RAM-only and lost on reset; export prints config.h station lines.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
cfg shows live values, compiled defaults and limits; cfg reset restores
  defaults.
```

## sim (page 1)

```text

SIM — inject simulated position readings

SYNTAX    sim <on|off>
          sim adc <0..4095>
          sim travel <from 1..6> <to 1..6> <1..10000 ms>

PARAMETER  MEANING                    DEFAULT  EXAMPLE
on/off     enable/disable simulation  -        sim on
adc        injected ADC, 0..4095      -        sim adc 2047
travel     simulate station travel    -        sim travel 1 5 300

INTERACTIONS, most consequential first
- Simulation inhibits physical motor output.

NOTE
ADC 0 to 4095
Example: sim adc 2047
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## cal (page 1)

```text

CAL — test simulated ADC or real station moves

SYNTAX    cal sim
          cal motor <5|50|500>

PARAMETER  MEANING                   DEFAULT  EXAMPLE
sim        motor-inhibited ADC test  -        cal sim
motor      5, 50 or 500 real moves   -        cal motor 500

INTERACTIONS, most consequential first
- cal sim tests injected ADC values; cal motor commands randomized real
  station moves and reports encoder centers and errors.

NOTE
sim is motor-inhibited; motor count is 5, 50 or 500
Example: cal sim | cal motor 500
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
cal sim: 50 randomized ADC points from 100 to 3000, motor inhibited; cal
  motor: 50 live station moves
```

## arm (page 1)

```text

ARM — Unlocks manual pulses until disarm or exit.

SYNTAX    arm

NOTE
idle controller
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## disarm (page 1)

```text

DISARM — Brakes and closes the manual interlock.

SYNTAX    disarm

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## drive (page 1)

```text

DRIVE — issue an armed manual motor pulse

SYNTAX    drive <fwd|rev> <0..255> <10..2000 ms>

PARAMETER  MEANING      DEFAULT  EXAMPLE
direction  fwd or rev   -        drive fwd 60 200
duty       0..255       -        drive fwd 60 200
duration   10..2000 ms  -        drive fwd 60 200

INTERACTIONS, most consequential first
- Requires arm; direction is fwd or rev.

NOTE
duty 0-255, 10-2000 ms
Example: drive fwd 60 200
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
direction fwd or rev; duty 0..255; duration 10..2000 ms; requires the debug
  motor interlock to be armed
```

## findmin (page 1)

```text

FINDMIN — Tests for the lowest duty that produces motion.

SYNTAX    findmin

NOTE
requires arm
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## batt (page 1)

```text

BATT — inspect battery state or configure simulation

SYNTAX    batt
          batt <raw|res|log|events|reset|help>
          batt sim <volts|off>
          batt sim range <minimum>-<maximum> V [/s]
          batt sim warning <volts> [/s]
          batt sim critical <volts> [/s]
          batt chirp <0.10..10.00> kHz [/s]
          batt chirp time i=<1..3600> r=<1..10> p=<1..10> d=<1..5> [/s]

PARAMETER     MEANING                     DEFAULT  EXAMPLE
raw           physical INA219 registers   -        batt raw
res           pack resistance and trend   -        batt res
log           session statistics          -        batt log
events        retained diagnostic events  -        batt events
reset         clear session and events    -        batt reset
sim           voltage override or off     -        batt sim 4.3
sim range     accepted voltage range      -        batt sim range 1.5-5.5 V
sim warning   warning threshold           -        batt sim warning 2.400 V
sim critical  critical threshold          -        batt sim critical 3.300 V
chirp         audible alert frequency     -        batt chirp 3.50 kHz
chirp time    i/r/p/d timing              -        help batt chirp time

INTERACTIONS, most consequential first
- Set/query the simulation range, simulate a voltage, or restore physical
  INA219 voltage.

NOTE
raw, res, log, events, reset, sim, sim range, or off
Example: batt sim 4.3 | batt sim range 1.5-5.5 V
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
append /s to range, warning, critical, or chirp to save flash defaults
batt sim range: within 1.0-6.0 V; batt sim warning: 1.4-5.4 V; batt sim
  critical: 2.2-5.9 V.
simulation 1.500-5.500 V (absolute 1.000-6.000 V); warning <3.600 V; critical
  <3.300 V
```

## batt raw (page 1)

```text

BATT RAW — report physical INA219 registers

SYNTAX    batt raw

NOTE
no additional arguments; read-only
Reports physical INA219 bus, shunt, current and power registers plus overflow.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## batt res (page 1)

```text

BATT RES — report pack resistance and trend

SYNTAX    batt res

NOTE
no additional arguments; read-only
Reports pack-resistance sample count, estimate and fresh-pack trend when
  available.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
minimum 10 wake samples; fresh-pack reference not measured
```

## batt log (page 1)

```text

BATT LOG — report battery session statistics

SYNTAX    batt log

NOTE
no additional arguments; read-only
Reports session duration, charge, energy, sample count and model status.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## batt events (page 1)

```text

BATT EVENTS — list retained battery diagnostic events

SYNTAX    batt events

NOTE
no additional arguments; read-only
Lists the retained battery peak, minimum-voltage and diagnostic events.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
read-only; retains the latest 16 notable events
```

## batt reset (page 1)

```text

BATT RESET — clear battery session counters and events

SYNTAX    batt reset

INTERACTIONS, most consequential first
- Clears battery session counters and events without changing calibration.

NOTE
no additional arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## batt sim (page 1)

```text

BATT SIM — override battery voltage

SYNTAX    batt sim <volts>
          batt sim off
          batt sim range [<minimum>-<maximum> V] [/s]

INTERACTIONS, most consequential first
- Overrides voltage for SOC and alarms; /s persists range and thresholds as
  defaults.

NOTE
selectable range within 1.0 to 6.0 V, or off
Example: batt sim range 1.5-5.5 V /s | batt sim 4.3 | batt sim off
Without /s the setting is RAM-only; append /s to save all battery defaults in
  flash.
simulation 1.500-5.500 V (absolute 1.000-6.000 V); warning <3.600 V; critical
  <3.300 V
```

## batt sim range (page 1)

```text

BATT SIM RANGE — set or query the accepted simulation range

SYNTAX    batt sim range
          batt sim range <minimum>-<maximum> V [/s]

NOTE
minimum-maximum within 1.000 to 6.000 V; optional /s
Example: batt sim range 1.50-5.5 V /s
Sets the accepted simulation range; /s saves all battery settings to flash.
Without /s the setting is RAM-only; append /s to save all battery defaults in
  flash.
simulation 1.500-5.500 V (absolute 1.000-6.000 V); warning <3.600 V; critical
  <3.300 V
```

## batt sim warning (page 1)

```text

BATT SIM WARNING — set the low-battery warning threshold

SYNTAX    batt sim warning [<volts>] [/s]

NOTE
threshold from 1.400 to 5.400 V; optional /s
Example: batt sim warning <2.400 V /s
Sets the low-battery warning threshold; /s saves all battery settings to
  flash.
Without /s the setting is RAM-only; append /s to save all battery defaults in
  flash.
simulation 1.500-5.500 V (absolute 1.000-6.000 V); warning <3.600 V; critical
  <3.300 V
```

## batt sim critical (page 1)

```text

BATT SIM CRITICAL — set the critical-battery threshold

SYNTAX    batt sim critical [<volts>] [/s]

NOTE
threshold from 2.200 to 5.900 V; optional /s
Example: batt sim critical <3.300 V /s
Sets the critical threshold; /s saves all battery settings to flash.
Without /s the setting is RAM-only; append /s to save all battery defaults in
  flash.
simulation 1.500-5.500 V (absolute 1.000-6.000 V); warning <3.600 V; critical
  <3.300 V
```

## batt chirp (page 1)

```text

BATT CHIRP — tone for the repeating critical-battery audible alert

SYNTAX    batt chirp [<frequency> kHz] [/s]
          batt chirp -> report the active chirp frequency and default source

INTERACTIONS, most consequential first
- Sounds only while a valid battery reading is below the critical threshold.
- Stops immediately when voltage is no longer critical; no boundary chirp.
- Audible alert remains active regardless of LED auto/on/off/raw mode.
- Use help batt chirp time to configure sequence interval, repeat, pause, and
  duration.

NOTE
Frequency range: 0.10 to 10.00 kHz (100 to 10000 Hz).
Compiled default is 2.700 kHz when no valid flash record exists.
batt chirp 3.50 kHz - use 3.5 kHz until reboot.
batt chirp 3.50 kHz /s - save it as the power-on default.
/s saves range, warning, critical, and chirp defaults together.
Examples: batt chirp | batt chirp 0.10 kHz | batt chirp 10.00 kHz /s
```

## batt chirp time (page 1)

```text

BATT CHIRP TIME — configure the critical-battery audible sequence

SYNTAX    batt chirp i=<interval> r=<repeat> p=<pause> d=<duration> [/s]
          batt chirp time i=<interval> r=<repeat> p=<pause> d=<duration> [/s]
          batt chirp time -> report interval, repeat, pause, duration, source

PARAMETER  MEANING                          DEFAULT  EXAMPLE
i          start-to-start, 1..3600 s        30       i=30
r          chirps per sequence, 1..10       2        r=2
p          silence between chirps, 1..10 s  5        p=5
d          each chirp length, 1..5 s        3        d=3

INTERACTIONS, most consequential first
- Changes take effect immediately; an active critical sequence restarts.
- All chirps stop immediately when the battery is no longer critical.
- Sequences never overlap; an overdue sequence starts after the prior one
  finishes.

NOTE
Configures the audible sequence triggered below the battery-critical
  threshold.
All interval, pause, and duration values are in seconds.
Example i=30: a new sequence is scheduled every 30 seconds. Example r=2: every
  sequence contains two chirps.
The pause starts when one chirp ends and finishes when the next chirp starts.
  There are r-1 pauses; pause is irrelevant when repeat is one.
/s saves range, warning, critical, frequency, and timing defaults to flash.
Example: batt chirp i=30 r=2 p=5 d=2. Every 30 s: two 2 s chirps separated by
  5 s of silence.
Sequence duration = (d * r) + (p * (r - 1)).
Silence after a sequence = i - sequence duration, when the result is positive.
For i=30 r=2 p=5 d=3: chirp 0-3, pause 3-8, chirp 8-11, silence 11-30.
Example: batt chirp i=10 r=3 p=2 d=1. Every 10 s: three 1 s chirps with 2 s
  silence between chirps.
Example: batt chirp i=60 r=1 p=10 d=4. Every 60 s: one 4 s chirp; pause is
  unused.
Example: batt chirp i=120 r=1 p=1 d=5 /s. Every 2 minutes: one 5 s chirp,
  saved as the power-on default.
Invalid or missing fields use defaults: i=30 r=2 p=5 d=3.
Example: i=0 r=0 p=0 d=0 becomes i=30 r=2 p=5 d=3.
Reset RAM: batt chirp i=30 r=2 p=5 d=3.
Status: batt chirp time - report current interval, repeat, pause, duration,
  and source.
Example: batt chirp i=30 r=2 p=5 d=2 /s
```

## load (page 1)

```text

LOAD — report load diagnostics

SYNTAX    load

NOTE
read-only
Inrush is a sampled lower bound; bench thresholds remain disabled until
  measured.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
inrush window 20 ms; no-load, stall and short thresholds not measured
```

## ina (page 1)

```text

INA — report INA219 configuration

SYNTAX    ina

NOTE
read-only
Shows computed calibration, conversion configuration and MODE 000 idle state.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
100 kHz I2C; 16 V bus; +/-160 mV shunt; 100 uA and 4 mV per LSB
```

## ds3231 (page 1)

```text

DS3231 — control the one-shot RTC event timer

SYNTAX    ds3231 <start|temp|timer|stop>
          ds3231 timeset <15..18000> [/s]

PARAMETER  MEANING            DEFAULT  EXAMPLE
start      start one event    -        DS3231 start
stop       disarm timer       -        DS3231 stop
temp       read temperature   -        DS3231 temp
timer      show countdown     -        DS3231 timer
timeset    15..18000 seconds  -        DS3231 timeset 1800 /s

INTERACTIONS, most consequential first
- Controls the one-shot RTC event timer; it remains stopped after boot and
  after an event.

NOTE
start, temp, timer, timeset, or stop
Example: DS3231 start | temp | timer | timeset 1800 | stop
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ds3231 start (page 1)

```text

DS3231 START — start one event using the configured interval

SYNTAX    ds3231 start

INTERACTIONS, most consequential first
- Starts one event using the configured interval restored from flash.

NOTE
no additional arguments
Example: DS3231 start
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ds3231 temp (page 1)

```text

DS3231 TEMP — Reads the DS3231 temperature registers over the shared I2C bus.

SYNTAX    ds3231 temp

NOTE
read-only
Example: DS3231 temp
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ds3231 stop (page 1)

```text

DS3231 STOP — Disarms the current DS3231 event timer.

SYNTAX    ds3231 stop

NOTE
no additional arguments
Example: DS3231 stop
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ds3231 timer (page 1)

```text

DS3231 TIMER — display the active one-shot countdown

SYNTAX    ds3231 timer

INTERACTIONS, most consequential first
- Displays the active one-shot countdown every second until q or Q is
  received.

NOTE
read-only
Example: DS3231 timer
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ds3231 timeset (page 1)

```text

DS3231 TIMESET — set the event interval

SYNTAX    ds3231 timeset <15..18000> [/s]

INTERACTIONS, most consequential first
- Sets the interval; re-arms if running, and /s saves it for DS3231 start.

NOTE
15 to 18000 seconds; optional /s
Example: DS3231 timeset 1800 /s
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## adc0offset (page 1)

```text

ADC0OFFSET — set the battery calibration correction

SYNTAX    adc0offset
          ADC0OFFSET=<-200..+200>MV [/s]

INTERACTIONS, most consequential first
- Adds a calibration correction before battery filtering; /s saves all battery
  settings to flash.

NOTE
signed offset -200 to +200 mV; optional /s
Example: ADC0OFFSET=+45MV /s
Without /s the setting is RAM-only; append /s to save all battery defaults in
  flash.
```

## co2 (page 1)

```text

CO2 — show the SCD41 measurement

SYNTAX    co2

INTERACTIONS, most consequential first
- Shows the filtered and raw SCD41 measurement; single mode starts a 5-second
  shot.

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2living (page 1)

```text

CO2LIVING — list the five living-room CO2 ranges

SYNTAX    co2living

NOTE
read-only
Lists all five living-room CO2 ranges with stations and air-quality functions.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2sleeping (page 1)

```text

CO2SLEEPING — list the five sleeping-room CO2 ranges

SYNTAX    co2sleeping

NOTE
read-only
Lists all five sleeping-room CO2 ranges with stations and air-quality
  functions.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2cfg (page 1)

```text

CO2CFG — show both profiles and the active selection

SYNTAX    co2cfg

NOTE
read-only
Shows both five-level CO2 profiles, the active selection, and flash source.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2sim (page 1)

```text

CO2SIM — simulate CO2 readings

SYNTAX    co2sim=<200..6000>
          co2sim off

INTERACTIONS, most consequential first
- Replaces SCD41 readings and exercises normal CO2-to-station control until
  off.

NOTE
200 to 6000 ppm, or off
Example: co2sim=440
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2limit (page 1)

```text

CO2LIMIT — edit one CO2 range boundary

SYNTAX    co2limit <profile> <level 1-4> <max ppm>

PARAMETER  MEANING             DEFAULT  EXAMPLE
profile    profile to edit     -        co2limit living 2 999
level      1..4; 5 open-ended  -        co2limit living 2 999
max ppm    upper bound         -        co2limit living 2 999

INTERACTIONS, most consequential first
- Changes one range boundary in RAM; level 5 remains open-ended.

NOTE
profile, level 1-4, maximum ppm
Example: co2limit living 2 999
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2save (page 1)

```text

CO2SAVE — save CO2 profiles to flash

SYNTAX    co2save

INTERACTIONS, most consequential first
- Saves both profiles and the active profile to dedicated flash storage.

NOTE
idle controller
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2defaults (page 1)

```text

CO2DEFAULTS — restore schematic CO2 defaults

SYNTAX    co2defaults

INTERACTIONS, most consequential first
- Restores schematic CO2 defaults in RAM; use co2save to persist them.

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## ready (page 1)

```text

READY — Shows and clears the SCD41 latched data-ready indication.

SYNTAX    ready

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## serial (page 1)

```text

SERIAL — read the 48-bit SCD41 serial number

SYNTAX    serial

INTERACTIONS, most consequential first
- Stops periodic measurement briefly and reads the SCD41 48-bit serial number.

NOTE
No arguments.
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## asc (page 1)

```text

ASC — Reads or sets SCD41 automatic self-calibration.

SYNTAX    asc
          asc <on|off>

NOTE
on, off, or no argument
Example: asc off
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## offset (page 1)

```text

OFFSET — Reads or sets the SCD41 temperature offset.

SYNTAX    offset
          offset <0..20 degrees C>

NOTE
0 to 20 degrees C, or no argument
Example: offset 4.5
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## altitude (page 1)

```text

ALTITUDE — Reads or sets SCD41 altitude compensation.

SYNTAX    altitude
          altitude <0..3000 metres>

NOTE
0 to 3000 metres, or no argument
Example: altitude 12
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## mode (page 1)

```text

MODE — Reads or selects the SCD41 measurement mode.

SYNTAX    mode
          mode <periodic|single>

NOTE
periodic, single, or no argument
Example: mode single
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## sdc41 (page 1)

```text

SDC41 — Uses the SCD41 protocol power-down or wake command.

SYNTAX    sdc41 <on|off>

NOTE
on or off
Example: sdc41 off
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## menu (page 1)

```text

MENU — Redraws the complete live SCD41 Page-5 menu.

SYNTAX    menu

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## clean (page 1)

```text

CLEAN — Clears command results and redraws the fixed-screen debug interface.

SYNTAX    clean

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## clear (page 1)

```text

CLEAR — clear output below Command >

SYNTAX    clear

INTERACTIONS, most consequential first
- Erases only output below Command >; preserves the current menu and Page-7
  log. New events can appear afterward.

NOTE
no arguments; fixed-screen mode
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## plain (page 1)

```text

PLAIN — Switches to line-oriented output without escape codes.

SYNTAX    plain

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## exit (page 1)

```text

EXIT — Leaves the debug console safely.

SYNTAX    exit

NOTE
no arguments
Debug-only diagnostic interface; command effects are RAM-only unless
  explicitly stated.
```

## co2 (page 5)

```text

CO2 — latest periodic reading or 5-second single shot

SYNTAX    co2

NOTE
SDC41 Page-5 command.
```

## co2living (page 5)

```text

CO2LIVING — list all five living-room ranges, stations, and functions

SYNTAX    co2living

NOTE
SDC41 Page-5 command.
```

## co2sleeping (page 5)

```text

CO2SLEEPING — list all five sleeping-room ranges, stations, and functions

SYNTAX    co2sleeping

NOTE
SDC41 Page-5 command.
```

## co2cfg (page 5)

```text

CO2CFG — show GP10-selected profile, both profiles, source, and mapped level

SYNTAX    co2cfg

NOTE
SDC41 Page-5 command.
```

## co2limit (page 5)

```text

CO2LIMIT — edit one upper bound in RAM

SYNTAX    co2limit <profile> <level 1-4> <max ppm>

PARAMETER  MEANING             DEFAULT  EXAMPLE
profile    profile to edit     -        co2limit living 2 999
level      1..4                -        co2limit living 2 999
max ppm    upper bound in RAM  -        co2limit living 2 999

NOTE
SDC41 Page-5 command.
```

## co2save (page 5)

```text

CO2SAVE — save both profile limits to flash

SYNTAX    co2save

NOTE
SDC41 Page-5 command.
```

## co2defaults (page 5)

```text

CO2DEFAULTS — restore schematic defaults in RAM

SYNTAX    co2defaults

NOTE
SDC41 Page-5 command.
```

## ready (page 5)

```text

READY — show and clear latched data-ready state

SYNTAX    ready

NOTE
SDC41 Page-5 command.
```

## serial (page 5)

```text

SERIAL — read the 48-bit SCD41 serial number

SYNTAX    serial

NOTE
SDC41 Page-5 command.
```

## selftest (page 5)

```text

SELFTEST — run the SCD41 sensor test

SYNTAX    selftest

INTERACTIONS, most consequential first
- stop measurement and run the approximately 10-second sensor test

NOTE
SDC41 Page-5 command.
```

## asc (page 5)

```text

ASC — read or set automatic self-calibration

SYNTAX    asc [on|off]

NOTE
SDC41 Page-5 command.
```

## offset (page 5)

```text

OFFSET — read or set 0..20 C temperature offset

SYNTAX    offset [degrees]

NOTE
SDC41 Page-5 command.
```

## altitude (page 5)

```text

ALTITUDE — read or set 0..3000 m

SYNTAX    altitude [metres]

NOTE
SDC41 Page-5 command.
```

## mode (page 5)

```text

MODE — read or select measurement mode

SYNTAX    mode [periodic|single]

NOTE
SDC41 Page-5 command.
```

## status (page 5)

```text

STATUS — show SCD41 mode, ASC, samples, and ready state

SYNTAX    status

NOTE
SDC41 Page-5 command.
```

## sdc41 (page 5)

```text

SDC41 — protocol power-down or wake/start

SYNTAX    sdc41 <on|off>

INTERACTIONS, most consequential first
- protocol power-down or wake/start

NOTE
SDC41 Page-5 command.
```

## menu (page 5)

```text

MENU — redraw Page 5 live SCD41 menu

SYNTAX    menu

NOTE
SDC41 Page-5 command.
```

## DUTY_NORMAL (page 1)

```text

DUTY_NORMAL — inspect or change this runtime setting

SYNTAX    cfg DUTY_NORMAL <value>
          cfg -> live value and limits

NOTE
DUTY_NORMAL is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## DUTY_APPROACH (page 1)

```text

DUTY_APPROACH — inspect or change this runtime setting

SYNTAX    cfg DUTY_APPROACH <value>
          cfg -> live value and limits

NOTE
DUTY_APPROACH is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## DUTY_CREEP (page 1)

```text

DUTY_CREEP — inspect or change this runtime setting

SYNTAX    cfg DUTY_CREEP <value>
          cfg -> live value and limits

NOTE
DUTY_CREEP is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## DUTY_MIN (page 1)

```text

DUTY_MIN — inspect or change this runtime setting

SYNTAX    cfg DUTY_MIN <value>
          cfg -> live value and limits

NOTE
DUTY_MIN is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## APPROACH_COUNTS (page 1)

```text

APPROACH_COUNTS — inspect or change this runtime setting

SYNTAX    cfg APPROACH_COUNTS <value>
          cfg -> live value and limits

NOTE
APPROACH_COUNTS is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_WINDOW (page 1)

```text

POS_WINDOW — how close counts as arrived

SYNTAX    cfg POS_WINDOW <value>
          cfg -> live value and limits

INTERACTIONS, most consequential first
- For oscillation, reducing DUTY_APPROACH is usually better.

NOTE
Example: cfg POS_WINDOW 40 - tighter stopping
Example: cfg POS_WINDOW 80 - looser, earlier arrival
now 20 counts, about 1.8 degrees either side of a station
10 to 102 counts; below quarter of current 411-count gap
```

## DEBOUNCE_MS (page 1)

```text

DEBOUNCE_MS — inspect or change this runtime setting

SYNTAX    cfg DEBOUNCE_MS <value>
          cfg -> live value and limits

NOTE
DEBOUNCE_MS is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## BRAKE_HOLD_MS (page 1)

```text

BRAKE_HOLD_MS — inspect or change this runtime setting

SYNTAX    cfg BRAKE_HOLD_MS <value>
          cfg -> live value and limits

NOTE
BRAKE_HOLD_MS is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_1_ADC (page 1)

```text

POS_1_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_1_ADC <value>
          cfg -> live value and limits

NOTE
POS_1_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_2_ADC (page 1)

```text

POS_2_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_2_ADC <value>
          cfg -> live value and limits

NOTE
POS_2_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_3_ADC (page 1)

```text

POS_3_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_3_ADC <value>
          cfg -> live value and limits

NOTE
POS_3_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_4_ADC (page 1)

```text

POS_4_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_4_ADC <value>
          cfg -> live value and limits

NOTE
POS_4_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_5_ADC (page 1)

```text

POS_5_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_5_ADC <value>
          cfg -> live value and limits

NOTE
POS_5_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## POS_6_ADC (page 1)

```text

POS_6_ADC — inspect or change this runtime setting

SYNTAX    cfg POS_6_ADC <value>
          cfg -> live value and limits

NOTE
POS_6_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## LOW_ENDSTOP_ADC (page 1)

```text

LOW_ENDSTOP_ADC — inspect or change this runtime setting

SYNTAX    cfg LOW_ENDSTOP_ADC <value>
          cfg -> live value and limits

NOTE
LOW_ENDSTOP_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```

## HIGH_ENDSTOP_ADC (page 1)

```text

HIGH_ENDSTOP_ADC — inspect or change this runtime setting

SYNTAX    cfg HIGH_ENDSTOP_ADC <value>
          cfg -> live value and limits

NOTE
HIGH_ENDSTOP_ADC is runtime-settable; type cfg for live value and limits
RAM-only; lost on reset. export prints config.h station lines.
```
