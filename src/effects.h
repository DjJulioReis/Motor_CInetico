#ifndef EFFECTS_H
#define EFFECTS_H

#include "config.h"

struct KineticPoint {
    long position;
    unsigned long durationMs;
};

class EffectsEngine {
private:
    long _pointA;
    long _pointB;
    bool _hasPoints;
    uint8_t _groupId;

    // Series Motion State
    bool _isEffectRunning;
    uint8_t _currentEffectId;
    unsigned long _effectStartTime;
    unsigned long _lastUpdate;
    int _sequenceStep;

public:
    EffectsEngine();
    void init();

    void setPointA(long pos);
    void setPointB(long pos);
    long getPointA() const { return _pointA; }
    long getPointB() const { return _pointB; }
    bool hasPoints() const { return _hasPoints; }

    void setGroupId(uint8_t id) { _groupId = id; }
    uint8_t getGroupId() const { return _groupId; }

    void startEffect(uint8_t effectId);
    void stopEffect();
    void update();
};

extern EffectsEngine effects;

#endif // EFFECTS_H
