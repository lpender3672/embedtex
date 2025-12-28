#include "res/reg/builtin_font_reg.h"

using namespace tex;

DEF_FONT_SET(Builtin)

// Minimal embedded font set - only essential fonts for basic math rendering
REG_FONT(cmr10)      // Computer Modern Roman - main text
REG_FONT(cmmi10)     // Computer Modern Math Italic - variables  
REG_FONT(cmsy10)     // Computer Modern Symbol - operators
REG_FONT(cmex10)     // Computer Modern Extension - large symbols
REG_FONT(cmbx10)     // Computer Modern Bold Extended - bold text

END_DEF_FONT_SET
