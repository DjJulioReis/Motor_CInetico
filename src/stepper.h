#ifndef STEPPER_H
#define STEPPER_H

#include "config.h"
#include "safety_lock.h"

enum HomingState {
    STATE_IDLE,
    STATE_HOMING_START, // Moving backward to find start sensor
    STATE_HOMING_END,   // Moving forward to find end sensor
    STATE_CALIBRATED
};

class StepperController {
private:
    long _currentPos;
    long _targetPos;
    float _currentSpeed;
    float _maxSpeed;
    float _accel;

    long _startLimit;
    long _endLimit;
    HomingState _homingState;

    unsigned long _lastStepTime;
    unsigned long _stepInterval; // Microseconds
    bool _stepState;

    void updateStepInterval();

public:
    StepperController();
    void init();
    void setTargetPosition(long target);
    void setMaxSpeed(float speed);
    void setAcceleration(float accel);

    long getCurrentPosition() const { return _currentPos; }
    long getTargetPosition() const { return _targetPos; }
    long getStartLimit() const { return _startLimit; }
    long getEndLimit() const { return _endLimit; }
    bool isCalibrated() const { return _homingState == STATE_CALIBRATED; }
    bool isHoming() const { return _homingState == STATE_HOMING_START || _homingState == STATE_HOMING_END; }
    bool isMoving() const { return _currentPos != _targetPos; }

    void setStartLimit(long pos) { _startLimit = pos; }
    void setEndLimit(long pos) { _endLimit = pos; _homingState = STATE_CALIBRATED; }

    void startHoming();
    void update();
};

extern StepperController stepper;

#endif // STEPPER_H
