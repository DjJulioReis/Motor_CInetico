#include "effects.h"
#include "stepper.h"

EffectsEngine effects;

EffectsEngine::EffectsEngine() :
    _pointA(0),
    _pointB(10000),
    _hasPoints(false),
    _groupId(0),
    _isEffectRunning(false),
    _currentEffectId(0),
    _effectStartTime(0),
    _lastUpdate(0),
    _sequenceStep(0) {}

void EffectsEngine::init() {
    // Storing points securely
    _pointA = 0;
    _pointB = 10000;
    _hasPoints = true;
}

void EffectsEngine::setPointA(long pos) {
    _pointA = pos;
    _hasPoints = true;
}

void EffectsEngine::setPointB(long pos) {
    _pointB = pos;
    _hasPoints = true;
}

void EffectsEngine::startEffect(uint8_t effectId) {
    if (!_hasPoints) return;
    _isEffectRunning = true;
    _currentEffectId = effectId;
    _effectStartTime = millis();
    _sequenceStep = 0;
}

void EffectsEngine::stopEffect() {
    _isEffectRunning = false;
}

void EffectsEngine::update() {
    if (!_isEffectRunning || !_hasPoints) return;

    unsigned long elapsed = millis() - _effectStartTime;

    switch (_currentEffectId) {
        case 1: // Continuous Loop (A -> B -> A)
            if (!stepper.isMoving()) {
                if (_sequenceStep == 0) {
                    stepper.setTargetPosition(_pointB);
                    _sequenceStep = 1;
                } else {
                    stepper.setTargetPosition(_pointA);
                    _sequenceStep = 0;
                }
            }
            break;

        case 2: // Sine Wave/Smooth Oscillation
            {
                float period = 3000.0f; // 3 seconds full oscillation
                float angle = (2.0f * PI * (float)(elapsed % (unsigned long)period)) / period;
                float normSine = (sinf(angle) + 1.0f) / 2.0f; // 0.0 to 1.0
                long target = _pointA + (long)(normSine * (float)(_pointB - _pointA));
                stepper.setTargetPosition(target);
            }
            break;

        case 3: // Coordinated Group Wave Delay (Sequential movement offsets)
            {
                // Each node applies a phase delay based on its configured Group ID
                float period = 4000.0f;
                float phaseDelay = (float)_groupId * 0.5f; // half second delay per group offset
                float angle = (2.0f * PI * ((float)elapsed / period)) + phaseDelay;
                float normSine = (sinf(angle) + 1.0f) / 2.0f;
                long target = _pointA + (long)(normSine * (float)(_pointB - _pointA));
                stepper.setTargetPosition(target);
            }
            break;

        default:
            stopEffect();
            break;
    }
}
