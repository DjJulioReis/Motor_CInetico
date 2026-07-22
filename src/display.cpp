#include "display.h"
#include "stepper.h"
#include "dmx_rdm.h"
#include "mileto_app.h"
#include <Adafruit_SSD1306.h>

DisplayController displayCtrl;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

DisplayController::DisplayController() {}

void DisplayController::init() {
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        // Serial fallback logging if display fails to mount
        Serial.println(F("SSD1306 allocation failed"));
    } else {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Mileto DMX - Init OK");
        display.display();
    }
}

void DisplayController::drawMenu(int currentSelection, int totalMenuOptions, const char* options[], int extraValues[]) {
    display.clearDisplay();

    // Status Bar Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("DMX:");
    display.print(dmxRdm.getStartAddress());
    display.print(" ");
    display.print(miletoApp.isConnected() ? "[BLE]" : "[DISC]");
    display.print(" ");
    display.println(stepper.isCalibrated() ? "CAL" : "UNCAL");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // List Menu items (with simple viewport management)
    int startIdx = (currentSelection / 4) * 4;
    for (int i = 0; i < 4; i++) {
        int idx = startIdx + i;
        if (idx >= totalMenuOptions) break;

        display.setCursor(10, 15 + (i * 12));
        if (idx == currentSelection) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.print(options[idx]);
        if (extraValues != nullptr && extraValues[idx] != -1) {
            display.print(": ");
            display.print(extraValues[idx]);
        }
    }

    display.display();
}

void DisplayController::update() {
    // Basic cyclic state loop visualization if no menu is explicitly active
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 500) {
        lastUpdate = millis();
        // Redraw automatically
    }
}
