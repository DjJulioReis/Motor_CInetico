#ifndef SAFETY_LOCK_H
#define SAFETY_LOCK_H

#include "config.h"

class SafetyLock {
private:
    bool _isLocked;
    unsigned long _unlockTime;
    unsigned long _lastActiveTime;
    const unsigned long UNLOCK_DELAY_MS = 150;     // Delay to allow solenoid to physically disengage
    const unsigned long IDLE_TIMEOUT_MS = 2000;    // Engage lock only after 2.0s of continuous idle state

public:
    SafetyLock() : _isLocked(true), _unlockTime(0), _lastActiveTime(0) {}

    void init() {
        pinMode(SAFETY_LOCK_PIN, OUTPUT);
        lock();
    }

    void lock() {
        digitalWrite(SAFETY_LOCK_PIN, LOW); // Solenoid engaged/locked (fail-safe layout)
        _isLocked = true;
    }

    void unlock() {
        if (_isLocked) {
            digitalWrite(SAFETY_LOCK_PIN, HIGH); // Actuate solenoid to retract lock pin
            _unlockTime = millis();
            _isLocked = false;
        }
        _lastActiveTime = millis();
    }

    void lockOnIdle() {
        if (!_isLocked) {
            if (millis() - _lastActiveTime >= IDLE_TIMEOUT_MS) {
                lock();
            }
        }
    }

    bool isReady() {
        if (_isLocked) return false;
        return (millis() - _unlockTime) >= UNLOCK_DELAY_MS;
    }

    bool isLocked() const {
        return _isLocked;
    }

    void resetIdleTimer() {
        _lastActiveTime = millis();
    }
};

extern SafetyLock safetyLock;

#endif // SAFETY_LOCK_H
