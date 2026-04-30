/*
 * impedance_matcher.ino
 * Impedance Matcher — Teensy 4.1
 *
 * Hardware:
 *   SSD1306 128x64 OLED via I2C (SDA=18, SCL=19 on Teensy 4.1)
 *   Rotary encoder with push button
 *   TMC2209 stepper drivers via UART (Serial1)
 *   Directional coupler on FWD_PIN / REV_PIN
 *
 * Dependencies (Arduino Library Manager):
 *   Adafruit SSD1306
 *   Adafruit GFX Library
 *   TMCStepper
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TMCStepper.h>

// ─── Pin Definitions ─────────────────────────────────────────────────────────
#define ENC_A        38
#define ENC_B        39
#define ENC_BTN      40
#define SCROLL_DIR   -1   // +1 normal, -1 reversed

#define STEP_PIN_1   2
#define DIR_PIN_1    3
#define STEP_PIN_2   9
#define DIR_PIN_2    10
#define FWD_PIN      25
#define REV_PIN      24
#define TRANSMIT_PIN 31

// ─── Display ─────────────────────────────────────────────────────────────────
#define SCREEN_W 128
#define SCREEN_H  64
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);

// ─── Enums — declared at file scope BEFORE any function definitions so the
//     Arduino IDE preprocessor sees them when it auto-generates prototypes. ──
enum AppState { S_HOME, S_MENU, S_MOTOR1, S_MOTOR2, S_METRICS };
enum OpMode   { MODE_AUTO, MODE_MANUAL };

// MenuID values used as plain `int` in all function signatures below to avoid
// the Arduino forward-declaration bug (enum not yet in scope at prototype time).
enum MenuID {
  M_MODE        = 0,
  M_MOTOR1      = 1,
  M_MOTOR2      = 2,
  M_ADV_METRICS = 3,
  M_BACK        = 4,
  M_COUNT       = 5
};

// ─── TMC2209 UART ────────────────────────────────────────────────────────────
#define SERIAL_PORT     Serial1
#define R_SENSE         0.11f
#define DRV_ADDRESS_1   0b00
#define DRV_ADDRESS_2   0b01

TMC2209Stepper driver1(&SERIAL_PORT, R_SENSE, DRV_ADDRESS_1);
TMC2209Stepper driver2(&SERIAL_PORT, R_SENSE, DRV_ADDRESS_2);

// ─── Motor / Algorithm Settings ──────────────────────────────────────────────
#define STALL_VALUE         140
#define STEP_DELAY          200
#define STREAM_PLOT_DATA    1

#define STEPS_PER_RAD       63.66197f
#define MICROSTEPS_PER_STEP 32
#define MAX_ROT             3.14159f    // PI rad = 180 deg mechanical limit
#define GRAD_SCALE          0.025f
#define GRAD_DEADBAND       0.001f
#define MAX_STEPSIZE        (PI / 36.0f)
#define MIN_STEPSIZE        (PI / 700.0f)

#define MOTOR_MIN_POS 0    // degrees — GUI range
#define MOTOR_MAX_POS 180  // degrees — GUI range

// ─── Application State ───────────────────────────────────────────────────────
AppState state   = S_HOME;
OpMode   opMode  = MODE_AUTO;
bool     radioTX = false;

// GUI motor positions in degrees (0–180), shown on display and controlled by encoder
long motor1Pos = 0;
long motor2Pos = 0;

// Internal motor positions in radians — authoritative for stepper commands
float motor1_pos = 0.0f;
float motor2_pos = 0.0f;

float dM1 = 0.1f;
float dM2 = 0.1f;

const float samp_num = 300.0f;
bool atMatch = false;

// Last sampled sensor values, updated by sampVSWR()
float lastVSWR = 1.0f;
float lastFwdV = 0.0f;
float lastRevV = 0.0f;

// OLED: prioritize matching over frame rate on home + AUTO (throttle I2C redraw).
#define DISPLAY_THROTTLE_AUTO_HOME_MS 300u
static uint32_t lastDisplayMs = 0;

// ─── ADC Helpers ─────────────────────────────────────────────────────────────
float analogReadMilliVolts(int pin) {
  return (analogRead(pin) / 4095.0f) * 3300.0f;
}

// ─── Motor Helpers ───────────────────────────────────────────────────────────
int round_up(float value) {
  int truncated = (int)value;
  return (value > truncated) ? truncated + 1 : truncated;
}

float stepsToRad(int steps) {
  return steps / (STEPS_PER_RAD * MICROSTEPS_PER_STEP);
}

int radToSteps(float rads) {
  return round_up(rads * (STEPS_PER_RAD * MICROSTEPS_PER_STEP));
}

void takeStep(int stepPin) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(STEP_DELAY / 2);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(STEP_DELAY / 2);
}

float turnByRad(TMC2209Stepper &driver, int stepPin, int dirPin,
                float rads, float &motor_pos, bool ignoreLimits = false) {
  (void)driver;
  if (rads == 0.0f) return 0.0f;

  bool  isNeg         = (rads < 0.0f);
  int   total_steps   = radToSteps(isNeg ? -rads : rads);
  float step_increment = stepsToRad(1);
  float pos_change    = isNeg ? -step_increment : step_increment;
  int   steps_taken   = 0;

  digitalWrite(dirPin, isNeg);

  for (int i = 0; i < total_steps; i++) {
    if (!ignoreLimits) {
      if ((motor_pos >= MAX_ROT && pos_change > 0.0f) ||
          (motor_pos <= 0.0f   && pos_change < 0.0f)) {
        Serial.println("MOTOR LIMIT REACHED");
        break;
      }
    }
    takeStep(stepPin);
    motor_pos += pos_change;
    steps_taken++;
  }
  return steps_taken * pos_change;
}

// ─── VSWR Sampling ───────────────────────────────────────────────────────────
float sampVSWR(int fwd, int rev) {
  float sum_fwd = 0.0f, sum_rev = 0.0f;
  for (int i = 0; i < (int)samp_num; i++) {
    sum_fwd += analogReadMilliVolts(fwd);
    sum_rev += analogReadMilliVolts(rev);
    delayMicroseconds(15);
  }
  float avgFwd = sum_fwd / samp_num;
  float avgRev = sum_rev / samp_num;

  lastFwdV = avgFwd / 1000.0f;
  lastRevV = avgRev / 1000.0f;

  float denom  = (avgFwd - avgRev);
  float vswr   = (denom != 0.0f) ? (avgFwd + avgRev) / denom : 99.0f;
  lastVSWR     = vswr;

  float loss = (vswr - 1.0f) * (vswr - 1.0f);

  if (vswr > 1.4f) atMatch = false;
  if (vswr < 1.2f) atMatch = true;

#if STREAM_PLOT_DATA
  Serial.print("VSWR_CSV,");
  Serial.print(millis());
  Serial.print(",");  Serial.print(vswr, 6);
  Serial.print(",");  Serial.print(lastFwdV, 6);
  Serial.print(",");  Serial.print(lastRevV, 6);
  Serial.print(",");  Serial.print(motor1_pos, 6);
  Serial.print(",");  Serial.print(motor2_pos, 6);
  Serial.print(",");  Serial.println(atMatch ? 1 : 0);
#endif

  return loss;
}

// ─── Auto-Tuning (Gradient Descent) ──────────────────────────────────────────
float clampMagnitude(float value, float minMag, float maxMag) {
  float mag  = (value < 0.0f) ? -value : value;
  float sign = (value < 0.0f) ? -1.0f  : 1.0f;
  if (mag > maxMag) return maxMag * sign;
  if (mag < minMag) return minMag * sign;
  return value;
}

void calcGradAndStep(TMC2209Stepper &driver, int stepPin, int dirPin,
                     float &gradient, float &motor_pos) {
  float initialCost   = sampVSWR(FWD_PIN, REV_PIN);
  float commandedStep = clampMagnitude(gradient * GRAD_SCALE, MIN_STEPSIZE, MAX_STEPSIZE);

  if (motor_pos >= MAX_ROT - MIN_STEPSIZE) commandedStep = -MIN_STEPSIZE;
  if (motor_pos <= MIN_STEPSIZE)           commandedStep =  MIN_STEPSIZE;

  float actualTravel = turnByRad(driver, stepPin, dirPin, commandedStep, motor_pos);
  delay(5);
  if (actualTravel != 0.0f)
    gradient = (initialCost - sampVSWR(FWD_PIN, REV_PIN)) / actualTravel;
}

// ─── GUI Sensor / Actuator Interface ─────────────────────────────────────────
float getVSWR()           { return lastVSWR; }
float getForwardVoltage() { return lastFwdV; }
float getReverseVoltage() { return lastRevV; }

void setMotor1Step(long posDeg) {
  float targetRad = posDeg * (PI / 180.0f);
  float delta     = targetRad - motor1_pos;
  turnByRad(driver1, STEP_PIN_1, DIR_PIN_1, delta, motor1_pos);
}

void setMotor2Step(long posDeg) {
  float targetRad = posDeg * (PI / 180.0f);
  float delta     = targetRad - motor2_pos;
  turnByRad(driver2, STEP_PIN_2, DIR_PIN_2, delta, motor2_pos);
}

void setRadioTX(bool en) {
  digitalWrite(TRANSMIT_PIN, en ? LOW : HIGH);
}

// ─── Encoder ─────────────────────────────────────────────────────────────────
volatile int encRaw = 0;
int encAccum = 0;
int lastEncA = HIGH;

void readEncoder() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);
  if (a != lastEncA) {
    encRaw += (a == b) ? -1 : 1;
    lastEncA = a;
  }
}

#define ENC_COUNTS_PER_CLICK 2  // raise to 4 if still too sensitive
int consumeDelta() {
  noInterrupts();
  int d = encRaw;
  encRaw = 0;
  interrupts();
  encAccum += d;
  int clicks = encAccum / ENC_COUNTS_PER_CLICK;
  encAccum -= clicks * ENC_COUNTS_PER_CLICK;
  return clicks * SCROLL_DIR;
}

// ─── Button ──────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS 40
bool     lastBtnRaw = HIGH;
bool     btnPending = false;
uint32_t lastBounce = 0;

void pollButton() {
  bool raw = digitalRead(ENC_BTN);
  if (raw == LOW && lastBtnRaw == HIGH && (millis() - lastBounce) > DEBOUNCE_MS) {
    btnPending = true;
    lastBounce = millis();
  }
  lastBtnRaw = raw;
}

bool consumeButton() {
  if (btnPending) { btnPending = false; return true; }
  return false;
}

// ─── Menu Helpers ────────────────────────────────────────────────────────────
int  menuSel          = 0;
int  homeSel          = 0;   // 0 = TX toggle, 1 = Settings
bool menuEditingMotor = false;
int  editingMotorId   = -1;
#define MOTOR_STEP_SIZE 10   // degrees per encoder detent — tune to taste

int buildMenu(int* out) {
  int n = 0;
  out[n++] = M_MODE;
  if (opMode == MODE_MANUAL) {
    out[n++] = M_MOTOR1;
    out[n++] = M_MOTOR2;
  }
  out[n++] = M_ADV_METRICS;
  out[n++] = M_BACK;
  return n;
}

const char* menuLabel(int id) {
  switch (id) {
    case M_MODE:        return "Mode";
    case M_MOTOR1:      return "Motor 1";
    case M_MOTOR2:      return "Motor 2";
    case M_ADV_METRICS: return "Advanced";
    case M_BACK:        return "< Back";
    default:            return "?";
  }
}

const char* menuValue(int id) {
  switch (id) {
    case M_MODE: return (opMode == MODE_AUTO) ? "AUTO" : "MANUAL";
    default:     return nullptr;
  }
}

// ─── Draw Routines ───────────────────────────────────────────────────────────
void drawHome() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Impedance Matcher");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 13);
  oled.print("VSWR");
  oled.setTextSize(2);
  oled.setCursor(0, 23);
  oled.print(getVSWR(), 3);

  oled.setTextSize(1);
  const char* modeStr = (opMode == MODE_AUTO) ? "AUTO" : "MANUAL";
  int modeLabelX = SCREEN_W - ((int)strlen("Mode:") * 6);
  int modeValueX = SCREEN_W - ((int)strlen(modeStr) * 6);
  oled.setCursor(modeLabelX, 13);
  oled.print("Mode:");
  oled.setCursor(modeValueX, 22);
  oled.print(modeStr);

  const int btnY = 49, btnW = 60, btnH = 14, gap = 8;
  for (int i = 0; i < 2; i++) {
    int  x   = i * (btnW + gap);
    bool sel = (homeSel == i);
    if (sel) {
      oled.fillRect(x, btnY, btnW, btnH, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.drawRect(x, btnY, btnW, btnH, SSD1306_WHITE);
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(x + 4, btnY + 4);
    oled.print(i == 0 ? (radioTX ? "TX:ON" : "TX:OFF") : "Settings");
  }

  oled.display();
}

void drawMenu() {
  int visible[M_COUNT];
  int count = buildMenu(visible);
  menuSel = constrain(menuSel, 0, count - 1);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Settings");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  const int ROWS  = 4;
  int       start = constrain(menuSel - ROWS + 1, 0, max(0, count - ROWS));

  for (int i = 0; i < ROWS && (start + i) < count; i++) {
    int  idx = start + i;
    int  id  = visible[idx];
    int  y   = 12 + i * 13;
    bool sel = (idx == menuSel);

    if (sel) {
      oled.fillRect(0, y - 1, 127, 12, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }

    oled.setCursor(3, y);
    oled.print(menuLabel(id));

    oled.setCursor(80, y);
    if (id == M_MOTOR1) {
      oled.print(motor1Pos);
      if (menuEditingMotor && editingMotorId == M_MOTOR1) oled.print("*");
    } else if (id == M_MOTOR2) {
      oled.print(motor2Pos);
      if (menuEditingMotor && editingMotorId == M_MOTOR2) oled.print("*");
    } else {
      const char* val = menuValue(id);
      if (val) oled.print(val);
    }
  }

  oled.setTextColor(SSD1306_WHITE);
  oled.display();
}

void drawMotorAdjust(int motorNum, long pos) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Motor ");
  oled.print(motorNum);
  oled.print(" (deg)");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(4, 20);
  oled.print("< ");
  oled.print(pos);
  oled.print(" >");

  oled.setTextSize(1);
  oled.setCursor(0, 50);
  oled.print("Turn:adj  Press:back");
  oled.display();
}

void drawAdvancedMetrics() {
  bool m1Max = (motor1Pos <= MOTOR_MIN_POS) || (motor1Pos >= MOTOR_MAX_POS);
  bool m2Max = (motor2Pos <= MOTOR_MIN_POS) || (motor2Pos >= MOTOR_MAX_POS);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Advanced Metrics");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  oled.setCursor(0, 12);
  oled.print("VSWR:");
  oled.print(getVSWR(), 3);

  oled.setCursor(0, 22);
  oled.print("FWD:");
  oled.print(getForwardVoltage(), 3);
  oled.print("V REV:");
  oled.print(getReverseVoltage(), 3);
  oled.print("V");

  oled.setCursor(0, 32);
  oled.print("M1:");
  oled.print(motor1_pos * (180.0f / PI), 1);
  oled.print("d ");
  oled.print(m1Max ? "MAX" : "OK");

  oled.setCursor(0, 42);
  oled.print("M2:");
  oled.print(motor2_pos * (180.0f / PI), 1);
  oled.print("d ");
  oled.print(m2Max ? "MAX" : "OK");

  oled.setCursor(0, 54);
  oled.print("Press: back");
  oled.display();
}

// ─── Input Handlers ──────────────────────────────────────────────────────────
void handleHome(int delta, bool pressed) {
  if (delta != 0) {
    homeSel = (homeSel + delta) % 2;
    if (homeSel < 0) homeSel += 2;
  }
  if (pressed) {
    if (homeSel == 0) {
      radioTX = !radioTX;
      setRadioTX(radioTX);
    } else {
      state   = S_MENU;
      menuSel = 0;
    }
  }
}

void handleMenu(int delta, bool pressed) {
  int visible[M_COUNT];
  int count = buildMenu(visible);

  if (delta != 0) {
    if (menuEditingMotor) {
      if (editingMotorId == M_MOTOR1) {
        motor1Pos += (long)delta * MOTOR_STEP_SIZE;
        motor1Pos = constrain(motor1Pos, MOTOR_MIN_POS, MOTOR_MAX_POS);
        setMotor1Step(motor1Pos);
      } else if (editingMotorId == M_MOTOR2) {
        motor2Pos += (long)delta * MOTOR_STEP_SIZE;
        motor2Pos = constrain(motor2Pos, MOTOR_MIN_POS, MOTOR_MAX_POS);
        setMotor2Step(motor2Pos);
      }
    } else if (count > 0) {
      menuSel = (menuSel + delta) % count;
      if (menuSel < 0) menuSel += count;
    }
  }

  if (pressed) {
    if (menuEditingMotor) {
      menuEditingMotor = false;
      editingMotorId   = -1;
      return;
    }
    int sel = visible[menuSel];
    switch (sel) {
      case M_MODE:
        opMode = (opMode == MODE_AUTO) ? MODE_MANUAL : MODE_AUTO;
        if (opMode == MODE_AUTO) { menuEditingMotor = false; editingMotorId = -1; }
        { int v2[M_COUNT]; menuSel = constrain(menuSel, 0, buildMenu(v2) - 1); }
        break;
      case M_MOTOR1:      menuEditingMotor = true; editingMotorId = M_MOTOR1; break;
      case M_MOTOR2:      menuEditingMotor = true; editingMotorId = M_MOTOR2; break;
      case M_ADV_METRICS: state = S_METRICS; break;
      case M_BACK:
        menuEditingMotor = false;
        editingMotorId   = -1;
        state = S_HOME;
        break;
    }
  }
}

void handleMotorAdjust(int motorNum, long& pos, int delta, bool pressed) {
  if (delta != 0) {
    pos += (long)delta * MOTOR_STEP_SIZE;
    pos = constrain(pos, MOTOR_MIN_POS, MOTOR_MAX_POS);
    if (motorNum == 1) setMotor1Step(pos);
    else               setMotor2Step(pos);
  }
  if (pressed) state = S_MENU;
}

void handleMetrics(int delta, bool pressed) {
  (void)delta;
  if (pressed) state = S_MENU;
}

// ─── Arduino Entry Points ─────────────────────────────────────────────────────
void setup() {
  analogReadResolution(12);
  Serial.begin(500000);
  while (!Serial && millis() < 3000);

  // Motor driver pins
  pinMode(STEP_PIN_1,   OUTPUT);
  pinMode(DIR_PIN_1,    OUTPUT);
  pinMode(STEP_PIN_2,   OUTPUT);
  pinMode(DIR_PIN_2,    OUTPUT);
  pinMode(TRANSMIT_PIN, OUTPUT);
  digitalWrite(TRANSMIT_PIN, HIGH);  // TX off by default

  SERIAL_PORT.begin(500000, SERIAL_8N1);
  delay(500);

  // Encoder / button
  pinMode(ENC_A,   INPUT_PULLUP);
  pinMode(ENC_B,   INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), readEncoder, CHANGE);

  // OLED
  Wire.begin();
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(200);
    }
  }
  oled.clearDisplay();
  oled.display();

  // Homing sequence — drive both motors to lower limit, zero, then center
  Serial.println("Starting Homing Sequence...");
  turnByRad(driver1, STEP_PIN_1, DIR_PIN_1, -PI, motor1_pos, true);
  turnByRad(driver2, STEP_PIN_2, DIR_PIN_2, -PI, motor2_pos, true);
  motor1_pos = 0.0f;
  motor2_pos = 0.0f;
  Serial.println("Finished Homing Sequence...");
  turnByRad(driver1, STEP_PIN_1, DIR_PIN_1, PI / 2.0f, motor1_pos, true);
  turnByRad(driver2, STEP_PIN_2, DIR_PIN_2, PI / 2.0f, motor2_pos, true);

  // Sync GUI degree positions to homed state
  motor1Pos = constrain((long)(motor1_pos * (180.0f / PI)), MOTOR_MIN_POS, MOTOR_MAX_POS);
  motor2Pos = constrain((long)(motor2_pos * (180.0f / PI)), MOTOR_MIN_POS, MOTOR_MAX_POS);
}

void loop() {
  if (opMode == MODE_AUTO) {
    if (!atMatch) {
      calcGradAndStep(driver1, STEP_PIN_1, DIR_PIN_1, dM1, motor1_pos);
      calcGradAndStep(driver2, STEP_PIN_2, DIR_PIN_2, dM2, motor2_pos);
    } else {
      sampVSWR(FWD_PIN, REV_PIN);
    }
    // Keep GUI display in sync with actual motor positions
    motor1Pos = constrain((long)(motor1_pos * (180.0f / PI)), MOTOR_MIN_POS, MOTOR_MAX_POS);
    motor2Pos = constrain((long)(motor2_pos * (180.0f / PI)), MOTOR_MIN_POS, MOTOR_MAX_POS);
  }

  pollButton();
  int  delta   = consumeDelta();
  bool pressed = consumeButton();

  const bool inAutoHome =
      (opMode == MODE_AUTO && state == S_HOME);
  const bool userInput = (delta != 0) || pressed;
  const bool slowRefreshDue =
      (lastDisplayMs == 0) ||
      ((millis() - lastDisplayMs) >= DISPLAY_THROTTLE_AUTO_HOME_MS);
  // Full UI rate anywhere except idle AUTO home; there only redraw on input or slow tick.
  const bool doDraw = userInput || !inAutoHome || slowRefreshDue;

  switch (state) {
    case S_HOME:
      handleHome(delta, pressed);
      if (doDraw) {
        drawHome();
        lastDisplayMs = millis();
      }
      break;
    case S_MENU:
      handleMenu(delta, pressed);
      if (doDraw) {
        drawMenu();
        lastDisplayMs = millis();
      }
      break;
    case S_MOTOR1:
      handleMotorAdjust(1, motor1Pos, delta, pressed);
      if (doDraw) {
        drawMotorAdjust(1, motor1Pos);
        lastDisplayMs = millis();
      }
      break;
    case S_MOTOR2:
      handleMotorAdjust(2, motor2Pos, delta, pressed);
      if (doDraw) {
        drawMotorAdjust(2, motor2Pos);
        lastDisplayMs = millis();
      }
      break;
    case S_METRICS:
      handleMetrics(delta, pressed);
      if (doDraw) {
        drawAdvancedMetrics();
        lastDisplayMs = millis();
      }
      break;
  }

  if (inAutoHome && !doDraw) {
    yield();  // matching runs again immediately
  } else {
    delay(16);  // ~60 fps when drawing or in menus / manual
  }
}
