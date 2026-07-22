#ifndef MILETO_APP_H
#define MILETO_APP_H

#include "config.h"

class MiletoApp {
private:
    bool _isConnected;

public:
    MiletoApp();
    void init();
    void update();
    bool isConnected() const { return _isConnected; }
    void setConnected(bool connected) { _isConnected = connected; }
};

extern MiletoApp miletoApp;

#endif // MILETO_APP_H
