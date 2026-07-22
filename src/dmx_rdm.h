#ifndef DMX_RDM_H
#define DMX_RDM_H

#include "config.h"

class DmxRdmController {
private:
    uint16_t _startAddress;
    uint8_t _dmxBuffer[513];
    bool _newPacketReceived;
    bool _isRdmMuted;

    // RDM State
    uint8_t _uid[6]; // Unique ID for RDM Discovery

public:
    DmxRdmController();
    void init();
    void update();

    uint16_t getStartAddress() const { return _startAddress; }
    void setStartAddress(uint16_t address);

    uint8_t getChannelValue(uint16_t offset) const;
    bool hasNewData();
    void clearNewDataFlag() { _newPacketReceived = false; }

    void handleRdmPacket(uint8_t* rdmData, uint16_t length);
    void sendRdmResponse(uint8_t* response, uint16_t length);
};

extern DmxRdmController dmxRdm;

#endif // DMX_RDM_H
