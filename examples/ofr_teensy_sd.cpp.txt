#include <Arduino.h>

// OpenFontRender uses weak file I/O hooks (OFR_fopen/OFR_fread/...) by default.
// This translation unit provides Teensy+SD implementations so loadFont("/path.ttf") works.
#include <OpenFontRender.h>
#include "ofrfs/Teensy_SD_Preset.h"
