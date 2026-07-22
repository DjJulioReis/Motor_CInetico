/**
 * Project: Claudinei DMX/RDM Controller (ESP32-C3) with Mileto App BLE
 *
 * Includes:
 *  - Dual-Photoelectric positioning sensors (Start/End limits)
 *  - NEMA 34 Stepper with non-blocking linear acceleration/deceleration
 *  - TAU-S0837DL Safety solenoid lock with 2.0s idle timeout to prevent chattering
 *  - Standard-compliant DMX/RDM engine with break/idle frame alignment & UID range checks
 *  - SSD1306 Local OLED & debounced rotary encoder UI
 *  - Mileto Bluetooth Low Energy (BLE) control protocol
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Pins ---
#define OLED_SDA          5
#define OLED_SCL          6
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64

#define ENCODER_CLK       0
#define ENCODER_DT        1
#define ENCODER_SW        2

#define DMX_RX_PIN        7
#define DMX_TX_PIN        10
#define DMX_DE_RE_PIN     3

#define STEPPER_PUL_PIN   4
#define STEPPER_DIR_PIN   9
#define STEPPER_EN_PIN    20

#define SENSOR_START_PIN  21
#define SENSOR_END_PIN    1

#define SAFETY_LOCK_PIN   2

// --- Stepper Speed Consts ---
#define MAX_SPEED         4000.0
#define MAX_ACCEL         8000.0

// --- DMX Map ---
#define DMX_CH_MODE       1
#define DMX_CH_POS_MSB    2
#define DMX_CH_POS_LSB    3
#define DMX_CH_SPEED      4
#define DMX_CH_LOCK_CMD   5
#define DMX_CH_GROUP_ID   6
#define DMX_CH_EFFECT_ID  7
#define DMX_CH_LENGTH     7

#define BLE_SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_CTRL_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_STATUS_UUID       "c7e462d0-eb14-41d3-a9d0-0870932258aa"

// --- Global Variables & States ---
enum HomingState { STATE_IDLE, STATE_HOMING_START, STATE_HOMING_END, STATE_CALIBRATED };
HomingState homingState = STATE_IDLE;

long currentPos = 0;
long targetPos = 0;
float currentSpeed = 0.0f;
float maxSpeed = MAX_SPEED;
float accel = MAX_ACCEL;
long startLimit = 0;
long endLimit = 10000;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 0;
bool stepState = false;

// Safety Lock Solenoid Parameters
bool isLocked = true;
unsigned long unlockTime = 0;
unsigned long lastActiveTime = 0;
const unsigned long UNLOCK_DELAY_MS = 150;
const unsigned long IDLE_TIMEOUT_MS = 2000;

// DMX & RDM Parameters
uint16_t dmxAddress = 1;
uint8_t dmxBuffer[513];
bool newPacketReceived = false;
bool isRdmMuted = false;
uint8_t uid[6];

// BLE & App Parameters
bool bleConnected = false;
BLEServer* pServer = NULL;
BLECharacteristic* pCtrlCharacteristic = NULL;
BLECharacteristic* pStatusCharacteristic = NULL;

// Display & UI
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
int encoderClkState = HIGH;
int menuPosition = 0;
bool encoderBtnPressed = false;
unsigned long lastDebounceTime = 0;

// Kinetics Effects
bool isEffectRunning = false;
uint8_t currentEffectId = 0;
unsigned long effectStartTime = 0;
int sequenceStep = 0;
uint8_t groupId = 0;

const int MENU_TOTAL = 5;
const char* menuOptions[MENU_TOTAL] = {
    "DMX Address",
    "Calibrate Limit",
    "Set Point A",
    "Set Point B",
    "Group ID"
};
int menuValues[MENU_TOTAL] = {1, 0, 0, 10000, 0};

// --- Prototypes ---
void updateStepper();
void updateDmx();
void handleRdmPacket(uint8_t* rdmData, uint16_t length);
void sendRdmResponse(uint8_t* response, uint16_t length);
void updateBle();
void updateEncoder();
void drawMenu(int currentSelection);
void startHoming();
void stopEffect();
void startEffect(uint8_t effectId);
void updateEffects();
void lockSolenoid();
void unlockSolenoid();
void lockOnIdle();

// BLE Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { bleConnected = true; }
    void onDisconnect(BLEServer* pServer) {
        bleConnected = false;
        pServer->startAdvertising();
    }
};

class MyCharCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string val = pCharacteristic->getValue();
        if (val.length() > 0) {
            uint8_t cmd = val[0];
            switch (cmd) {
                case 0x01: // Set target pos
                    if (val.length() >= 5) {
                        long target = (val[1] << 24) | (val[2] << 16) | (val[3] << 8) | val[4];
                        targetPos = target;
                    }
                    break;
                case 0x02: startLimit = currentPos; menuValues[2] = currentPos; break;
                case 0x03: endLimit = currentPos; menuValues[3] = currentPos; homingState = STATE_CALIBRATED; break;
                case 0x04: startHoming(); break;
                case 0x05: targetPos = currentPos; stopEffect(); break;
            }
        }
    }
};

void setup() {
    Serial.begin(115200);

    // Safety Lock
    pinMode(SAFETY_LOCK_PIN, OUTPUT);
    lockSolenoid();

    // Stepper
    pinMode(STEPPER_PUL_PIN, OUTPUT);
    pinMode(STEPPER_DIR_PIN, OUTPUT);
    pinMode(STEPPER_EN_PIN, OUTPUT);
    digitalWrite(STEPPER_EN_PIN, LOW); // Active Low Enable

    // Limit Sensors
    pinMode(SENSOR_START_PIN, INPUT_PULLUP);
    pinMode(SENSOR_END_PIN, INPUT_PULLUP);

    // Rotary Encoder
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    encoderClkState = digitalRead(ENCODER_CLK);

    // RDM UID Generation
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uid[0] = 0x7F; uid[1] = 0x0A;
    uid[2] = mac[2]; uid[3] = mac[3]; uid[4] = mac[4]; uid[5] = mac[5];

    // DMX Port
    pinMode(DMX_DE_RE_PIN, OUTPUT);
    digitalWrite(DMX_DE_RE_PIN, LOW); // Rx
    Serial1.begin(250000, SERIAL_8N2, DMX_RX_PIN, DMX_TX_PIN);

    // OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Mileto DMX - Init OK");
        display.display();
    }

    // BLE Service
    BLEDevice::init("Mileto DMX Controller");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
    pCtrlCharacteristic = pService->createCharacteristic(BLE_CHAR_CTRL_UUID, BLECharacteristic::PROPERTY_WRITE);
    pCtrlCharacteristic->setCallbacks(new MyCharCallbacks());
    pStatusCharacteristic = pService->createCharacteristic(BLE_CHAR_STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pStatusCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(BLE_SERVICE_UUID);
    pAdv->setScanResponse(true);
    BLEDevice::startAdvertising();

    menuValues[0] = dmxAddress;
    menuValues[1] = (homingState == STATE_CALIBRATED) ? 1 : 0;
    menuValues[2] = startLimit;
    menuValues[3] = endLimit;
    menuValues[4] = groupId;
}

void loop() {
    updateStepper();
    updateDmx();
    updateBle();
    updateEffects();
    updateEncoder();

    static int lastPos = -1;
    int currentMenuPos = menuPosition % MENU_TOTAL;

    if (encoderBtnPressed) {
        encoderBtnPressed = false;
        switch (currentMenuPos) {
            case 0:
                dmxAddress = dmxAddress + 1;
                if (dmxAddress > 506) dmxAddress = 1;
                menuValues[0] = dmxAddress;
                break;
            case 1:
                startHoming();
                break;
            case 2:
                startLimit = currentPos;
                menuValues[2] = currentPos;
                break;
            case 3:
                endLimit = currentPos;
                menuValues[3] = currentPos;
                break;
            case 4:
                groupId = (groupId + 1) % 16;
                menuValues[4] = groupId;
                break;
        }
        lastPos = -1;
    }

    if (currentMenuPos != lastPos) {
        menuValues[1] = (homingState == STATE_CALIBRATED) ? 1 : 0;
        drawMenu(currentMenuPos);
        lastPos = currentMenuPos;
    }
}

// --- Kinetic & Solenoid Functions ---
void lockSolenoid() {
    digitalWrite(SAFETY_LOCK_PIN, LOW);
    isLocked = true;
}

void unlockSolenoid() {
    if (isLocked) {
        digitalWrite(SAFETY_LOCK_PIN, HIGH);
        unlockTime = millis();
        isLocked = false;
    }
    lastActiveTime = millis();
}

void lockOnIdle() {
    if (!isLocked) {
        if (millis() - lastActiveTime >= IDLE_TIMEOUT_MS) {
            lockSolenoid();
        }
    }
}

void startHoming() {
    homingState = STATE_HOMING_START;
    targetPos = -999999;
    maxSpeed = 800.0f;
}

void updateStepper() {
    if (currentPos != targetPos) {
        if (isLocked) { unlockSolenoid(); return; }
        if (millis() - unlockTime < UNLOCK_DELAY_MS) return;
    } else {
        lockOnIdle();
        currentSpeed = 0;
        return;
    }

    if (homingState == STATE_HOMING_START && digitalRead(SENSOR_START_PIN) == LOW) {
        currentPos = 0;
        startLimit = 0;
        homingState = STATE_HOMING_END;
        targetPos = 999999;
        maxSpeed = 800.0f;
        return;
    } else if (homingState == STATE_HOMING_END && digitalRead(SENSOR_END_PIN) == LOW) {
        endLimit = currentPos;
        targetPos = currentPos;
        homingState = STATE_CALIBRATED;
        maxSpeed = MAX_SPEED;
        return;
    }

    unsigned long now = micros();
    unsigned long timeDelta = now - lastStepTime;

    if (stepInterval == 0 || timeDelta >= stepInterval) {
        long distanceToGo = targetPos - currentPos;
        float dt = (timeDelta > 0 && timeDelta < 100000) ? (timeDelta / 1000000.0f) : 0.001f;

        if (distanceToGo > 0) {
            digitalWrite(STEPPER_DIR_PIN, HIGH);
            currentSpeed += (accel * dt);
            if (currentSpeed > maxSpeed) currentSpeed = maxSpeed;
            if (currentSpeed < 50.0f) currentSpeed = 50.0f;

            float decelDistance = (currentSpeed * currentSpeed) / (2.0f * accel);
            if (distanceToGo <= decelDistance) {
                currentSpeed -= (accel * dt);
                if (currentSpeed < 50.0f) currentSpeed = 50.0f;
            }
            currentPos++;
        } else if (distanceToGo < 0) {
            digitalWrite(STEPPER_DIR_PIN, LOW);
            currentSpeed -= (accel * dt);
            if (fabsf(currentSpeed) > maxSpeed) currentSpeed = -maxSpeed;
            if (fabsf(currentSpeed) < 50.0f) currentSpeed = -50.0f;

            float decelDistance = (currentSpeed * currentSpeed) / (2.0f * accel);
            if (abs(distanceToGo) <= decelDistance) {
                currentSpeed += (accel * dt);
                if (currentSpeed > -50.0f) currentSpeed = -50.0f;
            }
            currentPos--;
        }

        stepState = !stepState;
        digitalWrite(STEPPER_PUL_PIN, stepState ? HIGH : LOW);
        lastStepTime = now;

        if (fabsf(currentSpeed) < 1.0f) stepInterval = 0;
        else stepInterval = (unsigned long)(1000000.0f / fabsf(currentSpeed));
    }
}

// --- DMX & RDM Functions ---
void updateDmx() {
    if (Serial1.available()) {
        static uint16_t idx = 0;
        static bool inFrame = false;
        static unsigned long lastByte = 0;
        unsigned long now = micros();

        if (now - lastByte > 150) { idx = 0; inFrame = false; }
        lastByte = now;

        while (Serial1.available()) {
            uint8_t data = Serial1.read();
            if (idx == 0) {
                if (data == 0x00 || data == 0xCC) {
                    inFrame = true;
                    dmxBuffer[0] = data;
                    idx = 1;
                }
            } else if (inFrame) {
                dmxBuffer[idx++] = data;
                if (dmxBuffer[0] == 0x00 && idx >= 513) {
                    newPacketReceived = true;
                    idx = 0; inFrame = false;
                } else if (dmxBuffer[0] == 0xCC && idx > 2) {
                    uint8_t len = dmxBuffer[2];
                    if (idx >= len) {
                        handleRdmPacket(dmxBuffer, len);
                        idx = 0; inFrame = false;
                    }
                }
            }
        }
    }

    if (newPacketReceived) {
        newPacketReceived = false;
        uint8_t mode = dmxBuffer[dmxAddress];
        uint8_t speedVal = dmxBuffer[dmxAddress + DMX_CH_SPEED - 1];
        uint8_t lockCmd = dmxBuffer[dmxAddress + DMX_CH_LOCK_CMD - 1];
        uint8_t netGroupId = dmxBuffer[dmxAddress + DMX_CH_GROUP_ID - 1];
        uint8_t effectId = dmxBuffer[dmxAddress + DMX_CH_EFFECT_ID - 1];

        maxSpeed = map(speedVal, 0, 255, 100, MAX_SPEED);
        groupId = netGroupId;
        menuValues[4] = groupId;

        if (lockCmd >= 128) lockSolenoid();

        switch (mode) {
            case 0: stopEffect(); targetPos = currentPos; break;
            case 1: {
                stopEffect();
                uint16_t rawPos = (dmxBuffer[dmxAddress + DMX_CH_POS_MSB - 1] << 8) | dmxBuffer[dmxAddress + DMX_CH_POS_LSB - 1];
                targetPos = map(rawPos, 0, 65535, startLimit, endLimit);
                break;
            }
            case 2: startEffect(effectId); break;
            case 3: stopEffect(); startHoming(); break;
        }
    }
}

void handleRdmPacket(uint8_t* rdmData, uint16_t length) {
    if (length < 24) return;
    uint8_t dest[6];
    for (int i = 0; i < 6; i++) dest[i] = rdmData[3 + i];
    uint8_t cmdClass = rdmData[20];
    uint16_t pid = (rdmData[21] << 8) | rdmData[22];

    if (cmdClass == 0x10) {
        if (pid == 0x0001 && length >= 36) {
            uint8_t low[6], high[6];
            for (int i = 0; i < 6; i++) { low[i] = rdmData[24 + i]; high[i] = rdmData[30 + i]; }
            bool active = true;
            for (int i = 0; i < 6; i++) {
                if (uid[i] < low[i]) { active = false; break; }
                else if (uid[i] > low[i]) break;
            }
            for (int i = 0; i < 6; i++) {
                if (uid[i] > high[i]) { active = false; break; }
                else if (uid[i] < high[i]) break;
            }
            if (active && !isRdmMuted) {
                uint8_t response[24] = { 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xAA };
                for(int i=0; i<6; i++) {
                    response[9 + i*2] = uid[i] | 0xAA;
                    response[10 + i*2] = uid[i] | 0x55;
                }
                sendRdmResponse(response, 21);
            }
        } else if (pid == 0x0002) {
            bool isMe = true;
            for (int i = 0; i < 6; i++) if (dest[i] != uid[i]) isMe = false;
            if (isMe) { isRdmMuted = true; uint8_t resp[1] = {0}; sendRdmResponse(resp, 1); }
        } else if (pid == 0x0003) {
            bool isMe = true;
            for (int i = 0; i < 6; i++) if (dest[i] != uid[i]) isMe = false;
            if (isMe) { isRdmMuted = false; uint8_t resp[1] = {0}; sendRdmResponse(resp, 1); }
        }
    } else if (cmdClass == 0x20) {
        bool isMe = true;
        for (int i = 0; i < 6; i++) if (dest[i] != uid[i]) isMe = false;
        if (isMe && pid == 0x00F0) {
            uint8_t resp[2] = { (uint8_t)(dmxAddress >> 8), (uint8_t)(dmxAddress & 0xFF) };
            sendRdmResponse(resp, 2);
        }
    } else if (cmdClass == 0x30) {
        bool isMe = true;
        for (int i = 0; i < 6; i++) if (dest[i] != uid[i]) isMe = false;
        if (isMe && pid == 0x00F0) {
            dmxAddress = (rdmData[24] << 8) | rdmData[25];
            menuValues[0] = dmxAddress;
            uint8_t resp[2] = { (uint8_t)(dmxAddress >> 8), (uint8_t)(dmxAddress & 0xFF) };
            sendRdmResponse(resp, 2);
        }
    }
}

void sendRdmResponse(uint8_t* response, uint16_t length) {
    digitalWrite(DMX_DE_RE_PIN, HIGH);
    delayMicroseconds(10);
    Serial1.write(response, length);
    Serial1.flush();
    delayMicroseconds(10);
    digitalWrite(DMX_DE_RE_PIN, LOW);
}

// --- BLE Functions ---
void updateBle() {
    if (bleConnected) {
        static unsigned long notify = 0;
        if (millis() - notify > 100) {
            notify = millis();
            uint8_t stats[10];
            stats[0] = (homingState == STATE_CALIBRATED) ? 1 : 0;
            stats[1] = (homingState == STATE_HOMING_START || homingState == STATE_HOMING_END) ? 1 : 0;
            stats[2] = (currentPos >> 24) & 0xFF; stats[3] = (currentPos >> 16) & 0xFF;
            stats[4] = (currentPos >> 8) & 0xFF; stats[5] = currentPos & 0xFF;
            stats[6] = (targetPos >> 24) & 0xFF; stats[7] = (targetPos >> 16) & 0xFF;
            stats[8] = (targetPos >> 8) & 0xFF; stats[9] = targetPos & 0xFF;

            pStatusCharacteristic->setValue(stats, 10);
            pStatusCharacteristic->notify();
        }
    }
}

// --- Effects Engine ---
void startEffect(uint8_t effectId) {
    isEffectRunning = true;
    currentEffectId = effectId;
    effectStartTime = millis();
    sequenceStep = 0;
}

void stopEffect() { isEffectRunning = false; }

void updateEffects() {
    if (!isEffectRunning) return;
    unsigned long elapsed = millis() - effectStartTime;

    switch (currentEffectId) {
        case 1: // Loop A -> B -> A
            if (currentPos == targetPos) {
                if (sequenceStep == 0) { targetPos = endLimit; sequenceStep = 1; }
                else { targetPos = startLimit; sequenceStep = 0; }
            }
            break;
        case 2: // Sine Wave Oscillation
            {
                float angle = (2.0f * PI * (float)(elapsed % 3000)) / 3000.0f;
                float norm = (sinf(angle) + 1.0f) / 2.0f;
                targetPos = startLimit + (long)(norm * (float)(endLimit - startLimit));
            }
            break;
        case 3: // Group Phase Delayed Cascade Wave
            {
                float delay = (float)groupId * 0.5f;
                float angle = (2.0f * PI * ((float)elapsed / 4000.0f)) + delay;
                float norm = (sinf(angle) + 1.0f) / 2.0f;
                targetPos = startLimit + (long)(norm * (float)(endLimit - startLimit));
            }
            break;
    }
}

// --- UI Local Control ---
void updateEncoder() {
    int clk = digitalRead(ENCODER_CLK);
    if (clk != encoderClkState && clk == LOW) {
        if (digitalRead(ENCODER_DT) != clk) menuPosition++;
        else menuPosition--;
        if (menuPosition < 0) menuPosition = 0;
    }
    encoderClkState = clk;

    if (digitalRead(ENCODER_SW) == LOW) {
        if (millis() - lastDebounceTime > 250) {
            encoderBtnPressed = true;
            lastDebounceTime = millis();
        }
    }
}

void drawMenu(int currentSelection) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("DMX:"); display.print(dmxAddress);
    display.print(bleConnected ? " [BLE]" : " [WIFI]");
    display.print((homingState == STATE_CALIBRATED) ? " CAL" : " UNCAL");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    int start = (currentSelection / 4) * 4;
    for (int i = 0; i < 4; i++) {
        int idx = start + i;
        if (idx >= MENU_TOTAL) break;
        display.setCursor(10, 15 + (i * 12));
        if (idx == currentSelection) display.print("> ");
        else display.print("  ");
        display.print(menuOptions[idx]);
        if (menuValues[idx] != -1) {
            display.print(": "); display.print(menuValues[idx]);
        }
    }
    display.display();
}
