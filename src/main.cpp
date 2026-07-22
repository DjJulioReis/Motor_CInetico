#include <Arduino.h>
#include "config.h"
#include "stepper.h"
#include "safety_lock.h"
#include "dmx_rdm.h"
#include "mileto_app.h"
#include "display.h"
#include "encoder.h"
#include "effects.h"

// Local Menu Configuration
const int MENU_TOTAL = 5;
const char* menuOptions[MENU_TOTAL] = {
    "DMX Address",
    "Calibrate Limit",
    "Set Point A",
    "Set Point B",
    "Group ID"
};

int menuValues[MENU_TOTAL] = {1, 0, 0, 10000, 0};

void setup() {
    Serial.begin(115200);

    // Core hardware system startup
    safetyLock.init();
    stepper.init();
    dmxRdm.init();
    miletoApp.init();
    displayCtrl.init();
    encoder.init();
    effects.init();

    menuValues[0] = dmxRdm.getStartAddress();
    menuValues[1] = stepper.isCalibrated() ? 1 : 0;
    menuValues[2] = effects.getPointA();
    menuValues[3] = effects.getPointB();
    menuValues[4] = effects.getGroupId();
}

void loop() {
    // 1. Keep asynchronous engines ticking
    stepper.update();
    dmxRdm.update();
    miletoApp.update();
    effects.update();
    encoder.update();

    // 2. Local Control & Display Logic
    static int lastMenuPos = -1;
    int currentMenuPos = encoder.getMenuPosition() % MENU_TOTAL;

    if (encoder.isButtonPressed()) {
        // Trigger action based on selection
        switch (currentMenuPos) {
            case 0: // Increment DMX Address (wraps at 506)
                {
                    uint16_t nextAddr = dmxRdm.getStartAddress() + 1;
                    if (nextAddr > 506) nextAddr = 1;
                    dmxRdm.setStartAddress(nextAddr);
                    menuValues[0] = nextAddr;
                }
                break;
            case 1: // Start Manual Homing Calibration
                stepper.startHoming();
                break;
            case 2: // Record current position as Point A
                effects.setPointA(stepper.getCurrentPosition());
                menuValues[2] = stepper.getCurrentPosition();
                break;
            case 3: // Record current position as Point B
                effects.setPointB(stepper.getCurrentPosition());
                menuValues[3] = stepper.getCurrentPosition();
                break;
            case 4: // Change sync group
                {
                    uint8_t nextGroup = (effects.getGroupId() + 1) % 16;
                    effects.setGroupId(nextGroup);
                    menuValues[4] = nextGroup;
                }
                break;
        }
        lastMenuPos = -1; // Force redraw
    }

    if (currentMenuPos != lastMenuPos) {
        menuValues[1] = stepper.isCalibrated() ? 1 : 0;
        displayCtrl.drawMenu(currentMenuPos, MENU_TOTAL, menuOptions, menuValues);
        lastMenuPos = currentMenuPos;
    }

    // 3. Process Live DMX Command Directives
    if (dmxRdm.hasNewData()) {
        uint8_t mode = dmxRdm.getChannelValue(DMX_CH_MODE);
        uint8_t speedVal = dmxRdm.getChannelValue(DMX_CH_SPEED);
        uint8_t lockCmd = dmxRdm.getChannelValue(DMX_CH_LOCK_CMD);
        uint8_t groupId = dmxRdm.getChannelValue(DMX_CH_GROUP_ID);
        uint8_t effectId = dmxRdm.getChannelValue(DMX_CH_EFFECT_ID);

        // Map speed channel
        float speed = map(speedVal, 0, 255, 100, MAX_SPEED);
        stepper.setMaxSpeed(speed);

        // Update active sync group
        effects.setGroupId(groupId);
        menuValues[4] = groupId;

        // Forced Solenoid Lock command overrides standard safety
        if (lockCmd >= 128) {
            safetyLock.lock();
        }

        switch (mode) {
            case 0: // Idle / Stop
                effects.stopEffect();
                stepper.setTargetPosition(stepper.getCurrentPosition());
                break;
            case 1: // Goto position
                {
                    effects.stopEffect();
                    uint16_t rawPos = (dmxRdm.getChannelValue(DMX_CH_POS_MSB) << 8) | dmxRdm.getChannelValue(DMX_CH_POS_LSB);
                    // Map 16-bit DMX position range (0-65535) into calibrated start-end physical boundary
                    long targetPos = map(rawPos, 0, 65535, effects.getPointA(), effects.getPointB());
                    stepper.setTargetPosition(targetPos);
                }
                break;
            case 2: // Run Series Effect Sequence
                effects.startEffect(effectId);
                break;
            case 3: // Start calibration homing sequence
                effects.stopEffect();
                stepper.startHoming();
                break;
        }
        dmxRdm.clearNewDataFlag();
    }
}
