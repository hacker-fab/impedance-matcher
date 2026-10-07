/*
 * impedance_matcher.ino
 * Impedance Matcher
 *
 * Dependencies (Arduino Library Manager):
 *   Adafruit SSD1306, Adafruit GFX, TMCStepper
 */

#include <Arduino.h>
#include "include/config.h"
#include "include/app_state.h"
#include "include/matching.h"
#include "include/encoder.h"
#include "include/ui.h"

// Global state
AppState state   = S_HOME;
OpMode   opMode  = MODE_AUTO;
bool     radioTX = false;

long  motor1Deg = 0;
long  motor2Deg = 0;
float motor1Rad = 0.0f;
float motor2Rad = 0.0f;
float lossGrad1 = 0.1f;
float lossGrad2 = 0.1f;

bool  atMatch   = false;
float lastVSWR  = 1.0f;
float lastFwdV  = 0.0f;
float lastRevV  = 0.0f;

bool csvStreamEnabled = false;

void setup() {
  analogReadResolution(12);
  Serial.begin(CONTROL_BAUD);
  while (!Serial && millis() < 3000)
    ;

  matching_init_motor_pins();
  matching_init_uart();
  encoder_init();

  if (!ui_init_display()) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(200);
    }
  }

  matching_homing();
}

void loop() {
  matching_tick();

  int  delta   = 0;
  bool pressed = false;
  encoder_poll(&delta, &pressed);

  const bool throttleDisplay =
      (opMode == MODE_AUTO && (state == S_HOME || state == S_METRICS));
  const bool userInput  = (delta != 0) || pressed;
  const bool doDraw     = ui_should_redraw(userInput, throttleDisplay);

  ui_tick(delta, pressed, doDraw);

  // OLED is throttled on AUTO home / metrics; skip fixed delay when not drawing
  // so matching_tick stays tight
  if (throttleDisplay && !doDraw) {
    yield();
  } else {
    delay(SCHED_UI_FRAME_MS);
  }
}
