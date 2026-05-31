#ifndef STATEX_PARSER_H
#define STATEX_PARSER_H

#include "statex_arena.h"
#include "statex_node.h"
#include "statex_types.h"

namespace statex {

/** Outcome of a parse. Every failure is a defined refusal (STX-ERR-02/03). */
enum class ParseError : u8 {
  Ok = 0,
  Empty,            // input had no atoms
  OutOfMemory,      // arena / node / operand / depth exhaustion (STX-MEM-03)
  TooDeep,          // nesting exceeded the work-stack (STX-EXE-03)
  UnbalancedBrace,  // unmatched { } or [ ]
  UnknownCommand,   // command not in the closed set (STX-LNG-02)
  MissingArg,       // script or command argument missing
  BadScript,        // misplaced ^ or _ (no base, or doubled)
  UnexpectedChar,   // e.g. a lone backslash
  InvalidMatrix,    // ragged rows, mismatched \begin/\end, or grid overflow
};

struct ParseResult {
  Handle root;       // NO_NODE on failure
  ParseError error;
  int errorPos;      // input index of the failure, or -1
};

/**
 * Parses a closed subset of math-mode TeX into an atom tree (STX-LNG-02).
 *
 * The parser is iterative: it drives an explicit frame work-stack allocated
 * from the arena (STX-EXE-01/02), so the C++ call stack depth is bounded
 * independent of input nesting (STX-EXE-03), and depth/operand/node exhaustion
 * all converge on a single refusal (STX-MEM-03). The input buffer is treated
 * as read-only (STX-LNG-03).
 *
 * Supported: characters, `^`/`_` scripts (incl. combined `x^2_i`), `{...}`
 * groups, `\frac{a}{b}` (and single-token `\frac a b`), `\sqrt{x}` /
 * `\sqrt[n]{x}`, and named symbols from the flash table (e.g. `\alpha`).
 */
class Parser {
 public:
  Parser(Arena& arena, NodeStore& store, u16 maxDepth = 64,
         u16 maxOperands = 256, u16 maxMatrixRows = 32, u16 maxMatrixCols = 32);

  bool ok() const { return _ok; }

  /** Parse `src[0..len)` (UTF-32). Does not reset the arena. */
  ParseResult parse(const c32* src, int len);

 private:
  struct Frame {
    u8 kind;          // FrameKind
    u8 scriptState;   // 0 none, 1 sup, 2 sub (containers only)
    u16 opBase;       // operand-stack base for container frames
    u8 need;          // remaining args (op frames)
    bool rule;        // frac rule
    Handle argA;      // frac numerator (op frame)
    Handle index;     // sqrt index (op frame)
    // Matrix-environment bookkeeping (kind == kMatrix):
    u16 cellBase;     // operand base of the current cell
    u16 cols;         // columns fixed by the first row (0 until set)
    u16 curCols;      // cells accumulated in the current row
    u16 rows;         // completed rows
    u8 env;           // MatrixEnv
    Face savedFace;   // face to restore when a kStyle frame closes
  };

  NodeStore& _store;
  bool _ok;
  Frame* _frames;
  u16 _maxDepth;
  u16 _depth;
  Handle* _operands;
  u16 _opCap;
  u16 _opTop;
  u16 _maxRows;
  u16 _maxCols;
  Face _curFace;
  ParseError _err;
  int _errPos;

  void fail(ParseError e, int pos);
  bool pushFrame(u8 kind, u8 need, bool rule, Handle index);
  void feedOperand(Handle h);
  void addToContainer(Frame& f, Handle h);
  Handle reduceRange(u16 base, u16 top);
  void onScript(u8 state, int pos);
  void onCloseBrace(int pos);
  void onCloseBracket(int pos);
  int onCommand(const c32* src, int len, int i);
  // Matrix support:
  bool endCell(Frame& f, int pos);   // reduce current cell onto the stack
  void onColumnSep(int pos);         // '&'
  void onRowSep(int pos);            // '\\'
  int onBegin(const c32* src, int len, int i);
  int onEnd(const c32* src, int len, int i);
  void onStyle(Face face);  // \mathrm \mathit \mathbf \mathbb
};

}  // namespace statex

#endif  // STATEX_PARSER_H
