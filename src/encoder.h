#ifndef ENCODER_H
#define ENCODER_H

#include "config.h"

class EncoderController {
private:
    int _lastClkState;
    int _menuPosition;
    bool _buttonPressed;
    unsigned long _lastDebounceTime;

public:
    EncoderController();
    void init();
    void update();
    int getMenuPosition() const { return _menuPosition; }
    void setMenuPosition(int pos) { _menuPosition = pos; }
    bool isButtonPressed();
};

extern EncoderController encoder;

#endif // ENCODER_H
