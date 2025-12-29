#include <Arduino.h>
#undef PI

extern "C" {
    int _open(const char *name, int flags, int mode) { return -1; }
    int _stat(const char *name, void *st) { return -1; }
}

#include <TFT_eSPI.h>
#include <SD.h>
#include "latex.h"
#include "graphic/graphic_tft.h"

using namespace tex;

TFT_eSPI tft;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    // Make sure the filesystem that holds /res/fonts/... is mounted.
    // For Teensy 4.1 built-in SD card slot:
    if (!SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD.begin(BUILTIN_SDCARD) failed");
    }

    SPI.begin();
    SPI.setClockDivider(SPI_CLOCK_DIV64);

    pinMode(10, OUTPUT); // CS
    pinMode(9, OUTPUT);  // DC
    pinMode(8, OUTPUT);  // RST
    
    Serial.println("Initializing TFT...");
    digitalWrite(8, HIGH);
    delay(100);
    digitalWrite(8, LOW);
    delay(100);
    digitalWrite(8, HIGH);
    delay(200);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_WHITE);

    uint16_t id1 = tft.readcommand16(0x04);  // Read display ID
    uint8_t id2 = tft.readcommand8(0x09);    // Read status
    uint32_t id3 = tft.readcommand32(0xEF);  // Read ID4 (some displays)
    
    Serial.print("ID 0x04: 0x"); Serial.println(id1, HEX);
    Serial.print("ID 0x09: 0x"); Serial.println(id2, HEX);
    Serial.print("ID 0xEF: 0x"); Serial.println(id3, HEX);
    
    Serial.println("Initializing MicroTeX...");
    
    // MicroTeX needs resource files loaded first
    // You'll need the res/ folder from MicroTeX with fonts
    // LaTeX::init("path/to/res");  // Adjust path for your setup
    LaTeX::init("/res");

    Serial.println("Parsing LaTeX...");
    
    // Convert the code to a paintable object (TeXRender)
    auto render = LaTeX::parse(
         L"\\text{Hello world}",   // LaTeX code to parse
        TFT_WIDTH,    // logical width of the graphics context (in pixel)
        32,     // font size (in point)
        16,     // space between 2 lines (in pixel)
        BLACK   // foreground color
    );
    
    if (render) {
        Serial.printf("Render size: %d x %d\n", render->getWidth(), render->getHeight());

        OpenFontRender ofr;
        ofr.setSerial(Serial);
        ofr.setDrawer(tft);
        Graphics2D_tft g2d(&tft, &ofr);
        render->draw(g2d, 100, 50);  // Draw at position (0, 0)
    } else {
        Serial.println("Failed to parse LaTeX");
    }
}

void loop() {}
