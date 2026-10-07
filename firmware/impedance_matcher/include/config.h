#pragma once

// Pins
#define ENC_A        38
#define ENC_B        39
#define ENC_BTN      40
#define SCROLL_DIR   1  // +1 normal, -1 reversed
#define STEP_PIN_1   2
#define DIR_PIN_1    3
#define STEP_PIN_2   9
#define DIR_PIN_2    10
#define FWD_PIN      25
#define REV_PIN      24
#define TRANSMIT_PIN 31

// Comms
#define CONTROL_BAUD         500000    // USB serial to host (live.py / plot logging); keep in sync with telemetry.py BAUD
#define MATCHING_UART_BAUD   500000    // TMC2209 UART on Serial1
#define MATCHING_SERIAL_PORT Serial1

// Display
#define SCREEN_W 128
#define SCREEN_H 64

// Motion
#define MOTOR_MIN_DEG      0
#define MOTOR_MAX_DEG      180
#define MOTOR_STEP_SIZE    10
#define HOMING_BACKOFF_RAD (-PI)       // drive to hard stop
#define HOMING_CENTER_RAD  (PI / 2.0f) // re-center after homing

// Matching
#define VSWR_MATCH_ENTER 1.2f          // declare match below this; keep in sync with telemetry.py VSWR_MATCH
#define VSWR_MATCH_EXIT  1.4f          // drop match above this; keep in sync with telemetry.py VSWR_UNMATCH

// Scheduling/UI
#define DISPLAY_THROTTLE_AUTO_HOME_MS 300u
#define SCHED_UI_FRAME_MS             16u
#define SETTINGS_MENU_AFK_MS          15000u
