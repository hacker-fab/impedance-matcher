#pragma once

#include <Arduino.h>
#include <TMCStepper.h>
#include "../impedance_matcher/include/config.h"

#define R_SENSE       0.11f
#define DRV_ADDRESS_1 0b00
#define DRV_ADDRESS_2 0b01

#define STEP_DELAY          200

// Mechanical: rad -> full steps x microsteps (MAX_ROT is half a turn of travel)
#define STEPS_PER_RAD       63.66197f
#define MICROSTEPS_PER_STEP 32
#define MAX_ROT             3.14159f
#define GRAD_SCALE          0.025f
#define MAX_STEPSIZE        (PI / 36.0f)
#define MIN_STEPSIZE        (PI / 700.0f)

#define VSWR_SAMPLES 300

// State defined in each sketch's main translation unit
extern float motor1Rad;
extern float motor2Rad;
extern bool  atMatch;
extern float lastVSWR;
extern float lastFwdV;
extern float lastRevV;
extern bool  csvStreamEnabled;

inline float analogReadMilliVolts(int pin) {
  return (analogRead(pin) / 4095.0f) * 3300.0f;
}

inline int roundUp(float value) {
  int truncated = (int)value;
  return (value > truncated) ? truncated + 1 : truncated;
}

inline float stepsToRad(int steps) {
  return steps / (STEPS_PER_RAD * MICROSTEPS_PER_STEP);
}

inline int radToSteps(float rads) {
  return roundUp(rads * (STEPS_PER_RAD * MICROSTEPS_PER_STEP));
}

inline void takeStep(int stepPin) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(STEP_DELAY / 2);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(STEP_DELAY / 2);
}

inline float turnByRad(TMC2209Stepper &driver, int stepPin, int dirPin,
                       float rads, float &motorRad, bool ignoreLimits = false) {
  (void)driver;
  if (rads == 0.0f) return 0.0f;

  bool  isNeg         = (rads < 0.0f);
  int   totalSteps    = radToSteps(isNeg ? -rads : rads);
  float stepIncrement = stepsToRad(1);
  float posChange     = isNeg ? -stepIncrement : stepIncrement;
  int   stepsTaken    = 0;

  digitalWrite(dirPin, isNeg);

  for (int i = 0; i < totalSteps; i++) {
    if (!ignoreLimits) {
      if ((motorRad >= MAX_ROT && posChange > 0.0f) ||
          (motorRad <= 0.0f   && posChange < 0.0f)) {
        Serial.println("MOTOR LIMIT REACHED");
        break;
      }
    }
    takeStep(stepPin);
    motorRad += posChange;
    stepsTaken++;
  }
  return stepsTaken * posChange;
}

inline float measureLoss(int fwd, int rev) {
  float sumFwd = 0.0f, sumRev = 0.0f;
  for (int i = 0; i < VSWR_SAMPLES; i++) {
    sumFwd += analogReadMilliVolts(fwd);
    sumRev += analogReadMilliVolts(rev);
    delayMicroseconds(15);
  }
  float avgFwd = sumFwd / VSWR_SAMPLES;
  float avgRev = sumRev / VSWR_SAMPLES;

  lastFwdV = avgFwd / 1000.0f;
  lastRevV = avgRev / 1000.0f;

  float denom = (avgFwd - avgRev);
  float vswr  = (denom > 0.0f) ? (avgFwd + avgRev) / denom : 99.0f;
  lastVSWR    = vswr;

  // Cost minimized by coord descent
  // Squared distance from ideal VSWR=1
  float loss = (vswr - 1.0f) * (vswr - 1.0f);

  if (vswr > VSWR_MATCH_EXIT)  atMatch = false;
  if (vswr < VSWR_MATCH_ENTER) atMatch = true;

  if (csvStreamEnabled) {
    Serial.print("VSWR_CSV,");
    Serial.print(millis());
    Serial.print(",");  Serial.print(vswr, 6);
    Serial.print(",");  Serial.print(lastFwdV, 6);
    Serial.print(",");  Serial.print(lastRevV, 6);
    Serial.print(",");  Serial.print(motor1Rad, 6);
    Serial.print(",");  Serial.print(motor2Rad, 6);
    Serial.print(",");  Serial.println(atMatch ? 1 : 0);
  }

  return loss;
}

inline float clampMagnitude(float value, float minMag, float maxMag) {
  float mag  = (value < 0.0f) ? -value : value;
  float sign = (value < 0.0f) ? -1.0f  : 1.0f;
  if (mag > maxMag) return maxMag * sign;
  if (mag < minMag) return minMag * sign;
  return value;
}

inline void calcGradAndStep(TMC2209Stepper &driver, int stepPin, int dirPin,
                            float &gradient, float &motorRad) {
  float initialCost   = measureLoss(FWD_PIN, REV_PIN);
  float commandedStep = clampMagnitude(gradient * GRAD_SCALE, MIN_STEPSIZE, MAX_STEPSIZE);

  if (motorRad >= MAX_ROT - MIN_STEPSIZE) commandedStep = -MIN_STEPSIZE;
  if (motorRad <= MIN_STEPSIZE)           commandedStep =  MIN_STEPSIZE;

  float actualTravel = turnByRad(driver, stepPin, dirPin, commandedStep, motorRad);
  delay(5);
  if (actualTravel != 0.0f)
    gradient = (initialCost - measureLoss(FWD_PIN, REV_PIN)) / actualTravel;
}

inline float radToDeg(float rad) {
  return rad * (180.0f / PI);
}

inline long clampDeg(float rad) {
  return constrain((long)radToDeg(rad), (long)MOTOR_MIN_DEG, (long)MOTOR_MAX_DEG);
}

// Back off to the hard stop, zero the reference, then re-center
inline void runHomingSequence(TMC2209Stepper &driver1, TMC2209Stepper &driver2) {
  turnByRad(driver1, STEP_PIN_1, DIR_PIN_1, HOMING_BACKOFF_RAD, motor1Rad, true);
  turnByRad(driver2, STEP_PIN_2, DIR_PIN_2, HOMING_BACKOFF_RAD, motor2Rad, true);
  motor1Rad = 0.0f;
  motor2Rad = 0.0f;
  turnByRad(driver1, STEP_PIN_1, DIR_PIN_1, HOMING_CENTER_RAD, motor1Rad, true);
  turnByRad(driver2, STEP_PIN_2, DIR_PIN_2, HOMING_CENTER_RAD, motor2Rad, true);
}

// One coordinate-descent tick: step both axes toward match, or just sample once matched
inline void autoMatchStep(TMC2209Stepper &driver1, TMC2209Stepper &driver2,
                          float &gradient1, float &gradient2) {
  if (!atMatch) {
    calcGradAndStep(driver1, STEP_PIN_1, DIR_PIN_1, gradient1, motor1Rad);
    calcGradAndStep(driver2, STEP_PIN_2, DIR_PIN_2, gradient2, motor2Rad);
  } else {
    measureLoss(FWD_PIN, REV_PIN);
  }
}

// Absolute move: drive one axis to posDeg degrees from its current position
inline void setMotorStep(TMC2209Stepper &driver, int stepPin, int dirPin,
                         long posDeg, float &motorRad) {
  float targetRad = posDeg * (PI / 180.0f);
  float delta     = targetRad - motorRad;
  turnByRad(driver, stepPin, dirPin, delta, motorRad);
}
