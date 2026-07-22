#include "stepper.h"

StepperController stepper;

StepperController::StepperController() :
    _currentPos(0),
    _targetPos(0),
    _currentSpeed(0.0f),
    _maxSpeed(MAX_SPEED),
    _accel(MAX_ACCEL),
    _startLimit(0),
    _endLimit(10000), // Default high fallback
    _homingState(STATE_IDLE),
    _lastStepTime(0),
    _stepInterval(0),
    _stepState(false) {}

void StepperController::init() {
    pinMode(STEPPER_PUL_PIN, OUTPUT);
    pinMode(STEPPER_DIR_PIN, OUTPUT);
    pinMode(STEPPER_EN_PIN, OUTPUT);

    pinMode(SENSOR_START_PIN, INPUT_PULLUP);
    pinMode(SENSOR_END_PIN, INPUT_PULLUP);

    digitalWrite(STEPPER_EN_PIN, LOW); // Enable driver (active-low usually)
    digitalWrite(STEPPER_PUL_PIN, LOW);
    digitalWrite(STEPPER_DIR_PIN, LOW);
}

void StepperController::setTargetPosition(long target) {
    if (_homingState == STATE_CALIBRATED) {
        if (target < _startLimit) target = _startLimit;
        if (target > _endLimit) target = _endLimit;
    }
    _targetPos = target;
}

void StepperController::setMaxSpeed(float speed) {
    _maxSpeed = constrain(speed, 10.0f, MAX_SPEED);
}

void StepperController::setAcceleration(float accel) {
    _accel = constrain(accel, 50.0f, MAX_ACCEL);
}

void StepperController::startHoming() {
    _homingState = STATE_HOMING_START;
    _targetPos = -999999; // Move backwards to find the physical start sensor
    _maxSpeed = 800.0f;   // Safe homing speed
}

void StepperController::updateStepInterval() {
    if (fabsf(_currentSpeed) < 1.0f) {
        _stepInterval = 0; // Prevent division by zero
    } else {
        _stepInterval = (unsigned long)(1000000.0f / fabsf(_currentSpeed));
    }
}

void StepperController::update() {
    // Check if we need to release the safety lock before motion
    if (_currentPos != _targetPos) {
        if (safetyLock.isLocked()) {
            safetyLock.unlock();
            return; // Wait for safety lock physical delay
        }
        if (!safetyLock.isReady()) {
            return; // Lock is currently disengaging
        }
    } else {
        // We reached target or are idle, secure the solenoid lock using idle timer inside safetyLock
        safetyLock.lockOnIdle();
        _currentSpeed = 0;
        return;
    }

    // Dynamic Homing Logic
    if (_homingState == STATE_HOMING_START) {
        if (digitalRead(SENSOR_START_PIN) == LOW) { // Start sensor triggered
            _currentPos = 0;
            _startLimit = 0;
            _homingState = STATE_HOMING_END;
            // Now start seeking the end limit to calibrate the range
            _targetPos = 999999;
            _maxSpeed = 800.0f;
            return;
        }
    } else if (_homingState == STATE_HOMING_END) {
        if (digitalRead(SENSOR_END_PIN) == LOW) { // End sensor triggered
            _endLimit = _currentPos;
            _targetPos = _currentPos;
            _homingState = STATE_CALIBRATED;
            _maxSpeed = MAX_SPEED; // Reset to production speed limits
            return;
        }
    }

    // Standard Trapezoidal / Linear Ramp Calculation
    unsigned long now = micros();
    unsigned long timeDelta = now - _lastStepTime;

    if (_stepInterval == 0 || timeDelta >= _stepInterval) {
        long distanceToGo = _targetPos - _currentPos;
        float dt = (timeDelta > 0 && timeDelta < 100000) ? (timeDelta / 1000000.0f) : 0.001f;

        if (distanceToGo > 0) {
            digitalWrite(STEPPER_DIR_PIN, HIGH);
            _currentSpeed += (_accel * dt);
            if (_currentSpeed > _maxSpeed) _currentSpeed = _maxSpeed;
            if (_currentSpeed < 50.0f) _currentSpeed = 50.0f; // Minimum starting speed

            // Deceleration logic
            float decelDistance = (_currentSpeed * _currentSpeed) / (2.0f * _accel);
            if (distanceToGo <= decelDistance) {
                _currentSpeed -= (_accel * dt);
                if (_currentSpeed < 50.0f) _currentSpeed = 50.0f;
            }

            _currentPos++;
        } else if (distanceToGo < 0) {
            digitalWrite(STEPPER_DIR_PIN, LOW);
            _currentSpeed -= (_accel * dt);
            if (fabsf(_currentSpeed) > _maxSpeed) _currentSpeed = -_maxSpeed;
            if (fabsf(_currentSpeed) < 50.0f) _currentSpeed = -50.0f;

            // Deceleration logic
            float decelDistance = (_currentSpeed * _currentSpeed) / (2.0f * _accel);
            if (abs(distanceToGo) <= decelDistance) {
                _currentSpeed += (_accel * dt);
                if (_currentSpeed > -50.0f) _currentSpeed = -50.0f;
            }

            _currentPos--;
        }

        // Toggle Pulse
        _stepState = !_stepState;
        digitalWrite(STEPPER_PUL_PIN, _stepState ? HIGH : LOW);

        _lastStepTime = now;
        updateStepInterval();
    }
}
