#ifndef STATEX_TEST_REPORT_H
#define STATEX_TEST_REPORT_H

// Failure pictures.
//
// A number telling you two renders differ by dice=0.47 is not much help on its
// own. This writes the pair out as a stacked PNG so the difference is visible:
//
//     +--------------------------+
//     | reference (oracle)       |
//     +--------------------------+
//     | statex                   |
//     +--------------------------+
//     | diff                     |   red   = reference only
//     +--------------------------+   green = statex only
//                                    grey  = both
//
// Both panels are cropped to the *same* rectangle -- the union of the two ink
// bounding boxes -- so they stay registered with each other and a vertical
// scan down the image compares like with like.
//
// Captions are drawn with StaTeX's own glyph atlas rather than a bundled
// bitmap font: the atlas is already linked in and already has the Latin
// alphabet.

#include <string>

#include "stx_image.h"
#include "stx_png.h"

namespace stxtest {

/** Advance width `text` would occupy, without drawing it. */
int measureCaption(const std::string& text, int emPx);

/** Draw ASCII text with the StaTeX Roman atlas. Returns the advance width. */
int drawCaption(stximg::RgbImage& dst, int x, int baselineY,
                const std::string& text, int emPx, std::uint8_t r,
                std::uint8_t g, std::uint8_t b);

/**
 * Write the three-panel stack described above.
 *
 * @param reference  what it should look like (the oracle, or the golden)
 * @param actual     what StaTeX produced
 * @param title      the case id, drawn at the top
 * @param detail     one line of context, e.g. the similarity scores
 * @param path       output .png path
 */
bool writeFailureStack(const stximg::Image& reference,
                       const stximg::Image& actual, const std::string& title,
                       const std::string& detail, const std::string& path,
                       const std::string& referenceLabel = "reference (MicroTeX oracle)",
                       const std::string& actualLabel = "statex");

/** Sanitise a case id into something safe for a filename. */
std::string artifactName(const std::string& id, const std::string& suffix);

}  // namespace stxtest

#endif  // STATEX_TEST_REPORT_H
