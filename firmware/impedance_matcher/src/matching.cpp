#include "../include/matching.h"
#include "../include/config.h"
#include "../include/app_state.h"
#include "../../shared/matcher_core.h"

TMC2209Stepper driver1(&MATCHING_SERIAL_PORT, R_SENSE, DRV_ADDRESS_1);
TMC2209Stepper driver2(&MATCHING_SERIAL_PORT, R_SENSE, DRV_ADDRESS_2);

void matching_init_motor_pins() {
  pinMode(STEP_PIN_1,   OUTPUT);
  pinMode(DIR_PIN_1,    OUTPUT);
  pinMode(STEP_PIN_2,   OUTPUT);
  pinMode(DIR_PIN_2,    OUTPUT);
  pinMode(TRANSMIT_PIN, OUTPUT);
  digitalWrite(TRANSMIT_PIN, LOW);
}

void matching_init_uart() {
  MATCHING_SERIAL_PORT.begin(MATCHING_UART_BAUD, SERIAL_8N1);
  delay(500);
}

void matching_homing() {
  Serial.println("Starting Homing Sequence...");
  runHomingSequence();
  Serial.println("Finished Homing Sequence...");

  motor1Deg = clampDeg(motor1Rad);
  motor2Deg = clampDeg(motor2Rad);
}

void matching_tick() {
  if (opMode != MODE_AUTO) return;
  autoMatchStep(lossGrad1, lossGrad2);
  motor1Deg = clampDeg(motor1Rad);
  motor2Deg = clampDeg(motor2Rad);
}

float getVSWR()           { return lastVSWR; }
float getForwardVoltage() { return lastFwdV; }
float getReverseVoltage() { return lastRevV; }

void setRadioTX(bool en) {
  radioTX = en;
  digitalWrite(TRANSMIT_PIN, en ? HIGH : LOW);
}

void setMotor1Step(long posDeg) {
  setMotorStep(STEP_PIN_1, DIR_PIN_1, posDeg, motor1Rad);
}

void setMotor2Step(long posDeg) {
  setMotorStep(STEP_PIN_2, DIR_PIN_2, posDeg, motor2Rad);
}
