#pragma once

// Shared UI / matcher state. Motor angles exist twice on purpose:
//   motor1Pos, motor2Pos — integer degrees for OLED and encoder menus (0..180).
//   motor1_pos, motor2_pos — float radians inside matching.cpp for stepping/limits.
// dM1, dM2 — estimated loss gradient w.r.t. each motor (finite differences in matching_tick).

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

extern long  motor1Pos;
extern long  motor2Pos;
extern float motor1_pos;
extern float motor2_pos;
extern float dM1;
extern float dM2;

extern bool  atMatch;
extern float lastVSWR;
extern float lastFwdV;
extern float lastRevV;

extern bool csvStreamEnabled;
