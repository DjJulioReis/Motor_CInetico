#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"

class DisplayController {
public:
    DisplayController();
    void init();
    void update();
    void drawMenu(int currentSelection, int totalMenuOptions, const char* options[], int extraValues[]);
};

extern DisplayController displayCtrl;

#endif // DISPLAY_H
