/*
 * headless_matcher.ino
 * Headless Matcher
 *
 * Dependencies (Arduino Library Manager):
 *   TMCStepper
 */

#include <Arduino.h>
#include <TMCStepper.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../shared/matcher_core.h"

TMC2209Stepper driver1(&MATCHING_SERIAL_PORT, R_SENSE, DRV_ADDRESS_1);
TMC2209Stepper driver2(&MATCHING_SERIAL_PORT, R_SENSE, DRV_ADDRESS_2);

#define SCHED_LOOP_MS 16u

enum OpMode { MODE_AUTO, MODE_MANUAL };
OpMode opMode = MODE_AUTO;
bool radioTX = false;
bool atMatch = false;

float motor1Rad = 0.0f;
float motor2Rad = 0.0f;
float lossGrad1 = 0.1f;
float lossGrad2 = 0.1f;

long motor1Deg = 0;
long motor2Deg = 0;

float lastVSWR = 1.0f;
float lastFwdV = 0.0f;
float lastRevV = 0.0f;
static uint32_t lastStatusMs = 0;
bool csvStreamEnabled = false;

static void printHelp();
static void printStatus();
static void processSerialCommand();
static void handleCommand(char *line);
static void setRadioTX(bool enabled);
static void syncMotorDeg();
static bool parseLongArg(const char *arg, long *out);
static void setMotor1Step(long posDeg);
static void setMotor2Step(long posDeg);

void setup() {
  analogReadResolution(12);

  Serial.begin(CONTROL_BAUD);
  while (!Serial && millis() < 3000) {
  }

  pinMode(STEP_PIN_1, OUTPUT);
  pinMode(DIR_PIN_1, OUTPUT);
  pinMode(STEP_PIN_2, OUTPUT);
  pinMode(DIR_PIN_2, OUTPUT);
  pinMode(TRANSMIT_PIN, OUTPUT);
  digitalWrite(TRANSMIT_PIN, LOW);  // HIGH = TX on

  MATCHING_SERIAL_PORT.begin(MATCHING_UART_BAUD, SERIAL_8N1);
  delay(500);

  Serial.println("Starting Homing Sequence...");
  runHomingSequence();
  Serial.println("Finished Homing Sequence...");

  syncMotorDeg();
  printHelp();
  printStatus();
}

void loop() {
  processSerialCommand();

  if (opMode == MODE_AUTO) {
    autoMatchStep(lossGrad1, lossGrad2);
  } else {
    // Keep telemetry fresh in manual mode too
    measureLoss(FWD_PIN, REV_PIN);
  }

  syncMotorDeg();

  const bool statusPeriodically = !csvStreamEnabled;
  if (statusPeriodically && millis() - lastStatusMs >= 1000u) {
    lastStatusMs = millis();
    printStatus();
  }

  delay(SCHED_LOOP_MS);
}

static void processSerialCommand() {
  static char line[64];
  static size_t len = 0;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (len > 0) {
        line[len] = '\0';
        handleCommand(line);
        len = 0;
      }
      continue;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = c;
    }
  }
}

static void handleCommand(char *line) {
  char *cmd = strtok(line, " \t");
  if (!cmd) return;

  if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
    return;
  }
  if (strcasecmp(cmd, "mode") == 0) {
    char *arg = strtok(nullptr, " \t");
    if (!arg) {
      Serial.println("ERR mode needs: auto|manual");
      return;
    }
    if (strcasecmp(arg, "auto") == 0) {
      opMode = MODE_AUTO;
      Serial.println("OK mode=auto");
      return;
    }
    if (strcasecmp(arg, "manual") == 0) {
      opMode = MODE_MANUAL;
      Serial.println("OK mode=manual");
      return;
    }
    Serial.println("ERR mode must be auto|manual");
    return;
  }
  if (strcasecmp(cmd, "tx") == 0) {
    char *arg = strtok(nullptr, " \t");
    if (!arg) {
      Serial.println("ERR tx needs: on|off");
      return;
    }
    if (strcasecmp(arg, "on") == 0) {
      setRadioTX(true);
      Serial.println("OK tx=on");
      return;
    }
    if (strcasecmp(arg, "off") == 0) {
      setRadioTX(false);
      Serial.println("OK tx=off");
      return;
    }
    Serial.println("ERR tx must be on|off");
    return;
  }
  if (strcasecmp(cmd, "m1") == 0 || strcasecmp(cmd, "m2") == 0) {
    if (opMode != MODE_MANUAL) {
      Serial.println("ERR manual mode required for motor set");
      return;
    }
    char *arg = strtok(nullptr, " \t");
    long deg = 0;
    if (!parseLongArg(arg, &deg)) {
      Serial.println("ERR motor command needs angle 0..180");
      return;
    }
    deg = constrain(deg, (long)MOTOR_MIN_DEG, (long)MOTOR_MAX_DEG);
    if (strcasecmp(cmd, "m1") == 0) setMotor1Step(deg);
    else setMotor2Step(deg);
    syncMotorDeg();
    Serial.println("OK motor updated");
    return;
  }
  if (strcasecmp(cmd, "home") == 0) {
    Serial.println("Re-homing...");
    runHomingSequence();
    syncMotorDeg();
    Serial.println("OK homed");
    return;
  }
  if (strcasecmp(cmd, "stream") == 0) {
    char *arg = strtok(nullptr, " \t");
    if (!arg) {
      Serial.println("ERR stream needs: on|off");
      return;
    }
    if (strcasecmp(arg, "on") == 0) {
      csvStreamEnabled = true;
      Serial.println("OK stream=on");
      return;
    }
    if (strcasecmp(arg, "off") == 0) {
      csvStreamEnabled = false;
      Serial.println("OK stream=off");
      return;
    }
    Serial.println("ERR stream must be on|off");
    return;
  }
  Serial.println("ERR unknown command. Type: help");
}

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  help              - show commands");
  Serial.println("  mode auto|manual  - set operation mode");
  Serial.println("  tx on|off         - radio transmit switch");
  Serial.println("  m1 <deg>          - set motor 1 (manual only)");
  Serial.println("  m2 <deg>          - set motor 2 (manual only)");
  Serial.println("  home              - run homing sequence");
  Serial.println("  stream on|off     - VSWR_CSV lines for live.py / plot logging");
  Serial.println();
}

static void printStatus() {
  Serial.print("STATE mode=");
  Serial.print(opMode == MODE_AUTO ? "AUTO" : "MANUAL");
  Serial.print(" tx=");
  Serial.print(radioTX ? "ON" : "OFF");
  Serial.print(" atMatch=");
  Serial.print(atMatch ? "1" : "0");
  Serial.print(" m1_deg=");
  Serial.print(motor1Deg);
  Serial.print(" m2_deg=");
  Serial.print(motor2Deg);
  Serial.print(" vswr=");
  Serial.print(lastVSWR, 3);
  Serial.print(" fwdV=");
  Serial.print(lastFwdV, 3);
  Serial.print(" revV=");
  Serial.println(lastRevV, 3);
}

static void syncMotorDeg() {
  motor1Deg = clampDeg(motor1Rad);
  motor2Deg = clampDeg(motor2Rad);
}

static void setRadioTX(bool enabled) {
  radioTX = enabled;
  digitalWrite(TRANSMIT_PIN, enabled ? HIGH : LOW);
}

static bool parseLongArg(const char *arg, long *out) {
  if (!arg || !out) return false;
  char *end = nullptr;
  long parsed = strtol(arg, &end, 10);
  if (!end || *end != '\0') return false;
  *out = parsed;
  return true;
}

static void setMotor1Step(long posDeg) {
  setMotorStep(STEP_PIN_1, DIR_PIN_1, posDeg, motor1Rad);
}

static void setMotor2Step(long posDeg) {
  setMotorStep(STEP_PIN_2, DIR_PIN_2, posDeg, motor2Rad);
}