#include "mileto_app.h"
#include "stepper.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

MiletoApp miletoApp;

BLEServer* pServer = NULL;
BLECharacteristic* pCtrlCharacteristic = NULL;
BLECharacteristic* pStatusCharacteristic = NULL;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        miletoApp.setConnected(true);
    };

    void onDisconnect(BLEServer* pServer) {
        miletoApp.setConnected(false);
        // Restart advertising
        pServer->startAdvertising();
    }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            uint8_t cmd = value[0];
            switch (cmd) {
                case 0x01: // Target position command: [0x01, POS_BYTE_3, POS_BYTE_2, POS_BYTE_1, POS_BYTE_0]
                    if (value.length() >= 5) {
                        long target = (value[1] << 24) | (value[2] << 16) | (value[3] << 8) | value[4];
                        stepper.setTargetPosition(target);
                    }
                    break;
                case 0x02: // Set Start Calibration position
                    stepper.setStartLimit(stepper.getCurrentPosition());
                    break;
                case 0x03: // Set End Calibration position
                    stepper.setEndLimit(stepper.getCurrentPosition());
                    break;
                case 0x04: // Start Homing/Calibration routine
                    stepper.startHoming();
                    break;
                case 0x05: // Emergency Stop
                    stepper.setTargetPosition(stepper.getCurrentPosition());
                    break;
            }
        }
    }
};

MiletoApp::MiletoApp() : _isConnected(false) {}

void MiletoApp::init() {
    BLEDevice::init("Mileto DMX Controller");

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    pCtrlCharacteristic = pService->createCharacteristic(
                            BLE_CHAR_CTRL_UUID,
                            BLECharacteristic::PROPERTY_WRITE
                          );
    pCtrlCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

    pStatusCharacteristic = pService->createCharacteristic(
                              BLE_CHAR_STATUS_UUID,
                              BLECharacteristic::PROPERTY_READ |
                              BLECharacteristic::PROPERTY_NOTIFY
                            );
    pStatusCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
}

void MiletoApp::update() {
    if (_isConnected) {
        static unsigned long lastNotify = 0;
        if (millis() - lastNotify > 100) { // Notify status every 100ms
            long curPos = stepper.getCurrentPosition();
            long targetPos = stepper.getTargetPosition();

            uint8_t statusData[10];
            statusData[0] = stepper.isCalibrated() ? 1 : 0;
            statusData[1] = stepper.isHoming() ? 1 : 0;
            statusData[2] = (curPos >> 24) & 0xFF;
            statusData[3] = (curPos >> 16) & 0xFF;
            statusData[4] = (curPos >> 8) & 0xFF;
            statusData[5] = curPos & 0xFF;
            statusData[6] = (targetPos >> 24) & 0xFF;
            statusData[7] = (targetPos >> 16) & 0xFF;
            statusData[8] = (targetPos >> 8) & 0xFF;
            statusData[9] = targetPos & 0xFF;

            pStatusCharacteristic->setValue(statusData, 10);
            pStatusCharacteristic->notify();
            lastNotify = millis();
        }
    }
}
