#include "dmx_rdm.h"
#include <HardwareSerial.h>

DmxRdmController dmxRdm;

// Initialize ESP32-C3 hardware serial for DMX/RDM.
// ESP32-C3 has Serial0 (USB-CDC) and Serial1 (Hardware UART).
// We configure Serial1 with 250,000 baud, 8 data bits, 2 stop bits.
#define DMX_SERIAL Serial1

DmxRdmController::DmxRdmController() :
    _startAddress(1),
    _newPacketReceived(false),
    _isRdmMuted(false) {
    // Generate a Unique ID (UID) for RDM using ESP32 MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    _uid[0] = 0x7F; // Manufacturer ID (Prototype/Development)
    _uid[1] = 0x0A;
    _uid[2] = mac[2];
    _uid[3] = mac[3];
    _uid[4] = mac[4];
    _uid[5] = mac[5];
}

void DmxRdmController::init() {
    pinMode(DMX_DE_RE_PIN, OUTPUT);
    digitalWrite(DMX_DE_RE_PIN, LOW); // Set to default Receive mode

    // Standard DMX rate is 250kbps, 8N2
    DMX_SERIAL.begin(250000, SERIAL_8N2, DMX_RX_PIN, DMX_TX_PIN);
}

void DmxRdmController::setStartAddress(uint16_t address) {
    if (address >= 1 && address <= 512 - DMX_CH_LENGTH + 1) {
        _startAddress = address;
    }
}

uint8_t DmxRdmController::getChannelValue(uint16_t offset) const {
    uint16_t absoluteChannel = _startAddress + offset - 1;
    if (absoluteChannel > 0 && absoluteChannel <= 512) {
        return _dmxBuffer[absoluteChannel];
    }
    return 0;
}

bool DmxRdmController::hasNewData() {
    return _newPacketReceived;
}

void DmxRdmController::update() {
    // Check if there is incoming DMX/RDM packet data
    if (DMX_SERIAL.available()) {
        static uint16_t bufferIndex = 0;
        static bool inFrame = false;
        static unsigned long lastByteTime = 0;

        unsigned long now = micros();
        // Standard DMX/RDM frame alignment via hardware break or idle line pause detection.
        // A break / idle pause at 250kbps is at least 88us. A gap of >100us between bytes indicates start of frame.
        if (now - lastByteTime > 150) {
            bufferIndex = 0;
            inFrame = false;
        }
        lastByteTime = now;

        while (DMX_SERIAL.available()) {
            uint8_t data = DMX_SERIAL.read();

            if (bufferIndex == 0) {
                if (data == 0x00) { // DMX Start Code after alignment break
                    inFrame = true;
                    _dmxBuffer[0] = data;
                    bufferIndex = 1;
                } else if (data == 0xCC) { // RDM Start Code after alignment break
                    inFrame = true;
                    _dmxBuffer[0] = data;
                    bufferIndex = 1;
                }
            } else if (inFrame) {
                _dmxBuffer[bufferIndex++] = data;

                // If standard DMX, we read up to 513 bytes
                if (_dmxBuffer[0] == 0x00 && bufferIndex >= 513) {
                    _newPacketReceived = true;
                    bufferIndex = 0;
                    inFrame = false;
                }
                // Handle RDM framing
                else if (_dmxBuffer[0] == 0xCC && bufferIndex > 2) {
                    uint8_t rdmLength = _dmxBuffer[2];
                    if (bufferIndex >= rdmLength) {
                        handleRdmPacket(_dmxBuffer, rdmLength);
                        bufferIndex = 0;
                        inFrame = false;
                    }
                }
            }
        }
    }
}

void DmxRdmController::handleRdmPacket(uint8_t* rdmData, uint16_t length) {
    if (length < 24) return;

    // Destructure Destination UID to ensure discovery/set packages are targeted to us or within branch limits
    uint8_t destUid[6];
    for (int i = 0; i < 6; i++) {
        destUid[i] = rdmData[3 + i];
    }

    uint8_t commandClass = rdmData[20];
    uint16_t pid = (rdmData[21] << 8) | rdmData[22];

    if (commandClass == 0x10) { // DISCOVERY_COMMAND
        if (pid == 0x0001) { // DISC_UNIQUE_BRANCH
            // Disc Unique Branch payload has 12 bytes of data (lower bound UID and upper bound UID) starting at offset 24
            if (length >= 36) {
                uint8_t lowerBound[6];
                uint8_t upperBound[6];
                for (int i = 0; i < 6; i++) {
                    lowerBound[i] = rdmData[24 + i];
                    upperBound[i] = rdmData[30 + i];
                }

                // Check if our UID fits between [lowerBound, upperBound] lexicographically
                bool isHigherThanLower = true;
                bool isLowerThanUpper = true;
                for (int i = 0; i < 6; i++) {
                    if (_uid[i] < lowerBound[i]) { isHigherThanLower = false; break; }
                    else if (_uid[i] > lowerBound[i]) { break; }
                }
                for (int i = 0; i < 6; i++) {
                    if (_uid[i] > upperBound[i]) { isLowerThanUpper = false; break; }
                    else if (_uid[i] < upperBound[i]) { break; }
                }

                if (isHigherThanLower && isLowerThanUpper && !_isRdmMuted) {
                    // Respond with preamble and modulated discovery response to prevent collisions
                    uint8_t response[24];
                    response[0] = 0xFE; // Preamble
                    response[1] = 0xFE;
                    response[2] = 0xFE;
                    response[3] = 0xFE;
                    response[4] = 0xFE;
                    response[5] = 0xFE;
                    response[6] = 0xFE;
                    response[7] = 0xFE;
                    response[8] = 0xAA; // Start Code delimiter
                    for(int i=0; i<6; i++) {
                        response[9 + i*2] = _uid[i] | 0xAA;
                        response[10 + i*2] = _uid[i] | 0x55;
                    }
                    sendRdmResponse(response, 21);
                }
            }
        } else if (pid == 0x0002) { // DISC_MUTE
            // Verify if command is directly addressed to us
            bool isMe = true;
            for (int i = 0; i < 6; i++) {
                if (destUid[i] != _uid[i]) isMe = false;
            }
            if (isMe) {
                _isRdmMuted = true;
                uint8_t response[1] = { 0x00 };
                sendRdmResponse(response, 1);
            }
        } else if (pid == 0x0003) { // DISC_UNMUTE
            bool isMe = true;
            for (int i = 0; i < 6; i++) {
                if (destUid[i] != _uid[i]) isMe = false;
            }
            if (isMe) {
                _isRdmMuted = false;
                uint8_t response[1] = { 0x00 };
                sendRdmResponse(response, 1);
            }
        }
    } else if (commandClass == 0x20) { // GET_COMMAND
        // Verify if command is directly addressed to us
        bool isMe = true;
        for (int i = 0; i < 6; i++) {
            if (destUid[i] != _uid[i]) isMe = false;
        }
        if (isMe && pid == 0x00F0) { // DMX_START_ADDRESS
            uint8_t response[2] = { (uint8_t)(_startAddress >> 8), (uint8_t)(_startAddress & 0xFF) };
            sendRdmResponse(response, 2);
        }
    } else if (commandClass == 0x30) { // SET_COMMAND
        bool isMe = true;
        for (int i = 0; i < 6; i++) {
            if (destUid[i] != _uid[i]) isMe = false;
        }
        if (isMe && pid == 0x00F0) { // DMX_START_ADDRESS
            uint16_t newAddress = (rdmData[24] << 8) | rdmData[25];
            setStartAddress(newAddress);
            uint8_t response[2] = { (uint8_t)(_startAddress >> 8), (uint8_t)(_startAddress & 0xFF) };
            sendRdmResponse(response, 2);
        }
    }
}

void DmxRdmController::sendRdmResponse(uint8_t* response, uint16_t length) {
    digitalWrite(DMX_DE_RE_PIN, HIGH); // Switch RS485 chip to Transmit Mode
    delayMicroseconds(10);
    DMX_SERIAL.write(response, length);
    DMX_SERIAL.flush();
    delayMicroseconds(10);
    digitalWrite(DMX_DE_RE_PIN, LOW); // Return RS485 chip to Receive Mode
}
