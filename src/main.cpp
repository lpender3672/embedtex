#include <Arduino.h>
#undef PI

extern "C" {
    int _open(const char *name, int flags, int mode) { return -1; }
}

#include <TFT_eSPI.h>
#include "latex.h"
#include "graphic/graphic_tft.h"

using namespace tex;

TFT_eSPI tft;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    Serial.println("Initializing TFT...");
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_WHITE);
    
    Serial.println("Initializing MicroTeX...");
    
    // MicroTeX needs resource files loaded first
    // You'll need the res/ folder from MicroTeX with fonts
    // LaTeX::init("path/to/res");  // Adjust path for your setup
    
    auto render = LaTeX::parse(
        L"\\frac{x^2}{y}",
        720,
        24,
        24,
        0xFF000000
    );
    
    if (render) {
        Serial.printf("Render size: %d x %d\n", render->getWidth(), render->getHeight());
        
        Graphics2D_tft g2d(&tft);
        render->draw(g2d, 10, 50);  // Draw at position (10, 50)
    } else {
        Serial.println("Failed to parse LaTeX");
    }
}

void loop() {}
