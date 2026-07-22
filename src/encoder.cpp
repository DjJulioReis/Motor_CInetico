#include "encoder.h"

EncoderController encoder;

EncoderController::EncoderController() :
    _lastClkState(HIGH),
    _menuPosition(0),
    _buttonPressed(false),
    _lastDebounceTime(0) {}

void EncoderController::init() {
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);

    _lastClkState = digitalRead(ENCODER_CLK);
}

void EncoderController::update() {
    int currentClkState = digitalRead(ENCODER_CLK);
    if (currentClkState != _lastClkState && currentClkState == LOW) {
        if (digitalRead(ENCODER_DT) != currentClkState) {
            _menuPosition++;
        } else {
            _menuPosition--;
        }
        if (_menuPosition < 0) _menuPosition = 0;
    }
    _lastClkState = currentClkState;

    // Handle button debounce
    int swState = digitalRead(ENCODER_SW);
    if (swState == LOW) {
        if (millis() - _lastDebounceTime > 200) {
            _buttonPressed = true;
            _lastDebounceTime = millis();
        }
    }
}

bool EncoderController::isButtonPressed() {
    if (_buttonPressed) {
        _buttonPressed = false;
        return true;
    }
    return false;
}
