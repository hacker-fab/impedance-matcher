#!/usr/bin/env python3
"""Run the actual shared control code against a small host-side hardware fake."""

from pathlib import Path
import shutil
import subprocess
import tempfile


ARDUINO = r"""
#pragma once
#include <cstdint>
#define PI 3.14159265358979323846
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define SERIAL_8N1 0
extern int adcValues[64];
extern int stepPulses;
extern int pinModes[64];
extern int digitalValues[64];
extern int stepPulsesByPin[64];
inline void pinMode(int pin, int mode) { pinModes[pin] = mode; }
struct FakeSerial {
  int starts = 0;
  unsigned long baud = 0;
  void begin(unsigned long b, int = 0) { ++starts; baud = b; }
  template<class T> void print(T, int = 0) {}
  template<class T> void println(T, int = 0) {}
};
extern FakeSerial Serial, Serial2, Serial7;
inline int analogRead(int pin) { return adcValues[pin]; }
inline void digitalWrite(int pin, int value) {
  digitalValues[pin] = value;
  if ((pin == 2 || pin == 9) && value == HIGH) {
    ++stepPulses;
    ++stepPulsesByPin[pin];
  }
}
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
inline unsigned long millis() { return 0; }
template<class T> T constrain(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}
"""

DRIVER = r"""
#pragma once
#include <cstdint>
struct TMC2209Stepper {
  uint8_t chipVersion = 0x21;
  bool strap1 = false, strap2 = true;
  bool acceptsWrite = true, countsWrite = true;
  uint8_t writes = 0;
  int reads = 0;
  uint32_t config = 0x181;
  TMC2209Stepper() = default;
  TMC2209Stepper(FakeSerial*, float, uint8_t) {}
  uint8_t version() { ++reads; return chipVersion; }
  bool ms1() { ++reads; return strap1; }
  bool ms2() { ++reads; return strap2; }
  uint32_t GCONF() { ++reads; return config; }
  void GCONF(uint32_t value) {
    if (acceptsWrite) config = value;
    if (countsWrite) ++writes;
  }
  uint8_t IFCNT() { ++reads; return writes; }
};
"""

TEST = r"""
#include <cassert>
#include <cmath>
#include <iostream>
#include "matcher_core.h"
#include "app_state.h"
#include "matching.h"

int adcValues[64] = {};
int stepPulses = 0;
int pinModes[64];
int digitalValues[64];
int stepPulsesByPin[64];
FakeSerial Serial, Serial2, Serial7;
AppState state = S_HOME;
OpMode opMode = MODE_AUTO;
bool radioTX = false;
long motor1Deg = 0, motor2Deg = 0;
float motor1Rad = 0, motor2Rad = 0;
float lossGrad1 = 0.1f, lossGrad2 = 0.1f;
bool atMatch = false;
float lastVSWR = 0, lastFwdV = 0, lastRevV = 0;
bool csvStreamEnabled = false;

int main() {
  for (int i = 0; i < 64; ++i) {
    pinModes[i] = -1;
    digitalValues[i] = -1;
  }

  // Startup states must agree with the board: the fan inverter is active-low,
  // and transmitter control must begin disabled.
  matching_init_motor_pins();
  for (int pin : {FAN_PIN, STEP_PIN_1, DIR_PIN_1, STEP_PIN_2, DIR_PIN_2, TRANSMIT_PIN})
    assert(pinModes[pin] == OUTPUT);
  assert(digitalValues[FAN_PIN] == LOW);
  assert(digitalValues[TRANSMIT_PIN] == LOW);
  setRadioTX(true);
  assert(digitalValues[TRANSMIT_PIN] == HIGH);
  setRadioTX(false);
  assert(digitalValues[TRANSMIT_PIN] == LOW);

  // The PCB routes the forward detector to pad 24 and reverse to pad 25.
  adcValues[24] = 3000;
  adcValues[25] = 1000;
  const float loss = measureLoss(FWD_PIN, REV_PIN);
  assert(std::fabs(lastVSWR - 2.0f) < 0.001f);
  assert(std::fabs(loss - 1.0f) < 0.001f);
  assert(lastFwdV > lastRevV);

  // Known measurement states: perfect match, hysteresis band, and invalid ratio.
  atMatch = false;
  adcValues[24] = 3000;
  adcValues[25] = 0;
  measureLoss(FWD_PIN, REV_PIN);
  assert(std::fabs(lastVSWR - 1.0f) < 0.001f && atMatch);
  adcValues[25] = 390; // VSWR about 1.30: inside the 1.2--1.4 no-change band.
  measureLoss(FWD_PIN, REV_PIN);
  assert(lastVSWR > VSWR_MATCH_ENTER && lastVSWR < VSWR_MATCH_EXIT && atMatch);
  adcValues[25] = 900; // VSWR about 1.86: leave matched state.
  measureLoss(FWD_PIN, REV_PIN);
  assert(lastVSWR > VSWR_MATCH_EXIT && !atMatch);
  adcValues[25] = 390;
  measureLoss(FWD_PIN, REV_PIN);
  assert(!atMatch);
  adcValues[25] = 3000;
  measureLoss(FWD_PIN, REV_PIN);
  assert(lastVSWR == 99.0f && !atMatch);

  // A 400-full-step motor at the PCB's 1/64 setting needs 6400 pulses/90 deg.
  stepPulses = stepPulsesByPin[STEP_PIN_1] = stepPulsesByPin[STEP_PIN_2] = 0;
  turnByRad(STEP_PIN_1, DIR_PIN_1, PI / 2, motor1Rad);
  assert(stepPulses >= 6400 && stepPulses <= 6401);
  assert(std::fabs(motor1Rad - PI / 2) < 0.002f);
  assert(digitalValues[DIR_PIN_1] == LOW);
  turnByRad(STEP_PIN_1, DIR_PIN_1, -PI / 2, motor1Rad);
  assert(std::fabs(motor1Rad) < 0.002f && digitalValues[DIR_PIN_1] == HIGH);
  const int motor1Pulses = stepPulsesByPin[STEP_PIN_1];
  turnByRad(STEP_PIN_2, DIR_PIN_2, PI / 2, motor2Rad);
  assert(stepPulsesByPin[STEP_PIN_2] >= 6400 && stepPulsesByPin[STEP_PIN_1] == motor1Pulses);
  motor1Rad = 0.0f;
  assert(turnByRad(STEP_PIN_1, DIR_PIN_1, -0.1f, motor1Rad) == 0.0f);
  motor1Rad = MAX_ROT;
  assert(turnByRad(STEP_PIN_1, DIR_PIN_1, 0.1f, motor1Rad) == 0.0f);

  // Exercise the application-level motor wrappers and homing state update.
  motor1Rad = motor2Rad = 0.0f;
  stepPulsesByPin[STEP_PIN_1] = stepPulsesByPin[STEP_PIN_2] = 0;
  setMotor1Step(90);
  assert(std::fabs(motor1Rad - PI / 2) < 0.002f);
  assert(std::fabs(motor2Rad) < 0.002f);
  assert(stepPulsesByPin[STEP_PIN_1] >= 6400 && stepPulsesByPin[STEP_PIN_2] == 0);
  setMotor2Step(90);
  assert(std::fabs(motor2Rad - PI / 2) < 0.002f);
  assert(stepPulsesByPin[STEP_PIN_2] >= 6400);
  matching_homing();
  assert(std::fabs(motor1Rad - PI / 2) < 0.002f);
  assert(std::fabs(motor2Rad - PI / 2) < 0.002f);
  assert(motor1Deg == 90 && motor2Deg == 90);

  // Auto mode samples only while matched; otherwise it commands both axes.
  opMode = MODE_AUTO;
  atMatch = true;
  const int heldMotor1 = stepPulsesByPin[STEP_PIN_1];
  const int heldMotor2 = stepPulsesByPin[STEP_PIN_2];
  matching_tick();
  assert(stepPulsesByPin[STEP_PIN_1] == heldMotor1 && stepPulsesByPin[STEP_PIN_2] == heldMotor2);
  atMatch = false;
  motor1Rad = motor2Rad = 0.0f;
  lossGrad1 = lossGrad2 = 0.1f;
  matching_tick();
  assert(stepPulsesByPin[STEP_PIN_1] > heldMotor1 && stepPulsesByPin[STEP_PIN_2] > heldMotor2);

  TMC2209Stepper driver1, driver2;
#if MATCHING_USE_DRIVER_UART
  driver1.writes = 255; // IFCNT must work across its 8-bit wraparound.
  assert(initMotorDrivers(driver1, driver2));
  assert(Serial2.starts == 1 && Serial7.starts == 1);
  assert(Serial2.baud == 500000 && Serial7.baud == 500000);
  assert(driver1.writes == 0 && driver2.writes == 1);
  assert(driver1.config == 0x141); // Preserve VREF/current bits; use MS straps.

  TMC2209Stepper missing;
  missing.chipVersion = 0;
  assert(!initMotorDriver(missing) && missing.writes == 0);
  TMC2209Stepper wrongMS1;
  wrongMS1.strap1 = true;
  assert(!initMotorDriver(wrongMS1) && wrongMS1.writes == 0);
  TMC2209Stepper wrongMS2;
  wrongMS2.strap2 = false;
  assert(!initMotorDriver(wrongMS2) && wrongMS2.writes == 0);
  TMC2209Stepper noAcknowledgment;
  noAcknowledgment.countsWrite = false;
  assert(!initMotorDriver(noAcknowledgment));
  TMC2209Stepper badReadback;
  badReadback.acceptsWrite = false;
  assert(!initMotorDriver(badReadback));
  std::cout << "PASS: startup states, sensor identity/VSWR/hysteresis, motor wrappers/homing/auto behavior, separate UARTs, "
               "register preservation/counter wrap, missing chip, MS1/MS2, "
               "write acknowledgment and register readback\n";
#else
  // Working standalone hardware must not require UART responses or change VREF.
  driver1.chipVersion = driver2.chipVersion = 0;
  driver1.acceptsWrite = driver2.acceptsWrite = false;
  assert(initMotorDrivers(driver1, driver2));
  assert(Serial2.starts == 0 && Serial7.starts == 0);
  assert(driver1.reads == 0 && driver2.reads == 0);
  assert(driver1.writes == 0 && driver2.writes == 0);
  assert(driver1.config == 0x181 && driver2.config == 0x181);
  for (int pin : {7, 8, 28, 29}) assert(pinModes[pin] == INPUT);
  std::cout << "PASS: startup states, sensor identity/VSWR/hysteresis, motor wrappers/homing/auto behavior, standalone startup "
               "without UART initialization, reads or writes\n";
#endif
}
"""


def main():
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A host C++ compiler (clang++ or g++) is required.")
    core = Path(__file__).resolve().parents[1] / "shared"
    with tempfile.TemporaryDirectory(prefix="matcher-core-tests-") as name:
        temp = Path(name)
        (temp / "Arduino.h").write_text(ARDUINO)
        (temp / "TMCStepper.h").write_text(DRIVER)
        (temp / "test.cpp").write_text(TEST)
        for mode in (None, 1):
            # No override exercises the shipped default; 1 exercises opt-in UART.
            flags = [] if mode is None else [f"-DMATCHING_USE_DRIVER_UART={mode}"]
            subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", *flags, f"-I{temp}",
                 f"-I{core}", f"-I{core.parent / 'impedance_matcher' / 'include'}",
                 str(temp / "test.cpp"),
                 str(core.parent / "impedance_matcher" / "src" / "matching.cpp"),
                 "-o", str(temp / "test")],
                check=True,
            )
            subprocess.run([str(temp / "test")], check=True)


if __name__ == "__main__":
    main()
