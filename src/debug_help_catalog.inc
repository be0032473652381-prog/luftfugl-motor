/* Structured presentation data. Command dispatch remains in debug.c. */
static const char *const help_help_syntax[] = {
    "help [<command>]",
    "help <setting>",
};
static const char *const help_help_notes[] = {
    "one command name, or none",
    "Example: help jog",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
    "Commands (use help <command> for its examples and limits):",
    "help              diag              sel               jog",
    "step              save              stations          limits",
    "lowendstop        highendstop       export            reset",
    "bootsel           move              pos               goto",
    "home              stop              status            adc",
    "angle             led               led brightness    led zone",
    "buzzer            buzzer crips-1    buzzer crips-2    buzzer crips-3",
    "buzzer crips-4    buzzer crips-5    buzzer tone-2     buzzer tone-3",
    "page              selftest          tick              trace",
    "pins              pwm               cfg               sim",
    "cal               arm               disarm            drive",
    "findmin           batt              batt raw          batt res",
    "batt log          batt events       batt reset        batt sim",
    "batt sim range    batt sim warning  batt sim critical batt chirp",
    "batt chirp time   load              ina               ds3231",
    "ds3231 start      ds3231 temp       ds3231 stop       ds3231 timer",
    "ds3231 timeset    adc0offset        co2               co2living",
    "co2sleeping       co2cfg            co2sim            co2limit",
    "co2save           co2defaults       ready             serial",
    "asc               offset            altitude          mode",
    "sdc41             menu              clean             clear",
    "plain             exit",
};
static const debug_help_document_t help_help = {
    .name = "HELP", .purpose = "Shows examples, limits and plain-language notes.",
    SYNTAX(help_help_syntax),
    NOTES(help_help_notes),
};

static const char *const help_diag_syntax[] = {
    "diag",
};
static const char *const help_diag_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_diag = {
    .name = "DIAG", .purpose = "Shows temporary UART receive and main-loop timing counters.",
    SYNTAX(help_diag_syntax),
    NOTES(help_diag_notes),
};

static const char *const help_sel_syntax[] = {
    "sel <1..6>",
};
static const char *const help_sel_notes[] = {
    "station 1 to 6",
    "Example: sel 3",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_sel = {
    .name = "SEL", .purpose = "Chooses which station save will update.",
    SYNTAX(help_sel_syntax),
    NOTES(help_sel_notes),
};

static const char *const help_jog_syntax[] = {
    "jog <-4095..+4095>",
};
static const char *const help_jog_notes[] = {
    "-4095 to +4095 counts",
    "Example: jog +2000",
    "Creep speed only; 100 counts is roughly 7 degrees.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_jog = {
    .name = "JOG", .purpose = "jog by an ADC count offset",
    SYNTAX(help_jog_syntax),
    NOTES(help_jog_notes),
};

static const char *const help_step_syntax[] = {
    "step <10|25|100|250|500>",
};
static const char *const help_step_notes[] = {
    "10, 25, 100, 250 or 500",
    "Example: step 250",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_step = {
    .name = "STEP", .purpose = "Changes the suggested calibration step.",
    SYNTAX(help_step_syntax),
    NOTES(help_step_notes),
};

static const char *const help_save_syntax[] = {
    "save [<1..6>]",
};
static const char *const help_save_notes[] = {
    "station 1 to 6",
    "Example: save 3",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_save = {
    .name = "SAVE", .purpose = "Without a number, saves the selected station.",
    SYNTAX(help_save_syntax),
    NOTES(help_save_notes),
};

static const char *const help_stations_syntax[] = {
    "stations",
};
static const char *const help_stations_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_stations = {
    .name = "STATIONS", .purpose = "Shows stored readings and difference from now.",
    SYNTAX(help_stations_syntax),
    NOTES(help_stations_notes),
};

static const char *const help_limits_syntax[] = {
    "limits",
};
static const char *const help_limits_notes[] = {
    "read-only",
    "One count is about 0.09 degrees; values come from the live configuration.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_limits = {
    .name = "LIMITS", .purpose = "show live movement limits",
    SYNTAX(help_limits_syntax),
    NOTES(help_limits_notes),
};

static const char *const help_lowendstop_syntax[] = {
    "lowendstop=<0..4095>",
};
static const char *const help_lowendstop_interactions[] = {
    "Sets the lower potentiometer travel limit in RAM; it takes effect immediately.",
};
static const char *const help_lowendstop_notes[] = {
    "ADC 0 to 4095; must be below high end-stop and station 1",
    "Example: lowendstop=100",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_lowendstop = {
    .name = "LOWENDSTOP", .purpose = "set the lower potentiometer travel limit",
    SYNTAX(help_lowendstop_syntax),
    INTERACTIONS(help_lowendstop_interactions),
    NOTES(help_lowendstop_notes),
};

static const char *const help_highendstop_syntax[] = {
    "highendstop=<0..4095>",
};
static const char *const help_highendstop_interactions[] = {
    "Sets the upper potentiometer travel limit in RAM; it takes effect immediately.",
};
static const char *const help_highendstop_notes[] = {
    "ADC 0 to 4095; must be above low end-stop and at or above station 6",
    "Example: highendstop=3000",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_highendstop = {
    .name = "HIGHENDSTOP", .purpose = "set the upper potentiometer travel limit",
    SYNTAX(help_highendstop_syntax),
    INTERACTIONS(help_highendstop_interactions),
    NOTES(help_highendstop_notes),
};

static const char *const help_export_syntax[] = {
    "export",
};
static const char *const help_export_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_export = {
    .name = "EXPORT", .purpose = "Prints values ready to paste into config.h.",
    SYNTAX(help_export_syntax),
    NOTES(help_export_notes),
};

static const char *const help_reset_syntax[] = {
    "reset",
    "reset stations",
};
static const char *const help_reset_notes[] = {
    "no arguments; reset stations restores calibration",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_reset = {
    .name = "RESET", .purpose = "Restarts from flash; prints resetting before rebooting.",
    SYNTAX(help_reset_syntax),
    NOTES(help_reset_notes),
};

static const char *const help_bootsel_syntax[] = {
    "bootsel",
};
static const char *const help_bootsel_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_bootsel = {
    .name = "BOOTSEL", .purpose = "Restarts into the USB bootloader for recovery.",
    SYNTAX(help_bootsel_syntax),
    NOTES(help_bootsel_notes),
};

static const char *const help_move_syntax[] = {
    "move <1..6>",
};
static const char *const help_move_notes[] = {
    "station 1 to 6",
    "Example: move 2",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_move = {
    .name = "MOVE", .purpose = "Uses closed-loop position control.",
    SYNTAX(help_move_syntax),
    NOTES(help_move_notes),
};

static const char *const help_pos_syntax[] = {
    "pos <1-6>",
};
static const char *const help_pos_interactions[] = {
    "rejects an invalid station, unknown starting position, or a controller that is already moving",
    "no wrap-around; configured motion range is station 1 through station 6, with station 6 reserved for CO2 errors",
    "every station uses target-window braking and settled arrival confirmation; station 1 homing also uses directional crossing protection",
};
static const char *const help_pos_notes[] = {
    "Alias for move; moves to one configured station through the normal closed-loop, filtered and limit-enforced controller path.",
    "Example: pos 6. Station 1 to 6.",
};
static const debug_help_document_t help_pos = {
    .name = "POS", .purpose = "move through the normal closed-loop controller path",
    SYNTAX(help_pos_syntax),
    INTERACTIONS(help_pos_interactions),
    NOTES(help_pos_notes),
};

static const char *const help_goto_syntax[] = {
    "goto <0..4095>",
};
static const char *const help_goto_notes[] = {
    "ADC range 0 to 4095",
    "Example: goto 4000",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_goto = {
    .name = "GOTO", .purpose = "Moves directly to one raw ADC target; movement is not wrap-aware.",
    SYNTAX(help_goto_syntax),
    NOTES(help_goto_notes),
};

static const char *const help_home_syntax[] = {
    "home",
};
static const char *const help_home_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_home = {
    .name = "HOME", .purpose = "Returns to station 1 through the guarded home path.",
    SYNTAX(help_home_syntax),
    NOTES(help_home_notes),
};

static const char *const help_stop_syntax[] = {
    "stop",
};
static const char *const help_stop_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_stop = {
    .name = "STOP", .purpose = "Brakes immediately; a period works without Enter.",
    SYNTAX(help_stop_syntax),
    NOTES(help_stop_notes),
};

static const char *const help_adc_syntax[] = {
    "adc",
};
static const char *const help_adc_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_adc = {
    .name = "ADC", .purpose = "Shows raw, filtered and classified sensing.",
    SYNTAX(help_adc_syntax),
    NOTES(help_adc_notes),
};

static const char *const help_angle_syntax[] = {
    "angle",
};
static const char *const help_angle_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_angle = {
    .name = "ANGLE", .purpose = "Shows the filtered ADC reading converted to degrees.",
    SYNTAX(help_angle_syntax),
    NOTES(help_angle_notes),
};

static const char *const help_led_syntax[] = {
    "led",
    "led <on|off|auto>",
    "led rgbw <on|off>",
    "led raw <hex>",
    "led brightness [<parameter> <0..100>|reset]",
    "led zone [<zone> <0..2000>|reset]",
};
static const debug_help_parameter_t help_led_parameters[] = {
    {"auto", "automatic station indication", "-", "led auto"},
    {"on", "station-5 red test colour", "-", "led on"},
    {"off", "force dark", "-", "led off"},
    {"rgbw on", "SK6812 G-R-B-W", "-", "led rgbw on"},
    {"rgbw off", "WS2812 G-R-B", "-", "led rgbw off"},
    {"raw", "raw wire-order colour", "-", "led raw 00ff0000"},
    {"brightness", "indication base %", "-", "help led brightness"},
    {"zone", "ambient multiplier %", "-", "help led zone"},
};
static const char *const help_led_interactions[] = {
    "Moving: LED off, GP0 LOW.",
    "GP0 follows pixel demand. Non-zero color: GP0 HIGH; wait 300 us; transmit pixel data. Dark/off: GP0 LOW, placing the SK6812 in its unpowered sleep state.",
    "led auto enables automatic station indication and power management. led on forces the station-5 red test color; GP0 goes HIGH. led off forces the LED dark; GP0 goes LOW.",
    "Stations: 3% base; all indication percentages share ambient scaling. Raw test: explicit wire bytes bypass ambient scaling.",
    "Ambient: VEML7700 0x10; battery-cycle sampling; software shutdown. Zones: hysteresis and consecutive samples confirm brightness changes.",
    "Reset/startup: GP0 LOW with the internal pull-down enabled.",
    "Station 2/3/4/5 play 1/2/3/4 bird calls on arrival.",
};
static const char *const help_led_notes[] = {
    "led: show pixel state, ambient lux, zone, scale and sensor shutdown.",
    "led brightness: set/list station, warning, critical, error, sample or breathe %. led zone: set/list Night, Dim, Indoor or Bright ambient multiplier %.",
    "Brightness commands are RAM-only and affect the named indication or ambient-zone multiplier.",
    "RGBW enabled: led raw GGRRBBWW - exactly 8 hexadecimal digits. RGBW disabled: led raw GGRRBB - exactly 6 hexadecimal digits.",
    "GP0: direct SK6812 power supply, active HIGH. GP18: SK6812 serial data at 800 kHz.",
    "Station 1: Green/mint; Station 2: Yellow-green; Station 3: Yellow; Station 4: Pink.",
    "Examples: led | led auto | led on | led off | led rgbw on | led raw 00ff0000",
    "help led brightness: full base-percentage reference and examples. help led zone: full ambient-zone multiplier reference and examples.",
};
static const debug_help_document_t help_led = {
    .name = "LED", .purpose = "control pixel indication and power",
    SYNTAX(help_led_syntax),
    PARAMETERS(help_led_parameters),
    INTERACTIONS(help_led_interactions),
    NOTES(help_led_notes),
};

static const char *const help_led_zone_syntax[] = {
    "led zone <zone> <0..2000>",
    "led zone -> lists all four current RAM multipliers",
    "led zone reset -> restores all four compiled multipliers",
};
static const debug_help_parameter_t help_led_zone_parameters[] = {
    {"night", "0 to <10 lux", "100% (1.00x)", "led zone night 100"},
    {"dim", "10 to <100 lux", "200% (2.00x)", "led zone dim 200"},
    {"indoor", "100 to <500 lux", "400% (4.00x)", "led zone indoor 400"},
    {"bright", "500 lux and above", "800% (8.00x)", "led zone bright 800"},
};
static const char *const help_led_zone_interactions[] = {
    "The multiplier applies to stations, battery warning/critical, CO2 error, sample flash and breathing.",
    "Final output is saturated at 100%; station output is capped below the 30% alert base.",
    "20% hysteresis prevents boundary flicker: upward thresholds are 12/120/600 lux.",
    "Downward thresholds are 8/80/400 lux; three consecutive samples confirm a change.",
    "A failed or anomalous sample retains the last confirmed zone and brightness.",
    "The sensor is sampled on the existing battery cycle and shut down between readings.",
};
static const char *const help_led_zone_notes[] = {
    "Valid range: 0..2000%. Example: led zone bright 900. Example: led zone.",
    "Values are RAM-only and return to defaults after reset; no flash is written.",
};
static const debug_help_document_t help_led_zone = {
    .name = "LED ZONE", .purpose = "set the VEML7700 multiplier used by every indication",
    SYNTAX(help_led_zone_syntax),
    PARAMETERS(help_led_zone_parameters),
    INTERACTIONS(help_led_zone_interactions),
    NOTES(help_led_zone_notes),
};

static const char *const help_buzzer_syntax[] = {
    "buzzer [on|off|status]",
    "buzzer crips-1 <1..200>",
    "buzzer crips-2 <1..200>",
    "buzzer crips-3 <1..200>",
    "buzzer crips-4 <1..200>",
    "buzzer crips-5 <1..200>",
    "buzzer tone-2 <100..10000 Hz> <1..5 seconds>",
    "buzzer tone-3 <100..10000 Hz> <1..5 seconds>",
};
static const debug_help_parameter_t help_buzzer_parameters[] = {
    {"on", "start bird warning", "-", "buzzer on"},
    {"off", "stop playback", "-", "buzzer off"},
    {"status", "report playback state", "-", "buzzer status"},
    {"crips-1", "1..200 complete calls/bouts", "-", "buzzer crips-1 3"},
    {"crips-2", "1..200 complete calls/bouts", "-", "buzzer crips-2 3"},
    {"crips-3", "1..200 complete calls/bouts", "-", "buzzer crips-3 3"},
    {"crips-4", "1..200 complete calls/bouts", "-", "buzzer crips-4 3"},
    {"crips-5", "1..200 complete calls/bouts", "-", "buzzer crips-5 3"},
    {"tone-2", "continuous sine, Hz and s", "-", "buzzer tone-2 6000 3"},
    {"tone-3", "continuous sine, Hz and s", "-", "buzzer tone-3 6000 3"},
};
static const char *const help_buzzer_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_notes[] = {
    "on, off, crips-1..crips-5 1..200, or status",
    "Example: buzzer crips-2 3",
    "Plays the randomized bird warning on BO1/BO2; BIN1/BIN2 GP6/GP7, PWMB GP16.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer = {
    .name = "BUZZER", .purpose = "play the bird warning",
    SYNTAX(help_buzzer_syntax),
    PARAMETERS(help_buzzer_parameters),
    INTERACTIONS(help_buzzer_interactions),
    NOTES(help_buzzer_notes),
};

static const char *const help_buzzer_crips_1_syntax[] = {
    "buzzer crips-1 <1..200>",
};
static const char *const help_buzzer_crips_1_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_crips_1_notes[] = {
    "1..200 complete calls; off stops",
    "Example: buzzer crips-1 3",
    "Original square-wave station chirp; fresh random composition each repeat; 200 ms gaps.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_crips_1 = {
    .name = "BUZZER CRIPS-1", .purpose = "play the original square-wave station chirp",
    SYNTAX(help_buzzer_crips_1_syntax),
    INTERACTIONS(help_buzzer_crips_1_interactions),
    NOTES(help_buzzer_crips_1_notes),
};

static const char *const help_buzzer_crips_2_syntax[] = {
    "buzzer crips-2 <1..200>",
};
static const char *const help_buzzer_crips_2_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_crips_2_notes[] = {
    "1..200 complete calls; off stops",
    "Example: buzzer crips-2 3",
    "Original composition with clean DDS sine; fixed composition repeated; 200 ms gaps.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_crips_2 = {
    .name = "BUZZER CRIPS-2", .purpose = "play the original composition with DDS sine",
    SYNTAX(help_buzzer_crips_2_syntax),
    INTERACTIONS(help_buzzer_crips_2_interactions),
    NOTES(help_buzzer_crips_2_notes),
};

static const char *const help_buzzer_crips_4_syntax[] = {
    "buzzer crips-4 <1..200>",
};
static const char *const help_buzzer_crips_4_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_crips_4_notes[] = {
    "1..200 complete bouts; off stops",
    "Example: buzzer crips-4 3",
    "Listening test: ~5 s, three phrases at 2 s onsets; two 100 ms intro notes then 6/7/8 bursts; 2/4.3 kHz sine.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_crips_4 = {
    .name = "BUZZER CRIPS-4", .purpose = "play the three-phrase listening test",
    SYNTAX(help_buzzer_crips_4_syntax),
    INTERACTIONS(help_buzzer_crips_4_interactions),
    NOTES(help_buzzer_crips_4_notes),
};

static const char *const help_buzzer_crips_5_syntax[] = {
    "buzzer crips-5 <1..200>",
};
static const char *const help_buzzer_crips_5_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_crips_5_notes[] = {
    "1..200 complete bouts; off stops",
    "Example: buzzer crips-5 3",
    "Compact crips-4: same 27 notes, frequencies and sound durations; short gaps retained, phrase/repeat pauses halved again; ~2.7 s bout.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_crips_5 = {
    .name = "BUZZER CRIPS-5", .purpose = "play the compact crips-4 composition",
    SYNTAX(help_buzzer_crips_5_syntax),
    INTERACTIONS(help_buzzer_crips_5_interactions),
    NOTES(help_buzzer_crips_5_notes),
};

static const char *const help_buzzer_tone_2_syntax[] = {
    "buzzer tone-2 <100..10000 Hz> <1..5 seconds>",
};
static const debug_help_parameter_t help_buzzer_tone_2_parameters[] = {
    {"frequency", "100..10000 Hz", "-", "buzzer tone-2 6000 3"},
    {"duration", "1..5 seconds", "-", "buzzer tone-2 6000 3"},
};
static const char *const help_buzzer_tone_2_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_tone_2_notes[] = {
    "100..10000 Hz; 1..5 seconds",
    "Example: buzzer tone-2 6000 3",
    "Continuous full-scale sine through crips-2's renderer and DMA; compare with tone-3 at identical settings. off stops.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_tone_2 = {
    .name = "BUZZER TONE-2", .purpose = "play a continuous sine through the crips-2 renderer",
    SYNTAX(help_buzzer_tone_2_syntax),
    PARAMETERS(help_buzzer_tone_2_parameters),
    INTERACTIONS(help_buzzer_tone_2_interactions),
    NOTES(help_buzzer_tone_2_notes),
};

static const char *const help_buzzer_tone_3_syntax[] = {
    "buzzer tone-3 <100..10000 Hz> <1..5 seconds>",
};
static const debug_help_parameter_t help_buzzer_tone_3_parameters[] = {
    {"frequency", "100..10000 Hz", "-", "buzzer tone-3 6000 3"},
    {"duration", "1..5 seconds", "-", "buzzer tone-3 6000 3"},
};
static const char *const help_buzzer_tone_3_interactions[] = {
    "buzzer off stops playback.",
};
static const char *const help_buzzer_tone_3_notes[] = {
    "100..10000 Hz; 1..5 seconds",
    "Example: buzzer tone-3 6000 3",
    "Continuous full-scale sine through crips-3's renderer and DMA; no burst gaps or pitch changes. off stops.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_buzzer_tone_3 = {
    .name = "BUZZER TONE-3", .purpose = "play a continuous sine through the crips-3 renderer",
    SYNTAX(help_buzzer_tone_3_syntax),
    PARAMETERS(help_buzzer_tone_3_parameters),
    INTERACTIONS(help_buzzer_tone_3_interactions),
    NOTES(help_buzzer_tone_3_notes),
};

static const char *const help_page_syntax[] = {
    "page -> lists pages",
    "page <1..8>",
};
static const char *const help_page_notes[] = {
    "no argument lists pages; page 1 to 8 selects",
    "Example: page 2",
    "Lists or selects general, motor, positions, battery, CO2, commands, data-log, or ambient-light pages.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_page = {
    .name = "PAGE", .purpose = "list or select a debug page",
    SYNTAX(help_page_syntax),
    NOTES(help_page_notes),
};

static const char *const help_selftest_syntax[] = {
    "selftest",
};
static const char *const help_selftest_notes[] = {
    "no motion",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_selftest = {
    .name = "SELFTEST", .purpose = "Checks configuration, ADC and the 1 kHz tick.",
    SYNTAX(help_selftest_syntax),
    NOTES(help_selftest_notes),
};

static const char *const help_tick_syntax[] = {
    "tick",
};
static const char *const help_tick_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_tick = {
    .name = "TICK", .purpose = "Shows loop timing and watchdog health.",
    SYNTAX(help_tick_syntax),
    NOTES(help_tick_notes),
};

static const char *const help_trace_syntax[] = {
    "trace",
};
static const char *const help_trace_notes[] = {
    "read-only",
    "Dumps the latest move at 50 ms intervals: time, ADC, direction and duty.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_trace = {
    .name = "TRACE", .purpose = "dump the latest move trace",
    SYNTAX(help_trace_syntax),
    NOTES(help_trace_notes),
};

static const char *const help_pins_syntax[] = {
    "pins",
};
static const char *const help_pins_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_pins = {
    .name = "PINS", .purpose = "Shows live motor and sensor pin levels.",
    SYNTAX(help_pins_syntax),
    NOTES(help_pins_notes),
};

static const char *const help_pwm_syntax[] = {
    "pwm",
};
static const char *const help_pwm_notes[] = {
    "read-only",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_pwm = {
    .name = "PWM", .purpose = "Shows PWM configuration and calculated frequency.",
    SYNTAX(help_pwm_syntax),
    NOTES(help_pwm_notes),
};

static const char *const help_cfg_syntax[] = {
    "cfg",
    "cfg <setting> <value>",
    "cfg reset",
};
static const debug_help_parameter_t help_cfg_parameters[] = {
    {"DUTY_NORMAL", "runtime setting", "-", "help DUTY_NORMAL"},
    {"DUTY_APPROACH", "runtime setting", "-", "help DUTY_APPROACH"},
    {"DUTY_CREEP", "runtime setting", "-", "help DUTY_CREEP"},
    {"DUTY_MIN", "runtime setting", "-", "help DUTY_MIN"},
    {"APPROACH_COUNTS", "runtime setting", "-", "help APPROACH_COUNTS"},
    {"POS_WINDOW", "runtime setting", "-", "help POS_WINDOW"},
    {"DEBOUNCE_MS", "runtime setting", "-", "help DEBOUNCE_MS"},
    {"BRAKE_HOLD_MS", "runtime setting", "-", "help BRAKE_HOLD_MS"},
    {"POS_1_ADC", "runtime setting", "-", "help POS_1_ADC"},
    {"POS_2_ADC", "runtime setting", "-", "help POS_2_ADC"},
    {"POS_3_ADC", "runtime setting", "-", "help POS_3_ADC"},
    {"POS_4_ADC", "runtime setting", "-", "help POS_4_ADC"},
    {"POS_5_ADC", "runtime setting", "-", "help POS_5_ADC"},
    {"POS_6_ADC", "runtime setting", "-", "help POS_6_ADC"},
    {"LOW_ENDSTOP_ADC", "runtime setting", "-", "help LOW_ENDSTOP_ADC"},
    {"HIGH_ENDSTOP_ADC", "runtime setting", "-", "help HIGH_ENDSTOP_ADC"},
};
static const char *const help_cfg_notes[] = {
    "validated RAM values",
    "Example: cfg DUTY_CREEP 30",
    "Changes are RAM-only and lost on reset; export prints config.h station lines.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
    "cfg shows live values, compiled defaults and limits; cfg reset restores defaults.",
};
static const debug_help_document_t help_cfg = {
    .name = "CFG", .purpose = "inspect or change runtime configuration",
    SYNTAX(help_cfg_syntax),
    PARAMETERS(help_cfg_parameters),
    NOTES(help_cfg_notes),
};

static const char *const help_sim_syntax[] = {
    "sim <on|off>",
    "sim adc <0..4095>",
    "sim travel <from 1..6> <to 1..6> <1..10000 ms>",
};
static const debug_help_parameter_t help_sim_parameters[] = {
    {"on/off", "enable/disable simulation", "-", "sim on"},
    {"adc", "injected ADC, 0..4095", "-", "sim adc 2047"},
    {"travel", "simulate station travel", "-", "sim travel 1 5 300"},
};
static const char *const help_sim_interactions[] = {
    "Simulation inhibits physical motor output.",
};
static const char *const help_sim_notes[] = {
    "ADC 0 to 4095",
    "Example: sim adc 2047",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_sim = {
    .name = "SIM", .purpose = "inject simulated position readings",
    SYNTAX(help_sim_syntax),
    PARAMETERS(help_sim_parameters),
    INTERACTIONS(help_sim_interactions),
    NOTES(help_sim_notes),
};

static const char *const help_cal_syntax[] = {
    "cal sim",
    "cal motor <5|50|500>",
};
static const debug_help_parameter_t help_cal_parameters[] = {
    {"sim", "motor-inhibited ADC test", "-", "cal sim"},
    {"motor", "5, 50 or 500 real moves", "-", "cal motor 500"},
};
static const char *const help_cal_interactions[] = {
    "cal sim tests injected ADC values; cal motor commands randomized real station moves and reports encoder centers and errors.",
};
static const char *const help_cal_notes[] = {
    "sim is motor-inhibited; motor count is 5, 50 or 500",
    "Example: cal sim | cal motor 500",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_cal = {
    .name = "CAL", .purpose = "test simulated ADC or real station moves",
    SYNTAX(help_cal_syntax),
    PARAMETERS(help_cal_parameters),
    INTERACTIONS(help_cal_interactions),
    NOTES(help_cal_notes),
};

static const char *const help_arm_syntax[] = {
    "arm",
};
static const char *const help_arm_notes[] = {
    "idle controller",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_arm = {
    .name = "ARM", .purpose = "Unlocks manual pulses until disarm or exit.",
    SYNTAX(help_arm_syntax),
    NOTES(help_arm_notes),
};

static const char *const help_disarm_syntax[] = {
    "disarm",
};
static const char *const help_disarm_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_disarm = {
    .name = "DISARM", .purpose = "Brakes and closes the manual interlock.",
    SYNTAX(help_disarm_syntax),
    NOTES(help_disarm_notes),
};

static const char *const help_drive_syntax[] = {
    "drive <fwd|rev> <0..255> <10..2000 ms>",
};
static const debug_help_parameter_t help_drive_parameters[] = {
    {"direction", "fwd or rev", "-", "drive fwd 60 200"},
    {"duty", "0..255", "-", "drive fwd 60 200"},
    {"duration", "10..2000 ms", "-", "drive fwd 60 200"},
};
static const char *const help_drive_interactions[] = {
    "Requires arm; direction is fwd or rev.",
};
static const char *const help_drive_notes[] = {
    "duty 0-255, 10-2000 ms",
    "Example: drive fwd 60 200",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_drive = {
    .name = "DRIVE", .purpose = "issue an armed manual motor pulse",
    SYNTAX(help_drive_syntax),
    PARAMETERS(help_drive_parameters),
    INTERACTIONS(help_drive_interactions),
    NOTES(help_drive_notes),
};

static const char *const help_findmin_syntax[] = {
    "findmin",
};
static const char *const help_findmin_notes[] = {
    "requires arm",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_findmin = {
    .name = "FINDMIN", .purpose = "Tests for the lowest duty that produces motion.",
    SYNTAX(help_findmin_syntax),
    NOTES(help_findmin_notes),
};

static const char *const help_batt_syntax[] = {
    "batt",
    "batt <raw|res|log|events|reset|help>",
    "batt sim <volts|off>",
    "batt sim range <minimum>-<maximum> V [/s]",
    "batt sim warning <volts> [/s]",
    "batt sim critical <volts> [/s]",
    "batt chirp <0.10..10.00> kHz [/s]",
    "batt chirp time i=<1..3600> r=<1..10> p=<1..10> d=<1..5> [/s]",
};
static const debug_help_parameter_t help_batt_parameters[] = {
    {"raw", "physical INA219 registers", "-", "batt raw"},
    {"res", "pack resistance and trend", "-", "batt res"},
    {"log", "session statistics", "-", "batt log"},
    {"events", "retained diagnostic events", "-", "batt events"},
    {"reset", "clear session and events", "-", "batt reset"},
    {"sim", "voltage override or off", "-", "batt sim 4.3"},
    {"sim range", "accepted voltage range", "-", "batt sim range 1.5-5.5 V"},
    {"sim warning", "warning threshold", "-", "batt sim warning 2.400 V"},
    {"sim critical", "critical threshold", "-", "batt sim critical 3.300 V"},
    {"chirp", "audible alert frequency", "-", "batt chirp 3.50 kHz"},
    {"chirp time", "i/r/p/d timing", "-", "help batt chirp time"},
};
static const char *const help_batt_interactions[] = {
    "Set/query the simulation range, simulate a voltage, or restore physical INA219 voltage.",
};
static const char *const help_batt_notes[] = {
    "raw, res, log, events, reset, sim, sim range, or off",
    "Example: batt sim 4.3 | batt sim range 1.5-5.5 V",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
    "append /s to range, warning, critical, or chirp to save flash defaults",
    "batt sim range: within 1.0-6.0 V; batt sim warning: 1.4-5.4 V; batt sim critical: 2.2-5.9 V.",
};
static const debug_help_document_t help_batt = {
    .name = "BATT", .purpose = "inspect battery state or configure simulation",
    SYNTAX(help_batt_syntax),
    PARAMETERS(help_batt_parameters),
    INTERACTIONS(help_batt_interactions),
    NOTES(help_batt_notes),
};

static const char *const help_batt_raw_syntax[] = {
    "batt raw",
};
static const char *const help_batt_raw_notes[] = {
    "no additional arguments; read-only",
    "Reports physical INA219 bus, shunt, current and power registers plus overflow.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_batt_raw = {
    .name = "BATT RAW", .purpose = "report physical INA219 registers",
    SYNTAX(help_batt_raw_syntax),
    NOTES(help_batt_raw_notes),
};

static const char *const help_batt_res_syntax[] = {
    "batt res",
};
static const char *const help_batt_res_notes[] = {
    "no additional arguments; read-only",
    "Reports pack-resistance sample count, estimate and fresh-pack trend when available.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_batt_res = {
    .name = "BATT RES", .purpose = "report pack resistance and trend",
    SYNTAX(help_batt_res_syntax),
    NOTES(help_batt_res_notes),
};

static const char *const help_batt_log_syntax[] = {
    "batt log",
};
static const char *const help_batt_log_notes[] = {
    "no additional arguments; read-only",
    "Reports session duration, charge, energy, sample count and model status.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_batt_log = {
    .name = "BATT LOG", .purpose = "report battery session statistics",
    SYNTAX(help_batt_log_syntax),
    NOTES(help_batt_log_notes),
};

static const char *const help_batt_events_syntax[] = {
    "batt events",
};
static const char *const help_batt_events_notes[] = {
    "no additional arguments; read-only",
    "Lists the retained battery peak, minimum-voltage and diagnostic events.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_batt_events = {
    .name = "BATT EVENTS", .purpose = "list retained battery diagnostic events",
    SYNTAX(help_batt_events_syntax),
    NOTES(help_batt_events_notes),
};

static const char *const help_batt_reset_syntax[] = {
    "batt reset",
};
static const char *const help_batt_reset_interactions[] = {
    "Clears battery session counters and events without changing calibration.",
};
static const char *const help_batt_reset_notes[] = {
    "no additional arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_batt_reset = {
    .name = "BATT RESET", .purpose = "clear battery session counters and events",
    SYNTAX(help_batt_reset_syntax),
    INTERACTIONS(help_batt_reset_interactions),
    NOTES(help_batt_reset_notes),
};

static const char *const help_batt_sim_syntax[] = {
    "batt sim <volts>",
    "batt sim off",
    "batt sim range [<minimum>-<maximum> V] [/s]",
};
static const char *const help_batt_sim_interactions[] = {
    "Overrides voltage for SOC and alarms; /s persists range and thresholds as defaults.",
};
static const char *const help_batt_sim_notes[] = {
    "selectable range within 1.0 to 6.0 V, or off",
    "Example: batt sim range 1.5-5.5 V /s | batt sim 4.3 | batt sim off",
    "Without /s the setting is RAM-only; append /s to save all battery defaults in flash.",
};
static const debug_help_document_t help_batt_sim = {
    .name = "BATT SIM", .purpose = "override battery voltage",
    SYNTAX(help_batt_sim_syntax),
    INTERACTIONS(help_batt_sim_interactions),
    NOTES(help_batt_sim_notes),
};

static const char *const help_batt_sim_range_syntax[] = {
    "batt sim range",
    "batt sim range <minimum>-<maximum> V [/s]",
};
static const char *const help_batt_sim_range_notes[] = {
    "minimum-maximum within 1.000 to 6.000 V; optional /s",
    "Example: batt sim range 1.50-5.5 V /s",
    "Sets the accepted simulation range; /s saves all battery settings to flash.",
    "Without /s the setting is RAM-only; append /s to save all battery defaults in flash.",
};
static const debug_help_document_t help_batt_sim_range = {
    .name = "BATT SIM RANGE", .purpose = "set or query the accepted simulation range",
    SYNTAX(help_batt_sim_range_syntax),
    NOTES(help_batt_sim_range_notes),
};

static const char *const help_batt_sim_warning_syntax[] = {
    "batt sim warning [<volts>] [/s]",
};
static const char *const help_batt_sim_warning_notes[] = {
    "threshold from 1.400 to 5.400 V; optional /s",
    "Example: batt sim warning <2.400 V /s",
    "Sets the low-battery warning threshold; /s saves all battery settings to flash.",
    "Without /s the setting is RAM-only; append /s to save all battery defaults in flash.",
};
static const debug_help_document_t help_batt_sim_warning = {
    .name = "BATT SIM WARNING", .purpose = "set the low-battery warning threshold",
    SYNTAX(help_batt_sim_warning_syntax),
    NOTES(help_batt_sim_warning_notes),
};

static const char *const help_batt_sim_critical_syntax[] = {
    "batt sim critical [<volts>] [/s]",
};
static const char *const help_batt_sim_critical_notes[] = {
    "threshold from 2.200 to 5.900 V; optional /s",
    "Example: batt sim critical <3.300 V /s",
    "Sets the critical threshold; /s saves all battery settings to flash.",
    "Without /s the setting is RAM-only; append /s to save all battery defaults in flash.",
};
static const debug_help_document_t help_batt_sim_critical = {
    .name = "BATT SIM CRITICAL", .purpose = "set the critical-battery threshold",
    SYNTAX(help_batt_sim_critical_syntax),
    NOTES(help_batt_sim_critical_notes),
};

static const char *const help_batt_chirp_time_syntax[] = {
    "batt chirp i=<interval> r=<repeat> p=<pause> d=<duration> [/s]",
    "batt chirp time i=<interval> r=<repeat> p=<pause> d=<duration> [/s]",
    "batt chirp time -> report interval, repeat, pause, duration, source",
};
static const debug_help_parameter_t help_batt_chirp_time_parameters[] = {
    {"i", "start-to-start, 1..3600 s", "30", "i=30"},
    {"r", "chirps per sequence, 1..10", "2", "r=2"},
    {"p", "silence between chirps, 1..10 s", "5", "p=5"},
    {"d", "each chirp length, 1..5 s", "3", "d=3"},
};
static const char *const help_batt_chirp_time_interactions[] = {
    "Changes take effect immediately; an active critical sequence restarts.",
    "All chirps stop immediately when the battery is no longer critical.",
    "Sequences never overlap; an overdue sequence starts after the prior one finishes.",
};
static const char *const help_batt_chirp_time_notes[] = {
    "Configures the audible sequence triggered below the battery-critical threshold.",
    "All interval, pause, and duration values are in seconds.",
    "Example i=30: a new sequence is scheduled every 30 seconds. Example r=2: every sequence contains two chirps.",
    "The pause starts when one chirp ends and finishes when the next chirp starts. There are r-1 pauses; pause is irrelevant when repeat is one.",
    "/s saves range, warning, critical, frequency, and timing defaults to flash.",
    "Example: batt chirp i=30 r=2 p=5 d=2. Every 30 s: two 2 s chirps separated by 5 s of silence.",
    "Sequence duration = (d * r) + (p * (r - 1)).",
    "Silence after a sequence = i - sequence duration, when the result is positive.",
    "For i=30 r=2 p=5 d=3: chirp 0-3, pause 3-8, chirp 8-11, silence 11-30.",
    "Example: batt chirp i=10 r=3 p=2 d=1. Every 10 s: three 1 s chirps with 2 s silence between chirps.",
    "Example: batt chirp i=60 r=1 p=10 d=4. Every 60 s: one 4 s chirp; pause is unused.",
    "Example: batt chirp i=120 r=1 p=1 d=5 /s. Every 2 minutes: one 5 s chirp, saved as the power-on default.",
    "Invalid or missing fields use defaults: i=30 r=2 p=5 d=3.",
    "Example: i=0 r=0 p=0 d=0 becomes i=30 r=2 p=5 d=3.",
    "Reset RAM: batt chirp i=30 r=2 p=5 d=3.",
    "Status: batt chirp time - report current interval, repeat, pause, duration, and source.",
    "Example: batt chirp i=30 r=2 p=5 d=2 /s",
};
static const debug_help_document_t help_batt_chirp_time = {
    .name = "BATT CHIRP TIME", .purpose = "configure the critical-battery audible sequence",
    SYNTAX(help_batt_chirp_time_syntax),
    PARAMETERS(help_batt_chirp_time_parameters),
    INTERACTIONS(help_batt_chirp_time_interactions),
    NOTES(help_batt_chirp_time_notes),
};

static const char *const help_load_syntax[] = {
    "load",
};
static const char *const help_load_notes[] = {
    "read-only",
    "Inrush is a sampled lower bound; bench thresholds remain disabled until measured.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_load = {
    .name = "LOAD", .purpose = "report load diagnostics",
    SYNTAX(help_load_syntax),
    NOTES(help_load_notes),
};

static const char *const help_ina_syntax[] = {
    "ina",
};
static const char *const help_ina_notes[] = {
    "read-only",
    "Shows computed calibration, conversion configuration and MODE 000 idle state.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ina = {
    .name = "INA", .purpose = "report INA219 configuration",
    SYNTAX(help_ina_syntax),
    NOTES(help_ina_notes),
};

static const char *const help_ds3231_syntax[] = {
    "ds3231 <start|temp|timer|stop>",
    "ds3231 timeset <15..18000> [/s]",
};
static const debug_help_parameter_t help_ds3231_parameters[] = {
    {"start", "start one event", "-", "DS3231 start"},
    {"stop", "disarm timer", "-", "DS3231 stop"},
    {"temp", "read temperature", "-", "DS3231 temp"},
    {"timer", "show countdown", "-", "DS3231 timer"},
    {"timeset", "15..18000 seconds", "-", "DS3231 timeset 1800 /s"},
};
static const char *const help_ds3231_interactions[] = {
    "Controls the one-shot RTC event timer; it remains stopped after boot and after an event.",
};
static const char *const help_ds3231_notes[] = {
    "start, temp, timer, timeset, or stop",
    "Example: DS3231 start | temp | timer | timeset 1800 | stop",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231 = {
    .name = "DS3231", .purpose = "control the one-shot RTC event timer",
    SYNTAX(help_ds3231_syntax),
    PARAMETERS(help_ds3231_parameters),
    INTERACTIONS(help_ds3231_interactions),
    NOTES(help_ds3231_notes),
};

static const char *const help_ds3231_start_syntax[] = {
    "ds3231 start",
};
static const char *const help_ds3231_start_interactions[] = {
    "Starts one event using the configured interval restored from flash.",
};
static const char *const help_ds3231_start_notes[] = {
    "no additional arguments",
    "Example: DS3231 start",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231_start = {
    .name = "DS3231 START", .purpose = "start one event using the configured interval",
    SYNTAX(help_ds3231_start_syntax),
    INTERACTIONS(help_ds3231_start_interactions),
    NOTES(help_ds3231_start_notes),
};

static const char *const help_ds3231_temp_syntax[] = {
    "ds3231 temp",
};
static const char *const help_ds3231_temp_notes[] = {
    "read-only",
    "Example: DS3231 temp",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231_temp = {
    .name = "DS3231 TEMP", .purpose = "Reads the DS3231 temperature registers over the shared I2C bus.",
    SYNTAX(help_ds3231_temp_syntax),
    NOTES(help_ds3231_temp_notes),
};

static const char *const help_ds3231_stop_syntax[] = {
    "ds3231 stop",
};
static const char *const help_ds3231_stop_notes[] = {
    "no additional arguments",
    "Example: DS3231 stop",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231_stop = {
    .name = "DS3231 STOP", .purpose = "Disarms the current DS3231 event timer.",
    SYNTAX(help_ds3231_stop_syntax),
    NOTES(help_ds3231_stop_notes),
};

static const char *const help_ds3231_timer_syntax[] = {
    "ds3231 timer",
};
static const char *const help_ds3231_timer_interactions[] = {
    "Displays the active one-shot countdown every second until q or Q is received.",
};
static const char *const help_ds3231_timer_notes[] = {
    "read-only",
    "Example: DS3231 timer",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231_timer = {
    .name = "DS3231 TIMER", .purpose = "display the active one-shot countdown",
    SYNTAX(help_ds3231_timer_syntax),
    INTERACTIONS(help_ds3231_timer_interactions),
    NOTES(help_ds3231_timer_notes),
};

static const char *const help_ds3231_timeset_syntax[] = {
    "ds3231 timeset <15..18000> [/s]",
};
static const char *const help_ds3231_timeset_interactions[] = {
    "Sets the interval; re-arms if running, and /s saves it for DS3231 start.",
};
static const char *const help_ds3231_timeset_notes[] = {
    "15 to 18000 seconds; optional /s",
    "Example: DS3231 timeset 1800 /s",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ds3231_timeset = {
    .name = "DS3231 TIMESET", .purpose = "set the event interval",
    SYNTAX(help_ds3231_timeset_syntax),
    INTERACTIONS(help_ds3231_timeset_interactions),
    NOTES(help_ds3231_timeset_notes),
};

static const char *const help_adc0offset_syntax[] = {
    "adc0offset",
    "ADC0OFFSET=<-200..+200>MV [/s]",
};
static const char *const help_adc0offset_interactions[] = {
    "Adds a calibration correction before battery filtering; /s saves all battery settings to flash.",
};
static const char *const help_adc0offset_notes[] = {
    "signed offset -200 to +200 mV; optional /s",
    "Example: ADC0OFFSET=+45MV /s",
    "Without /s the setting is RAM-only; append /s to save all battery defaults in flash.",
};
static const debug_help_document_t help_adc0offset = {
    .name = "ADC0OFFSET", .purpose = "set the battery calibration correction",
    SYNTAX(help_adc0offset_syntax),
    INTERACTIONS(help_adc0offset_interactions),
    NOTES(help_adc0offset_notes),
};

static const char *const help_co2_syntax[] = {
    "co2",
};
static const char *const help_co2_interactions[] = {
    "Shows the filtered and raw SCD41 measurement; single mode starts a 5-second shot.",
};
static const char *const help_co2_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2 = {
    .name = "CO2", .purpose = "show the SCD41 measurement",
    SYNTAX(help_co2_syntax),
    INTERACTIONS(help_co2_interactions),
    NOTES(help_co2_notes),
};

static const char *const help_co2living_syntax[] = {
    "co2living",
};
static const char *const help_co2living_notes[] = {
    "read-only",
    "Lists all five living-room CO2 ranges with stations and air-quality functions.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2living = {
    .name = "CO2LIVING", .purpose = "list the five living-room CO2 ranges",
    SYNTAX(help_co2living_syntax),
    NOTES(help_co2living_notes),
};

static const char *const help_co2sleeping_syntax[] = {
    "co2sleeping",
};
static const char *const help_co2sleeping_notes[] = {
    "read-only",
    "Lists all five sleeping-room CO2 ranges with stations and air-quality functions.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2sleeping = {
    .name = "CO2SLEEPING", .purpose = "list the five sleeping-room CO2 ranges",
    SYNTAX(help_co2sleeping_syntax),
    NOTES(help_co2sleeping_notes),
};

static const char *const help_co2cfg_syntax[] = {
    "co2cfg",
};
static const char *const help_co2cfg_notes[] = {
    "read-only",
    "Shows both five-level CO2 profiles, the active selection, and flash source.",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2cfg = {
    .name = "CO2CFG", .purpose = "show both profiles and the active selection",
    SYNTAX(help_co2cfg_syntax),
    NOTES(help_co2cfg_notes),
};

static const char *const help_co2sim_syntax[] = {
    "co2sim=<200..6000>",
    "co2sim off",
};
static const char *const help_co2sim_interactions[] = {
    "Replaces SCD41 readings and exercises normal CO2-to-station control until off.",
};
static const char *const help_co2sim_notes[] = {
    "200 to 6000 ppm, or off",
    "Example: co2sim=440",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2sim = {
    .name = "CO2SIM", .purpose = "simulate CO2 readings",
    SYNTAX(help_co2sim_syntax),
    INTERACTIONS(help_co2sim_interactions),
    NOTES(help_co2sim_notes),
};

static const char *const help_co2limit_syntax[] = {
    "co2limit <profile> <level 1-4> <max ppm>",
};
static const debug_help_parameter_t help_co2limit_parameters[] = {
    {"profile", "profile to edit", "-", "co2limit living 2 999"},
    {"level", "1..4; 5 open-ended", "-", "co2limit living 2 999"},
    {"max ppm", "upper bound", "-", "co2limit living 2 999"},
};
static const char *const help_co2limit_interactions[] = {
    "Changes one range boundary in RAM; level 5 remains open-ended.",
};
static const char *const help_co2limit_notes[] = {
    "profile, level 1-4, maximum ppm",
    "Example: co2limit living 2 999",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2limit = {
    .name = "CO2LIMIT", .purpose = "edit one CO2 range boundary",
    SYNTAX(help_co2limit_syntax),
    PARAMETERS(help_co2limit_parameters),
    INTERACTIONS(help_co2limit_interactions),
    NOTES(help_co2limit_notes),
};

static const char *const help_co2save_syntax[] = {
    "co2save",
};
static const char *const help_co2save_interactions[] = {
    "Saves both profiles and the active profile to dedicated flash storage.",
};
static const char *const help_co2save_notes[] = {
    "idle controller",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2save = {
    .name = "CO2SAVE", .purpose = "save CO2 profiles to flash",
    SYNTAX(help_co2save_syntax),
    INTERACTIONS(help_co2save_interactions),
    NOTES(help_co2save_notes),
};

static const char *const help_co2defaults_syntax[] = {
    "co2defaults",
};
static const char *const help_co2defaults_interactions[] = {
    "Restores schematic CO2 defaults in RAM; use co2save to persist them.",
};
static const char *const help_co2defaults_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_co2defaults = {
    .name = "CO2DEFAULTS", .purpose = "restore schematic CO2 defaults",
    SYNTAX(help_co2defaults_syntax),
    INTERACTIONS(help_co2defaults_interactions),
    NOTES(help_co2defaults_notes),
};

static const char *const help_ready_syntax[] = {
    "ready",
};
static const char *const help_ready_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_ready = {
    .name = "READY", .purpose = "Shows and clears the SCD41 latched data-ready indication.",
    SYNTAX(help_ready_syntax),
    NOTES(help_ready_notes),
};

static const char *const help_asc_syntax[] = {
    "asc",
    "asc <on|off>",
};
static const char *const help_asc_notes[] = {
    "on, off, or no argument",
    "Example: asc off",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_asc = {
    .name = "ASC", .purpose = "Reads or sets SCD41 automatic self-calibration.",
    SYNTAX(help_asc_syntax),
    NOTES(help_asc_notes),
};

static const char *const help_offset_syntax[] = {
    "offset",
    "offset <0..20 degrees C>",
};
static const char *const help_offset_notes[] = {
    "0 to 20 degrees C, or no argument",
    "Example: offset 4.5",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_offset = {
    .name = "OFFSET", .purpose = "Reads or sets the SCD41 temperature offset.",
    SYNTAX(help_offset_syntax),
    NOTES(help_offset_notes),
};

static const char *const help_altitude_syntax[] = {
    "altitude",
    "altitude <0..3000 metres>",
};
static const char *const help_altitude_notes[] = {
    "0 to 3000 metres, or no argument",
    "Example: altitude 12",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_altitude = {
    .name = "ALTITUDE", .purpose = "Reads or sets SCD41 altitude compensation.",
    SYNTAX(help_altitude_syntax),
    NOTES(help_altitude_notes),
};

static const char *const help_mode_syntax[] = {
    "mode",
    "mode <periodic|single>",
};
static const char *const help_mode_notes[] = {
    "periodic, single, or no argument",
    "Example: mode single",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_mode = {
    .name = "MODE", .purpose = "Reads or selects the SCD41 measurement mode.",
    SYNTAX(help_mode_syntax),
    NOTES(help_mode_notes),
};

static const char *const help_sdc41_syntax[] = {
    "sdc41 <on|off>",
};
static const char *const help_sdc41_notes[] = {
    "on or off",
    "Example: sdc41 off",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_sdc41 = {
    .name = "SDC41", .purpose = "Uses the SCD41 protocol power-down or wake command.",
    SYNTAX(help_sdc41_syntax),
    NOTES(help_sdc41_notes),
};

static const char *const help_menu_syntax[] = {
    "menu",
};
static const char *const help_menu_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_menu = {
    .name = "MENU", .purpose = "Redraws the complete live SCD41 Page-5 menu.",
    SYNTAX(help_menu_syntax),
    NOTES(help_menu_notes),
};

static const char *const help_clean_syntax[] = {
    "clean",
};
static const char *const help_clean_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_clean = {
    .name = "CLEAN", .purpose = "Clears command results and redraws the fixed-screen debug interface.",
    SYNTAX(help_clean_syntax),
    NOTES(help_clean_notes),
};

static const char *const help_clear_syntax[] = {
    "clear",
};
static const char *const help_clear_interactions[] = {
    "Erases only output below Command >; preserves the current menu and Page-7 log. New events can appear afterward.",
};
static const char *const help_clear_notes[] = {
    "no arguments; fixed-screen mode",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_clear = {
    .name = "CLEAR", .purpose = "clear output below Command >",
    SYNTAX(help_clear_syntax),
    INTERACTIONS(help_clear_interactions),
    NOTES(help_clear_notes),
};

static const char *const help_plain_syntax[] = {
    "plain",
};
static const char *const help_plain_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_plain = {
    .name = "PLAIN", .purpose = "Switches to line-oriented output without escape codes.",
    SYNTAX(help_plain_syntax),
    NOTES(help_plain_notes),
};

static const char *const help_exit_syntax[] = {
    "exit",
};
static const char *const help_exit_notes[] = {
    "no arguments",
    "Debug-only diagnostic interface; command effects are RAM-only unless explicitly stated.",
};
static const debug_help_document_t help_exit = {
    .name = "EXIT", .purpose = "Leaves the debug console safely.",
    SYNTAX(help_exit_syntax),
    NOTES(help_exit_notes),
};

static const char *const help_page5_co2_syntax[] = {
    "co2",
};
static const char *const help_page5_co2_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2 = {
    .name = "CO2", .purpose = "latest periodic reading or 5-second single shot",
    SYNTAX(help_page5_co2_syntax),
    NOTES(help_page5_co2_notes),
};

static const char *const help_page5_co2living_syntax[] = {
    "co2living",
};
static const char *const help_page5_co2living_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2living = {
    .name = "CO2LIVING", .purpose = "list all five living-room ranges, stations, and functions",
    SYNTAX(help_page5_co2living_syntax),
    NOTES(help_page5_co2living_notes),
};

static const char *const help_page5_co2sleeping_syntax[] = {
    "co2sleeping",
};
static const char *const help_page5_co2sleeping_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2sleeping = {
    .name = "CO2SLEEPING", .purpose = "list all five sleeping-room ranges, stations, and functions",
    SYNTAX(help_page5_co2sleeping_syntax),
    NOTES(help_page5_co2sleeping_notes),
};

static const char *const help_page5_co2cfg_syntax[] = {
    "co2cfg",
};
static const char *const help_page5_co2cfg_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2cfg = {
    .name = "CO2CFG", .purpose = "show GP10-selected profile, both profiles, source, and mapped level",
    SYNTAX(help_page5_co2cfg_syntax),
    NOTES(help_page5_co2cfg_notes),
};

static const char *const help_page5_co2limit_syntax[] = {
    "co2limit <profile> <level 1-4> <max ppm>",
};
static const debug_help_parameter_t help_page5_co2limit_parameters[] = {
    {"profile", "profile to edit", "-", "co2limit living 2 999"},
    {"level", "1..4", "-", "co2limit living 2 999"},
    {"max ppm", "upper bound in RAM", "-", "co2limit living 2 999"},
};
static const char *const help_page5_co2limit_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2limit = {
    .name = "CO2LIMIT", .purpose = "edit one upper bound in RAM",
    SYNTAX(help_page5_co2limit_syntax),
    PARAMETERS(help_page5_co2limit_parameters),
    NOTES(help_page5_co2limit_notes),
};

static const char *const help_page5_co2save_syntax[] = {
    "co2save",
};
static const char *const help_page5_co2save_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2save = {
    .name = "CO2SAVE", .purpose = "save both profile limits to flash",
    SYNTAX(help_page5_co2save_syntax),
    NOTES(help_page5_co2save_notes),
};

static const char *const help_page5_co2defaults_syntax[] = {
    "co2defaults",
};
static const char *const help_page5_co2defaults_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_co2defaults = {
    .name = "CO2DEFAULTS", .purpose = "restore schematic defaults in RAM",
    SYNTAX(help_page5_co2defaults_syntax),
    NOTES(help_page5_co2defaults_notes),
};

static const char *const help_page5_ready_syntax[] = {
    "ready",
};
static const char *const help_page5_ready_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_ready = {
    .name = "READY", .purpose = "show and clear latched data-ready state",
    SYNTAX(help_page5_ready_syntax),
    NOTES(help_page5_ready_notes),
};

static const char *const help_page5_selftest_syntax[] = {
    "selftest",
};
static const char *const help_page5_selftest_interactions[] = {
    "stop measurement and run the approximately 10-second sensor test",
};
static const char *const help_page5_selftest_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_selftest = {
    .name = "SELFTEST", .purpose = "run the SCD41 sensor test",
    SYNTAX(help_page5_selftest_syntax),
    INTERACTIONS(help_page5_selftest_interactions),
    NOTES(help_page5_selftest_notes),
};

static const char *const help_page5_asc_syntax[] = {
    "asc [on|off]",
};
static const char *const help_page5_asc_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_asc = {
    .name = "ASC", .purpose = "read or set automatic self-calibration",
    SYNTAX(help_page5_asc_syntax),
    NOTES(help_page5_asc_notes),
};

static const char *const help_page5_offset_syntax[] = {
    "offset [degrees]",
};
static const char *const help_page5_offset_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_offset = {
    .name = "OFFSET", .purpose = "read or set 0..20 C temperature offset",
    SYNTAX(help_page5_offset_syntax),
    NOTES(help_page5_offset_notes),
};

static const char *const help_page5_altitude_syntax[] = {
    "altitude [metres]",
};
static const char *const help_page5_altitude_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_altitude = {
    .name = "ALTITUDE", .purpose = "read or set 0..3000 m",
    SYNTAX(help_page5_altitude_syntax),
    NOTES(help_page5_altitude_notes),
};

static const char *const help_page5_mode_syntax[] = {
    "mode [periodic|single]",
};
static const char *const help_page5_mode_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_mode = {
    .name = "MODE", .purpose = "read or select measurement mode",
    SYNTAX(help_page5_mode_syntax),
    NOTES(help_page5_mode_notes),
};

static const char *const help_page5_status_syntax[] = {
    "status",
};
static const char *const help_page5_status_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_status = {
    .name = "STATUS", .purpose = "show SCD41 mode, ASC, samples, and ready state",
    SYNTAX(help_page5_status_syntax),
    NOTES(help_page5_status_notes),
};

static const char *const help_page5_sdc41_syntax[] = {
    "sdc41 <on|off>",
};
static const char *const help_page5_sdc41_interactions[] = {
    "protocol power-down or wake/start",
};
static const char *const help_page5_sdc41_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_sdc41 = {
    .name = "SDC41", .purpose = "protocol power-down or wake/start",
    SYNTAX(help_page5_sdc41_syntax),
    INTERACTIONS(help_page5_sdc41_interactions),
    NOTES(help_page5_sdc41_notes),
};

static const char *const help_page5_menu_syntax[] = {
    "menu",
};
static const char *const help_page5_menu_notes[] = {
    "SDC41 Page-5 command.",
};
static const debug_help_document_t help_page5_menu = {
    .name = "MENU", .purpose = "redraw Page 5 live SCD41 menu",
    SYNTAX(help_page5_menu_syntax),
    NOTES(help_page5_menu_notes),
};

static const char *const help_duty_normal_syntax[] = {
    "cfg DUTY_NORMAL <value>",
    "cfg -> live value and limits",
};
static const char *const help_duty_normal_notes[] = {
    "DUTY_NORMAL is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_duty_normal = {
    .name = "DUTY_NORMAL", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_duty_normal_syntax),
    NOTES(help_duty_normal_notes),
};

static const char *const help_duty_approach_syntax[] = {
    "cfg DUTY_APPROACH <value>",
    "cfg -> live value and limits",
};
static const char *const help_duty_approach_notes[] = {
    "DUTY_APPROACH is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_duty_approach = {
    .name = "DUTY_APPROACH", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_duty_approach_syntax),
    NOTES(help_duty_approach_notes),
};

static const char *const help_duty_creep_syntax[] = {
    "cfg DUTY_CREEP <value>",
    "cfg -> live value and limits",
};
static const char *const help_duty_creep_notes[] = {
    "DUTY_CREEP is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_duty_creep = {
    .name = "DUTY_CREEP", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_duty_creep_syntax),
    NOTES(help_duty_creep_notes),
};

static const char *const help_duty_min_syntax[] = {
    "cfg DUTY_MIN <value>",
    "cfg -> live value and limits",
};
static const char *const help_duty_min_notes[] = {
    "DUTY_MIN is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_duty_min = {
    .name = "DUTY_MIN", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_duty_min_syntax),
    NOTES(help_duty_min_notes),
};

static const char *const help_approach_counts_syntax[] = {
    "cfg APPROACH_COUNTS <value>",
    "cfg -> live value and limits",
};
static const char *const help_approach_counts_notes[] = {
    "APPROACH_COUNTS is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_approach_counts = {
    .name = "APPROACH_COUNTS", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_approach_counts_syntax),
    NOTES(help_approach_counts_notes),
};

static const char *const help_pos_window_syntax[] = {
    "cfg POS_WINDOW <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_window_interactions[] = {
    "For oscillation, reducing DUTY_APPROACH is usually better.",
};
static const char *const help_pos_window_notes[] = {
    "Example: cfg POS_WINDOW 40 - tighter stopping",
    "Example: cfg POS_WINDOW 80 - looser, earlier arrival",
};
static const debug_help_document_t help_pos_window = {
    .name = "POS_WINDOW", .purpose = "how close counts as arrived",
    SYNTAX(help_pos_window_syntax),
    INTERACTIONS(help_pos_window_interactions),
    NOTES(help_pos_window_notes),
};

static const char *const help_debounce_ms_syntax[] = {
    "cfg DEBOUNCE_MS <value>",
    "cfg -> live value and limits",
};
static const char *const help_debounce_ms_notes[] = {
    "DEBOUNCE_MS is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_debounce_ms = {
    .name = "DEBOUNCE_MS", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_debounce_ms_syntax),
    NOTES(help_debounce_ms_notes),
};

static const char *const help_brake_hold_ms_syntax[] = {
    "cfg BRAKE_HOLD_MS <value>",
    "cfg -> live value and limits",
};
static const char *const help_brake_hold_ms_notes[] = {
    "BRAKE_HOLD_MS is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_brake_hold_ms = {
    .name = "BRAKE_HOLD_MS", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_brake_hold_ms_syntax),
    NOTES(help_brake_hold_ms_notes),
};

static const char *const help_pos_1_adc_syntax[] = {
    "cfg POS_1_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_1_adc_notes[] = {
    "POS_1_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_1_adc = {
    .name = "POS_1_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_1_adc_syntax),
    NOTES(help_pos_1_adc_notes),
};

static const char *const help_pos_2_adc_syntax[] = {
    "cfg POS_2_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_2_adc_notes[] = {
    "POS_2_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_2_adc = {
    .name = "POS_2_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_2_adc_syntax),
    NOTES(help_pos_2_adc_notes),
};

static const char *const help_pos_3_adc_syntax[] = {
    "cfg POS_3_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_3_adc_notes[] = {
    "POS_3_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_3_adc = {
    .name = "POS_3_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_3_adc_syntax),
    NOTES(help_pos_3_adc_notes),
};

static const char *const help_pos_4_adc_syntax[] = {
    "cfg POS_4_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_4_adc_notes[] = {
    "POS_4_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_4_adc = {
    .name = "POS_4_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_4_adc_syntax),
    NOTES(help_pos_4_adc_notes),
};

static const char *const help_pos_5_adc_syntax[] = {
    "cfg POS_5_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_5_adc_notes[] = {
    "POS_5_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_5_adc = {
    .name = "POS_5_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_5_adc_syntax),
    NOTES(help_pos_5_adc_notes),
};

static const char *const help_pos_6_adc_syntax[] = {
    "cfg POS_6_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_pos_6_adc_notes[] = {
    "POS_6_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_pos_6_adc = {
    .name = "POS_6_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_pos_6_adc_syntax),
    NOTES(help_pos_6_adc_notes),
};

static const char *const help_low_endstop_adc_syntax[] = {
    "cfg LOW_ENDSTOP_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_low_endstop_adc_notes[] = {
    "LOW_ENDSTOP_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_low_endstop_adc = {
    .name = "LOW_ENDSTOP_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_low_endstop_adc_syntax),
    NOTES(help_low_endstop_adc_notes),
};

static const char *const help_high_endstop_adc_syntax[] = {
    "cfg HIGH_ENDSTOP_ADC <value>",
    "cfg -> live value and limits",
};
static const char *const help_high_endstop_adc_notes[] = {
    "HIGH_ENDSTOP_ADC is runtime-settable; type cfg for live value and limits",
    "RAM-only; lost on reset. export prints config.h station lines.",
};
static const debug_help_document_t help_high_endstop_adc = {
    .name = "HIGH_ENDSTOP_ADC", .purpose = "inspect or change this runtime setting",
    SYNTAX(help_high_endstop_adc_syntax),
    NOTES(help_high_endstop_adc_notes),
};

static const struct {
  const char *key;
  uint8_t page;
  const debug_help_document_t *document;
} catalog[] = {
    {"co2", 5u, &help_page5_co2},
    {"co2living", 5u, &help_page5_co2living},
    {"co2sleeping", 5u, &help_page5_co2sleeping},
    {"co2cfg", 5u, &help_page5_co2cfg},
    {"co2limit", 5u, &help_page5_co2limit},
    {"co2save", 5u, &help_page5_co2save},
    {"co2defaults", 5u, &help_page5_co2defaults},
    {"ready", 5u, &help_page5_ready},
    {"selftest", 5u, &help_page5_selftest},
    {"asc", 5u, &help_page5_asc},
    {"offset", 5u, &help_page5_offset},
    {"altitude", 5u, &help_page5_altitude},
    {"mode", 5u, &help_page5_mode},
    {"status", 5u, &help_page5_status},
    {"sdc41", 5u, &help_page5_sdc41},
    {"menu", 5u, &help_page5_menu},
    {"help", 0u, &help_help},
    {"diag", 0u, &help_diag},
    {"sel", 0u, &help_sel},
    {"jog", 0u, &help_jog},
    {"step", 0u, &help_step},
    {"save", 0u, &help_save},
    {"stations", 0u, &help_stations},
    {"limits", 0u, &help_limits},
    {"lowendstop", 0u, &help_lowendstop},
    {"highendstop", 0u, &help_highendstop},
    {"export", 0u, &help_export},
    {"reset", 0u, &help_reset},
    {"bootsel", 0u, &help_bootsel},
    {"move", 0u, &help_move},
    {"pos", 0u, &help_pos},
    {"goto", 0u, &help_goto},
    {"home", 0u, &help_home},
    {"stop", 0u, &help_stop},
    {"adc", 0u, &help_adc},
    {"angle", 0u, &help_angle},
    {"led", 0u, &help_led},
    {"led zone", 0u, &help_led_zone},
    {"buzzer", 0u, &help_buzzer},
    {"buzzer crips-1", 0u, &help_buzzer_crips_1},
    {"buzzer crips-2", 0u, &help_buzzer_crips_2},
    {"buzzer crips-4", 0u, &help_buzzer_crips_4},
    {"buzzer crips-5", 0u, &help_buzzer_crips_5},
    {"buzzer tone-2", 0u, &help_buzzer_tone_2},
    {"buzzer tone-3", 0u, &help_buzzer_tone_3},
    {"page", 0u, &help_page},
    {"selftest", 0u, &help_selftest},
    {"tick", 0u, &help_tick},
    {"trace", 0u, &help_trace},
    {"pins", 0u, &help_pins},
    {"pwm", 0u, &help_pwm},
    {"cfg", 0u, &help_cfg},
    {"sim", 0u, &help_sim},
    {"cal", 0u, &help_cal},
    {"arm", 0u, &help_arm},
    {"disarm", 0u, &help_disarm},
    {"drive", 0u, &help_drive},
    {"findmin", 0u, &help_findmin},
    {"batt", 0u, &help_batt},
    {"batt raw", 0u, &help_batt_raw},
    {"batt res", 0u, &help_batt_res},
    {"batt log", 0u, &help_batt_log},
    {"batt events", 0u, &help_batt_events},
    {"batt reset", 0u, &help_batt_reset},
    {"batt sim", 0u, &help_batt_sim},
    {"batt sim range", 0u, &help_batt_sim_range},
    {"batt sim warning", 0u, &help_batt_sim_warning},
    {"batt sim critical", 0u, &help_batt_sim_critical},
    {"batt chirp time", 0u, &help_batt_chirp_time},
    {"load", 0u, &help_load},
    {"ina", 0u, &help_ina},
    {"ds3231", 0u, &help_ds3231},
    {"ds3231 start", 0u, &help_ds3231_start},
    {"ds3231 temp", 0u, &help_ds3231_temp},
    {"ds3231 stop", 0u, &help_ds3231_stop},
    {"ds3231 timer", 0u, &help_ds3231_timer},
    {"ds3231 timeset", 0u, &help_ds3231_timeset},
    {"adc0offset", 0u, &help_adc0offset},
    {"co2", 0u, &help_co2},
    {"co2living", 0u, &help_co2living},
    {"co2sleeping", 0u, &help_co2sleeping},
    {"co2cfg", 0u, &help_co2cfg},
    {"co2sim", 0u, &help_co2sim},
    {"co2limit", 0u, &help_co2limit},
    {"co2save", 0u, &help_co2save},
    {"co2defaults", 0u, &help_co2defaults},
    {"ready", 0u, &help_ready},
    {"asc", 0u, &help_asc},
    {"offset", 0u, &help_offset},
    {"altitude", 0u, &help_altitude},
    {"mode", 0u, &help_mode},
    {"sdc41", 0u, &help_sdc41},
    {"menu", 0u, &help_menu},
    {"clean", 0u, &help_clean},
    {"clear", 0u, &help_clear},
    {"plain", 0u, &help_plain},
    {"exit", 0u, &help_exit},
    {"DUTY_NORMAL", 0u, &help_duty_normal},
    {"DUTY_APPROACH", 0u, &help_duty_approach},
    {"DUTY_CREEP", 0u, &help_duty_creep},
    {"DUTY_MIN", 0u, &help_duty_min},
    {"APPROACH_COUNTS", 0u, &help_approach_counts},
    {"POS_WINDOW", 0u, &help_pos_window},
    {"DEBOUNCE_MS", 0u, &help_debounce_ms},
    {"BRAKE_HOLD_MS", 0u, &help_brake_hold_ms},
    {"POS_1_ADC", 0u, &help_pos_1_adc},
    {"POS_2_ADC", 0u, &help_pos_2_adc},
    {"POS_3_ADC", 0u, &help_pos_3_adc},
    {"POS_4_ADC", 0u, &help_pos_4_adc},
    {"POS_5_ADC", 0u, &help_pos_5_adc},
    {"POS_6_ADC", 0u, &help_pos_6_adc},
    {"LOW_ENDSTOP_ADC", 0u, &help_low_endstop_adc},
    {"HIGH_ENDSTOP_ADC", 0u, &help_high_endstop_adc},
};
