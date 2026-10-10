#pragma once

#include <Arduino.h>

void matching_init_motor_pins();
bool matching_init_motors();
void matching_homing();
void matching_tick();

float getVSWR();
float getForwardVoltage();
float getReverseVoltage();
void  setMotor1Step(long posDeg);
void  setMotor2Step(long posDeg);
void  setRadioTX(bool en);
