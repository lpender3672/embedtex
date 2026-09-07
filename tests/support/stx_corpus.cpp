#include "stx_corpus.h"

namespace stxtest {
namespace {

using F = Feature;
using E = Expect;

const std::vector<Case> kCases = {
    // --- plain characters ---------------------------------------------------
    {"char_single", "x", F::Chars, E::Renders, "smallest possible render"},
    {"char_word", "abc", F::Chars, E::Renders, "row of three glyphs"},
    {"char_digits", "2024", F::Chars, E::Renders, "digit advances"},
    {"char_mixed_case", "aXbY", F::Chars, E::Renders, "upper/lower metrics"},
    {"char_long_row", "abcdefghij", F::Chars, E::Renders,
     "ten advances in a row: quantisation error accumulates here if anywhere"},
    {"group_redundant", "{{a}}", F::Nesting, E::Renders,
     "nested groups must collapse to the same box as a bare atom"},
    {"group_in_row", "a{bc}d", F::Chars | F::Nesting, E::Renders,
     "a group inside a row is an Ord atom, not a transparent splice"},

    // --- inter-atom spacing -------------------------------------------------
    // These exist to pin TeX's spacing classes. AtomType is parsed today but
    // never consulted by layout, so the binary/relation cases are the ones
    // that expose it.
    {"space_binary", "a+b", F::Chars | F::Spacing, E::Renders,
     "medium space either side of a binary operator"},
    {"space_relation", "a=b", F::Chars | F::Spacing, E::Renders,
     "thick space either side of a relation"},
    {"space_punct", "a,b", F::Chars | F::Spacing, E::Renders,
     "thin space after punctuation"},
    {"space_ord", "ab", F::Chars | F::Spacing, E::Renders,
     "no space between ordinaries -- the control case"},
    {"space_chain", "a+b=c", F::Chars | F::Spacing, E::Renders,
     "binary and relation in one row"},

    // BIN->ORD demotion (TeXbook p.170). A binary operator with no operand on
    // one side is a sign, and takes no space. Nothing exercised this against
    // the oracle until these were added, and they immediately found that
    // StaTeX draws ASCII hyphen where TeX draws U+2212 MINUS.
    {"space_unary_lead", "-x", F::Chars | F::Spacing, E::Renders,
     "leading operator is a sign, not a subtraction"},
    {"space_unary_after_rel", "x=-y", F::Chars | F::Spacing, E::Renders,
     "operator after a relation is demoted"},
    {"space_unary_chain", "-x+y", F::Chars | F::Spacing, E::Renders,
     "a demoted operator must not demote the next one"},
    {"space_unary_after_open", "(-x)", F::Chars | F::Spacing, E::Renders,
     "operator after an opening delimiter is demoted"},

    // Opening/Closing/Punctuation columns of the spacing table, which the
    // binary/relation cases above never reach.
    {"space_delim_paren", "(a+b)", F::Chars | F::Spacing, E::Renders,
     "delimiters take no space against their contents"},
    {"space_delim_bracket", "[a,b]", F::Chars | F::Spacing, E::Renders,
     "punctuation inside delimiters"},
    {"space_delim_nested", "((a))", F::Chars | F::Spacing | F::Nesting,
     E::Renders, "adjacent openings and closings"},
    {"space_relation_lt", "a<b", F::Chars | F::Spacing, E::Renders,
     "'<' is a relation like '='"},
    {"space_semicolon", "a;b", F::Chars | F::Spacing, E::Renders,
     "';' is punctuation like ','"},
    {"space_star", "a*b", F::Chars | F::Spacing, E::Renders,
     "'*' is TeX's \\ast (U+2217), not the ASCII asterisk"},

    // --- scripts ------------------------------------------------------------
    {"script_sup", "x^2", F::Scripts, E::Renders, "superscript only"},
    {"script_sub", "x_i", F::Scripts, E::Renders, "subscript only"},
    {"script_both", "x^2_i", F::Scripts, E::Renders, "both, stacked"},
    {"script_both_rev", "x_i^2", F::Scripts, E::Renders, "order independence"},
    {"script_group", "x^{ab}", F::Scripts, E::Renders, "grouped script"},
    {"script_nested", "x^{y^{z}}", F::Scripts | F::Nesting, E::Renders,
     "script size floor kicks in"},
    {"script_deep", "x^{y^{z^{w}}}", F::Scripts | F::Nesting, E::Renders,
     "three levels: scriptscript and below"},
    {"script_frac", "x^{\\frac{a}{b}}", F::Scripts | F::Fraction, E::Renders,
     "a fraction in a script picks different num/denom parameters"},
    {"script_on_close", "(a)^2", F::Scripts | F::Spacing, E::Renders,
     "a scripted group keeps its base's spacing class"},
    // A scripted big operator is still a big operator: it must space like one
    // and, in display style, set its limits above and below rather than beside.
    {"script_on_bigop", "\\sum_{i}^{n}", F::Scripts | F::Symbols, E::Renders,
     "limits on a big operator"},
    {"script_on_int", "\\int_a^b", F::Scripts | F::Symbols, E::Renders,
     "integrals take side-set limits even in display"},
    {"script_bigop_in_row", "a+\\sum_{i}b", F::Scripts | F::Symbols | F::Spacing,
     E::Renders, "a scripted operator inside a spaced row"},

    // --- fractions ----------------------------------------------------------
    {"frac_simple", "\\frac{a}{b}", F::Fraction, E::Renders, "bar and stack"},
    {"frac_tokens", "\\frac a b", F::Fraction, E::Renders,
     "single-token argument form"},
    {"frac_wide_num", "\\frac{abc}{d}", F::Fraction, E::Renders,
     "numerator wider than denominator"},
    {"frac_nested", "\\frac{\\frac{a}{b}}{c}", F::Fraction | F::Nesting,
     E::Renders, "fraction inside a fraction"},
    {"frac_with_script", "\\frac{x^2+1}{2}", F::Fraction | F::Scripts,
     E::Renders, "the numerator from src/main.cpp"},
    {"frac_deep", "\\frac{\\frac{a}{b}}{\\frac{c}{d}}",
     F::Fraction | F::Nesting, E::Renders, "fractions on both sides of the bar"},
    {"frac_sum_over_sum", "\\frac{a+b}{c+d}", F::Fraction | F::Spacing,
     E::Renders, "inter-atom spacing inside both fraction arms"},

    // --- radicals -----------------------------------------------------------
    {"sqrt_simple", "\\sqrt{x}", F::Radical, E::Renders, "radical + vinculum"},
    {"sqrt_index", "\\sqrt[3]{x}", F::Radical, E::Renders, "explicit index"},
    {"sqrt_wide", "\\sqrt{abc}", F::Radical, E::Renders,
     "vinculum must span the radicand"},
    {"sqrt_tall", "\\sqrt{\\frac{a}{b}}", F::Radical | F::Fraction, E::Renders,
     "radical grows to the radicand height"},
    {"sqrt_nested", "\\sqrt{\\sqrt{x}}", F::Radical | F::Nesting, E::Renders,
     "each radical must pick a taller variant than the one inside it"},
    {"sqrt_index_wide", "\\sqrt[10]{x}", F::Radical, E::Renders,
     "a two-digit index must not overlap the surd"},
    {"sqrt_in_frac", "\\frac{\\sqrt{a}}{b}", F::Radical | F::Fraction,
     E::Renders, "a radical set in script style"},

    // --- symbols ------------------------------------------------------------
    {"sym_greek", "\\alpha\\beta\\gamma", F::Symbols, E::Renders,
     "greek from the closed set"},
    {"sym_relation", "\\alpha\\leq\\infty", F::Symbols | F::Spacing, E::Renders,
     "named relation spacing"},
    {"sym_bigop", "\\sum", F::Symbols, E::Renders, "big operator"},
    {"sym_integral", "\\int", F::Symbols, E::Renders, "tall glyph metrics"},
    {"sym_binary", "a\\cdot b", F::Symbols | F::Spacing, E::Renders,
     "named binary operator spacing"},
    {"sym_greek_more", "\\pi\\theta\\phi", F::Symbols, E::Renders,
     "the rest of the closed greek set"},
    {"sym_times", "a\\times b", F::Symbols | F::Spacing, E::Renders,
     "\\times spaces as a binary operator"},
    {"sym_geq", "a\\geq b", F::Symbols | F::Spacing, E::Renders,
     "\\geq spaces as a relation"},
    {"sym_bigop_row", "\\sum a+b", F::Symbols | F::Spacing, E::Renders,
     "an unscripted big operator in a row: Op-Ord takes a thin space"},

    // --- styles -------------------------------------------------------------
    {"style_bold", "\\mathbf{A}", F::Styles, E::Renders, "bold face"},
    {"style_italic", "\\mathit{x}", F::Styles, E::Renders, "italic face"},
    {"style_roman", "\\mathrm{d}", F::Styles, E::Renders, "upright face"},
    {"style_bb", "\\mathbb{R}", F::Styles, E::Renders, "blackboard face"},
    {"style_group", "\\mathbf{abc}", F::Styles, E::Renders,
     "face applies to a whole group"},
    {"style_nested", "\\mathbf{a\\mathit{b}}", F::Styles | F::Nesting,
     E::Renders, "an inner face overrides and then restores"},
    {"style_bb_all", "\\mathbb{RCNZQ}", F::Styles, E::Renders,
     "every glyph the blackboard face carries"},
    {"style_rm_digits", "\\mathrm{123}", F::Styles, E::Renders,
     "digits are already upright; \\mathrm must not change them"},
    {"style_sf", "\\mathsf{x}", F::Styles, E::Renders,
     "STX-FNT-05 requires \\mathsf; the parser has no case for it",
     "MATHSF-MISSING"},

    // --- matrices -----------------------------------------------------------
    {"matrix_1x1", "\\begin{matrix}a\\end{matrix}", F::Matrix, E::Renders,
     "degenerate grid"},
    {"matrix_2x2", "\\begin{matrix}a&b\\\\c&d\\end{matrix}", F::Matrix,
     E::Renders, "the basic grid"},
    {"matrix_bracket", "\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}", F::Matrix,
     E::Renders, "scaled square delimiters"},
    {"matrix_paren", "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}", F::Matrix,
     E::Renders, "scaled round delimiters"},
    {"matrix_3x1", "\\begin{bmatrix}a\\\\b\\\\c\\end{bmatrix}", F::Matrix,
     E::Renders, "column vector"},
    {"matrix_1x3", "\\begin{bmatrix}a&b&c\\end{bmatrix}", F::Matrix, E::Renders,
     "row vector"},
    {"matrix_3x3", "\\begin{matrix}a&b&c\\\\d&e&f\\\\g&h&i\\end{matrix}",
     F::Matrix, E::Renders, "column gaps accumulate across three columns"},
    {"matrix_frac_cell", "\\begin{bmatrix}\\frac{a}{b}&c\\end{bmatrix}",
     F::Matrix | F::Fraction, E::Renders,
     "a tall cell must set the row height and the delimiter size"},
    {"matrix_script_cell", "\\begin{pmatrix}x^2&y_i\\end{pmatrix}",
     F::Matrix | F::Scripts, E::Renders,
     "cells carry their own height and depth"},
    {"matrix_empty", "\\begin{matrix}\\end{matrix}", F::Matrix, E::Refuses,
     "an environment with no cells is not a matrix"},
    {"matrix_ragged", "\\begin{matrix}a&b\\\\c\\end{matrix}", F::Matrix,
     E::Refuses, "ragged rows must refuse"},
    {"matrix_mismatch", "\\begin{matrix}a\\end{bmatrix}", F::Matrix, E::Refuses,
     "begin/end mismatch must refuse"},
    {"matrix_unclosed", "\\begin{matrix}a", F::Matrix, E::Refuses,
     "unclosed environment must refuse"},

    // --- the showcase -------------------------------------------------------
    {"showcase",
     "\\mathbf{A}=\\begin{bmatrix}\\frac{x^2+1}{2} & \\sqrt{\\omega} \\\\"
     "\\alpha^2_i & \\sum\\leq\\infty\\end{bmatrix}",
     F::Matrix | F::Fraction | F::Radical | F::Scripts | F::Symbols | F::Styles,
     E::Renders, "the formula src/main.cpp draws on the panel"},

    // --- refusals -----------------------------------------------------------
    {"bad_unknown_cmd", "\\nosuchcommand", F::Chars, E::Refuses,
     "closed command set (STX-LNG-02)"},
    {"bad_unbalanced_open", "{a", F::Nesting, E::Refuses, "unclosed group"},
    {"bad_unbalanced_close", "a}", F::Nesting, E::Refuses, "stray close"},
    {"bad_lone_backslash", "a\\", F::Chars, E::Refuses, "trailing backslash"},
    {"bad_script_no_base", "^2", F::Scripts, E::Refuses, "script with no base"},
    {"bad_double_script", "x^2^3", F::Scripts, E::Refuses,
     "two superscripts on one base"},
    {"bad_empty", "", F::Chars, E::Refuses, "empty input"},
    {"bad_frac_missing_arg", "\\frac{a}", F::Fraction, E::Refuses,
     "fraction missing its denominator"},

    // --- glyph coverage -----------------------------------------------------
    // Characters and (face, glyph) pairs the atlas does not carry. STX-FNT-05
    // says an unavailable pair refuses; these are the cases that check it.
    {"glyph_absent_question", "?", F::Chars, E::Refuses,
     "'?' is not in the Roman atlas"},
    {"glyph_absent_percent", "%", F::Chars, E::Refuses,
     "'%' is not in the Roman atlas"},
    {"glyph_absent_italic_digit", "\\mathit{5}", F::Styles, E::Refuses,
     "the italic face carries no digits"},
    {"glyph_absent_bb_letter", "\\mathbb{A}", F::Styles, E::Refuses,
     "blackboard carries only R C N Z Q"},
    {"glyph_absent_mixed", "a?b", F::Chars, E::Refuses,
     "one absent glyph in a row of present ones"},
};

}  // namespace

const std::vector<Case>& corpus() { return kCases; }

std::vector<Case> corpusWith(Feature f) {
  std::vector<Case> out;
  for (const Case& c : kCases) {
    if (has(c.features, f)) out.push_back(c);
  }
  return out;
}

std::vector<Case> corpusRendering() {
  std::vector<Case> out;
  for (const Case& c : kCases) {
    if (c.expect == Expect::Renders) out.push_back(c);
  }
  return out;
}

const Case* findCase(const std::string& id) {
  for (const Case& c : kCases) {
    if (id == c.id) return &c;
  }
  return nullptr;
}

}  // namespace stxtest
