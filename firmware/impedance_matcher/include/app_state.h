#pragma once

// Shared UI / matcher state. Motor angles: *Deg (integer degrees) for OLED/encoder,
// *Rad (float radians) for stepping/limits. lossGrad1/2: loss gradient per motor

enum AppState { S_HOME, S_MENU, S_MOTOR1, S_MOTOR2, S_METRICS };
enum OpMode   { MODE_AUTO, MODE_MANUAL };

enum MenuID {
  M_MODE        = 0,
  M_MOTOR1      = 1,
  M_MOTOR2      = 2,
  M_STREAM_CSV  = 3,
  M_ADV_METRICS = 4,
  M_BACK        = 5,
  M_COUNT       = 6
};

extern AppState state;
extern OpMode   opMode;
extern bool     radioTX;

extern long  motor1Deg;
extern long  motor2Deg;
extern float motor1Rad;
extern float motor2Rad;
extern float lossGrad1;
extern float lossGrad2;

extern bool  atMatch;
extern float lastVSWR;
extern float lastFwdV;
extern float lastRevV;

extern bool csvStreamEnabled;
