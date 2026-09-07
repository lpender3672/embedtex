#ifndef STATEX_TEST_CORPUS_H
#define STATEX_TEST_CORPUS_H

// The shared formula corpus. One list, consumed by every suite that wants
// breadth: golden-image regression, the MicroTeX differential, the memory
// budget sweep, and the refusal audit.
//
// Keeping it in one place means "add a formula" is a one-line change that
// widens all of them at once, and that a formula's id is stable enough to
// name a golden file after.

#include <string>
#include <vector>

namespace stxtest {

/** What a case is for, so a suite can select the slice it cares about. */
enum class Feature : unsigned {
  Chars = 1u << 0,
  Scripts = 1u << 1,
  Fraction = 1u << 2,
  Radical = 1u << 3,
  Matrix = 1u << 4,
  Symbols = 1u << 5,
  Styles = 1u << 6,   // \mathrm \mathit \mathbf \mathbb
  Spacing = 1u << 7,  // cases whose point is inter-atom spacing
  Nesting = 1u << 8,
};

inline Feature operator|(Feature a, Feature b) {
  return static_cast<Feature>(static_cast<unsigned>(a) |
                              static_cast<unsigned>(b));
}
inline bool has(Feature set, Feature f) {
  return (static_cast<unsigned>(set) & static_cast<unsigned>(f)) != 0;
}

/** Whether a case is expected to render, and if not, why. */
enum class Expect {
  Renders,   // must produce a picture
  Refuses,   // must return a defined refusal
};

struct Case {
  const char* id;       // stable; names the golden file
  const char* tex;      // ASCII source, backslashes escaped as in C
  Feature features;
  Expect expect;        // what the requirements say should happen
  const char* note;     // why this case exists

  // Non-null when StaTeX does NOT currently do what `expect` says, naming the
  // defect suite that owns it. `expect` stays as the specified behaviour --
  // this field records that today's behaviour differs. Green suites skip these
  // cases and say so; the named red suite asserts them.
  const char* openDefect = nullptr;
};

/** True if this case behaves as specified today. */
inline bool isSettled(const Case& c) { return c.openDefect == nullptr; }

/** Every case. */
const std::vector<Case>& corpus();

/** The subset whose features intersect `f`. */
std::vector<Case> corpusWith(Feature f);

/** The subset expected to render. */
std::vector<Case> corpusRendering();

/** Look up by id, or nullptr. */
const Case* findCase(const std::string& id);

}  // namespace stxtest

#endif  // STATEX_TEST_CORPUS_H
