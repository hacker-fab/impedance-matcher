#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── Pin Definitions ─────────────────────────────────────────────────────────
#define ENC_A    38
#define ENC_B    39
#define ENC_BTN  40
#define SCROLL_DIR -1  // +1 normal, -1 reversed

// ─── Display ─────────────────────────────────────────────────────────────────
#define SCREEN_W 128
#define SCREEN_H  64
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);

enum AppState { S_HOME, S_MENU, S_MOTOR1, S_MOTOR2, S_METRICS };
enum OpMode   { MODE_AUTO, MODE_MANUAL };

enum MenuID {
  M_MODE   = 0,
  M_MOTOR1 = 1,
  M_MOTOR2 = 2,
  M_ADV_METRICS = 3,
  M_BACK   = 4,
  M_COUNT  = 5   // array size sentinel
};

// ─── Application State ───────────────────────────────────────────────────────
AppState state     = S_HOME;
OpMode   opMode    = MODE_AUTO;
bool     radioTX   = false;
long     motor1Pos = 0;
long     motor2Pos = 0;
#define MOTOR_MIN_POS 0
#define MOTOR_MAX_POS 180

// ─── Encoder ─────────────────────────────────────────────────────────────────
volatile int encRaw = 0;
int encAccum = 0;
int lastEncA = HIGH;

void readEncoder() {
  digitalToggle(LED_BUILTIN); 
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);
  if (a != lastEncA) {
    encRaw += (a == b) ? -1 : 1;
    lastEncA = a;
  }
}

// Returns net detent clicks since last call
#define ENC_COUNTS_PER_CLICK 2  // lower for more sensitivity
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

// ─── Stubs — implement these in your own file or below ───────────────────────
float getVSWR()               { return 1.00f;  /* replace with ADC read */ }
float getForwardVoltage()     { return 0.00f;  /* replace with ADC read */ }
float getReverseVoltage()     { return 0.00f;  /* replace with ADC read */ }
void  setMotor1Step(long pos) { (void)pos;     /* STEP/DIR for motor 1  */ }
void  setMotor2Step(long pos) { (void)pos;     /* STEP/DIR for motor 2  */ }
void  setRadioTX(bool en)     { (void)en;      /* optoisolator KEY pin  */ }

// ─── Menu Helpers ────────────────────────────────────────────────────────────

int menuSel = 0;
int homeSel = 0;  // 0 = TX toggle, 1 = Settings
bool menuEditingMotor = false;
int  editingMotorId   = -1;
#define MOTOR_STEP_SIZE 10  // steps per encoder detent

// Fill out[] with currently visible item IDs; return count
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
    case M_MODE:   return "Mode";
    case M_MOTOR1: return "Motor 1";
    case M_MOTOR2: return "Motor 2";
    case M_ADV_METRICS: return "Advanced";
    case M_BACK:   return "< Back";
    default:       return "?";
  }
}

// Returns value string for items that show one, nullptr otherwise
const char* menuValue(int id) {
  switch (id) {
    case M_MODE:  return (opMode == MODE_AUTO) ? "AUTO" : "MANUAL";
    default:      return nullptr;
  }
}

// ─── Draw Routines ───────────────────────────────────────────────────────────
void drawHome() {
  float vswr = getVSWR();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // Header
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Impedance Matcher");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Large VSWR readout
  oled.setTextSize(1);
  oled.setCursor(0, 13);
  oled.print("VSWR");
  oled.setTextSize(2);
  oled.setCursor(0, 23);
  oled.print(vswr, 3);

  // Status block
  oled.setTextSize(1);
  const char* modeStr = (opMode == MODE_AUTO) ? "AUTO" : "MANUAL";
  int modeLabelX = SCREEN_W - ((int)strlen("Mode") * 6);
  int modeValueX = SCREEN_W - ((int)strlen(modeStr) * 6);
  oled.setCursor(modeLabelX, 13);
  oled.print("Mode");
  oled.setCursor(modeValueX, 22);
  oled.print(modeStr);

  // Home actions as two buttons
  const int btnY = 49;
  const int btnW = 60;
  const int btnH = 14;
  const int gap  = 8;
  for (int i = 0; i < 2; i++) {
    int x = i * (btnW + gap);
    bool selected = (homeSel == i);
    if (selected) {
      oled.fillRect(x, btnY, btnW, btnH, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.drawRect(x, btnY, btnW, btnH, SSD1306_WHITE);
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(x + 4, btnY + 4);
    if (i == 0) {
      oled.print(radioTX ? "TX:ON" : "TX:OFF");
    } else {
      oled.print("Settings");
    }
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
  float vswr = getVSWR();
  float vFwd = getForwardVoltage();
  float vRev = getReverseVoltage();
  float m1Rot = motor1Pos / 360.0f;
  float m2Rot = motor2Pos / 360.0f;
  bool  m1Max = (motor1Pos <= MOTOR_MIN_POS) || (motor1Pos >= MOTOR_MAX_POS);
  bool  m2Max = (motor2Pos <= MOTOR_MIN_POS) || (motor2Pos >= MOTOR_MAX_POS);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Advanced Metrics");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  oled.setCursor(0, 12);
  oled.print("VSWR:");
  oled.print(vswr, 3);

  oled.setCursor(0, 22);
  oled.print("FWD:");
  oled.print(vFwd, 3);
  oled.print("V REV:");
  oled.print(vRev, 3);
  oled.print("V");

  oled.setCursor(0, 32);
  oled.print("M1:");
  oled.print(m1Rot, 2);
  oled.print("r ");
  oled.print(m1Max ? "MAX" : "OK");

  oled.setCursor(0, 42);
  oled.print("M2:");
  oled.print(m2Rot, 2);
  oled.print("r ");
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
      // Wrap selection so top and bottom connect
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
        if (opMode == MODE_AUTO) {
          menuEditingMotor = false;
          editingMotorId   = -1;
        }
        // Re-clamp in case the list shrank
        { int v2[M_COUNT]; menuSel = constrain(menuSel, 0, buildMenu(v2) - 1); }
        break;
      case M_MOTOR1:
        menuEditingMotor = true;
        editingMotorId   = M_MOTOR1;
        break;
      case M_MOTOR2:
        menuEditingMotor = true;
        editingMotorId   = M_MOTOR2;
        break;
      case M_ADV_METRICS:
        state = S_METRICS;
        break;
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
  if (pressed) {
    state = S_MENU;
  }
}

void handleMetrics(int delta, bool pressed) {
  (void)delta;
  if (pressed) state = S_MENU;
}

// ─── Arduino Entry Points ────────────────────────────────────────────────────
void setup() {
  pinMode(ENC_A,   INPUT_PULLUP);
  pinMode(ENC_B,   INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_A), readEncoder, CHANGE);

  Wire.begin();
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {}
  }

  oled.clearDisplay();
  oled.display();
}

void loop() {


  pollButton();
  int  delta   = consumeDelta();
  bool pressed = consumeButton();

  switch (state) {
    case S_HOME:
      handleHome(delta, pressed);
      drawHome();
      break;
    case S_MENU:
      handleMenu(delta, pressed);
      drawMenu();
      break;
    case S_MOTOR1:
      handleMotorAdjust(1, motor1Pos, delta, pressed);
      drawMotorAdjust(1, motor1Pos);
      break;
    case S_MOTOR2:
      handleMotorAdjust(2, motor2Pos, delta, pressed);
      drawMotorAdjust(2, motor2Pos);
      break;
    case S_METRICS:
      handleMetrics(delta, pressed);
      drawAdvancedMetrics();
      break;
  }

  delay(16);  // ~60 fps cap
}
