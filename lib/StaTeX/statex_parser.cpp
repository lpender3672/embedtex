#include "statex_parser.h"

#include "statex_symbols.h"

namespace statex {
namespace {

enum FrameKind : u8 {
  kRoot = 0, kBrace, kBracket, kFrac, kSqrt, kMatrix, kStyle
};
enum ScriptState : u8 { kNone = 0, kSup, kSub };

// Compare src[start..start+len) to a NUL-terminated ASCII literal.
inline bool nameEq(const c32* src, int start, int len, const char* lit) {
  int k = 0;
  for (; k < len; ++k) {
    if (lit[k] == '\0' || src[start + k] != static_cast<c32>(lit[k])) {
      return false;
    }
  }
  return lit[k] == '\0';
}

inline bool isSpace(c32 c) {
  return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r';
}
inline bool isAlpha(c32 c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

inline AtomType classify(c32 c) {
  switch (c) {
    case U'+':
    case U'-':
    case U'*':
      return AtomType::BinaryOp;
    case U'=':
    case U'<':
    case U'>':
      return AtomType::Relation;
    case U'(':
    case U'[':
      return AtomType::Opening;
    case U')':
    case U']':
      return AtomType::Closing;
    case U',':
    case U';':
      return AtomType::Punctuation;
    default:
      return AtomType::Ordinary;
  }
}

}  // namespace

Parser::Parser(Arena& arena, NodeStore& store, u16 maxDepth, u16 maxOperands,
               u16 maxMatrixRows, u16 maxMatrixCols)
    : _store(store),
      _ok(false),
      _frames(nullptr),
      _maxDepth(maxDepth),
      _depth(0),
      _operands(nullptr),
      _opCap(maxOperands),
      _opTop(0),
      _maxRows(maxMatrixRows),
      _maxCols(maxMatrixCols),
      _curFace(Face::Roman),
      _err(ParseError::Ok),
      _errPos(-1) {
  _frames = arena.allocArray<Frame>(maxDepth);
  _operands = arena.allocArray<Handle>(maxOperands);
  _ok = (_frames != nullptr) && (_operands != nullptr);
}

void Parser::fail(ParseError e, int pos) {
  if (_err == ParseError::Ok) {  // keep first error
    _err = e;
    _errPos = pos;
  }
}

bool Parser::pushFrame(u8 kind, u8 need, bool rule, Handle index) {
  if (_depth >= _maxDepth) {
    fail(ParseError::TooDeep, -1);
    return false;
  }
  Frame& f = _frames[_depth++];
  f.kind = kind;
  f.scriptState = kNone;
  f.opBase = _opTop;
  f.need = need;
  f.rule = rule;
  f.argA = NO_NODE;
  f.index = index;
  f.cellBase = _opTop;
  f.cols = 0;
  f.curCols = 0;
  f.rows = 0;
  f.env = 0;
  return true;
}

void Parser::addToContainer(Frame& f, Handle h) {
  if (f.scriptState == kNone) {
    if (_opTop >= _opCap) {
      fail(ParseError::OutOfMemory, -1);
      return;
    }
    _operands[_opTop++] = h;
    return;
  }
  // Attach h as a script of the preceding operand.
  Handle base = _operands[_opTop - 1];
  Node& bn = _store.get(base);
  if (bn.kind == Kind::Script) {
    if (f.scriptState == kSup) {
      if (valid(bn.script.sup)) {
        fail(ParseError::BadScript, -1);
        return;
      }
      bn.script.sup = h;
    } else {
      if (valid(bn.script.sub)) {
        fail(ParseError::BadScript, -1);
        return;
      }
      bn.script.sub = h;
    }
  } else {
    _opTop--;  // consume base
    const Handle sup = (f.scriptState == kSup) ? h : NO_NODE;
    const Handle sub = (f.scriptState == kSub) ? h : NO_NODE;
    const Handle sc = _store.makeScript(base, sup, sub);
    if (!valid(sc)) {
      fail(ParseError::OutOfMemory, -1);
      return;
    }
    _operands[_opTop++] = sc;
  }
  f.scriptState = kNone;
}

void Parser::feedOperand(Handle h) {
  while (_err == ParseError::Ok) {
    Frame& f = _frames[_depth - 1];
    if (f.kind == kRoot || f.kind == kBrace || f.kind == kBracket ||
        f.kind == kMatrix) {
      addToContainer(f, h);
      return;
    }
    // Op frame: consume h as an argument.
    Handle result;
    if (f.kind == kStyle) {
      // Face already applied to the argument's chars during parsing; this frame
      // just restores the previous face and passes the argument through.
      _curFace = f.savedFace;
      result = h;
    } else if (f.kind == kFrac) {
      if (f.need == 2) {
        f.argA = h;
        f.need = 1;
        return;
      }
      result = _store.makeFrac(f.argA, h, f.rule);
    } else {  // kSqrt
      result = _store.makeSqrt(h, f.index);
    }
    _depth--;  // pop op frame
    if (!valid(result)) {
      fail(ParseError::OutOfMemory, -1);
      return;
    }
    h = result;  // hand to parent (loop, not recurse — STX-EXE-01)
  }
}

Handle Parser::reduceRange(u16 base, u16 top) {
  const u16 count = static_cast<u16>(top - base);
  Handle res;
  if (count == 0) {
    res = _store.makeRow(nullptr, 0);
  } else if (count == 1) {
    res = _operands[base];
  } else {
    res = _store.makeRow(&_operands[base], count);
  }
  _opTop = base;
  return res;
}

void Parser::onScript(u8 state, int pos) {
  Frame& f = _frames[_depth - 1];
  if (f.kind == kFrac || f.kind == kSqrt) {
    fail(ParseError::BadScript, pos);  // script in argument position
    return;
  }
  if (f.scriptState != kNone) {
    fail(ParseError::MissingArg, pos);  // e.g. x^_ or x^^
    return;
  }
  const u16 base = (f.kind == kMatrix) ? f.cellBase : f.opBase;
  if (_opTop <= base) {
    fail(ParseError::BadScript, pos);  // no base atom
    return;
  }
  f.scriptState = state;
}

void Parser::onCloseBrace(int pos) {
  Frame& f = _frames[_depth - 1];
  if (f.kind != kBrace) {
    fail(ParseError::UnbalancedBrace, pos);
    return;
  }
  if (f.scriptState != kNone) {
    fail(ParseError::MissingArg, pos);
    return;
  }
  const Handle res = reduceRange(f.opBase, _opTop);
  _depth--;
  if (!valid(res)) {
    fail(ParseError::OutOfMemory, pos);
    return;
  }
  feedOperand(res);
}

void Parser::onCloseBracket(int pos) {
  Frame& f = _frames[_depth - 1];
  // Only reached when top is a Bracket frame (checked by caller).
  const Handle res = reduceRange(f.opBase, _opTop);
  _depth--;
  // Parent must be the Sqrt frame that opened it; fill its index slot.
  Frame& parent = _frames[_depth - 1];
  if (parent.kind != kSqrt) {
    fail(ParseError::UnbalancedBrace, pos);
    return;
  }
  if (!valid(res)) {
    fail(ParseError::OutOfMemory, pos);
    return;
  }
  parent.index = res;
}

int Parser::onCommand(const c32* src, int len, int i) {
  const int start = i;
  while (i < len && isAlpha(src[i])) ++i;
  const int n = i - start;
  if (n == 0) {
    fail(ParseError::UnexpectedChar, start);  // lone backslash
    return i;
  }
  // \frac and \sqrt are structural; everything else is a named symbol.
  auto nameIs = [&](const char* lit) -> bool {
    int k = 0;
    for (; k < n; ++k) {
      if (lit[k] == '\0' || src[start + k] != static_cast<c32>(lit[k])) {
        return false;
      }
    }
    return lit[k] == '\0';
  };

  if (nameIs("frac")) {
    pushFrame(kFrac, /*need=*/2, /*rule=*/true, NO_NODE);
    return i;
  }
  if (nameIs("sqrt")) {
    pushFrame(kSqrt, /*need=*/1, /*rule=*/false, NO_NODE);
    return i;
  }
  if (nameIs("begin")) return onBegin(src, len, i);
  if (nameIs("end")) return onEnd(src, len, i);
  if (nameIs("mathrm")) { onStyle(Face::Roman); return i; }
  if (nameIs("mathit")) { onStyle(Face::Italic); return i; }
  if (nameIs("mathbf")) { onStyle(Face::Bold); return i; }
  if (nameIs("mathbb")) { onStyle(Face::Blackboard); return i; }

  // Named symbol: build an ASCII key and look it up in the flash table.
  char key[32];
  if (n >= static_cast<int>(sizeof(key))) {
    fail(ParseError::UnknownCommand, start);
    return i;
  }
  for (int k = 0; k < n; ++k) key[k] = static_cast<char>(src[start + k]);
  const SymbolEntry* e = findSymbol(key, n);
  if (e == nullptr) {
    fail(ParseError::UnknownCommand, start);
    return i;
  }
  const Handle h = _store.makeChar(e->glyph, e->type, Face::Symbol);
  if (!valid(h)) {
    fail(ParseError::OutOfMemory, start);
    return i;
  }
  feedOperand(h);
  return i;
}

void Parser::onStyle(Face face) {
  if (!pushFrame(kStyle, /*need=*/1, /*rule=*/false, NO_NODE)) return;
  _frames[_depth - 1].savedFace = _curFace;
  _curFace = face;
}

bool Parser::endCell(Frame& f, int pos) {
  const Handle cell = reduceRange(f.cellBase, _opTop);  // sets _opTop = cellBase
  if (!valid(cell)) {
    fail(ParseError::OutOfMemory, pos);
    return false;
  }
  if (_opTop >= _opCap) {
    fail(ParseError::OutOfMemory, pos);
    return false;
  }
  _operands[_opTop++] = cell;
  return true;
}

void Parser::onColumnSep(int pos) {
  Frame& f = _frames[_depth - 1];
  if (f.kind != kMatrix) {
    fail(ParseError::InvalidMatrix, pos);
    return;
  }
  if (f.scriptState != kNone) {
    fail(ParseError::MissingArg, pos);
    return;
  }
  if (!endCell(f, pos)) return;
  f.curCols++;
  if (f.curCols > _maxCols) {
    fail(ParseError::InvalidMatrix, pos);
    return;
  }
  f.cellBase = _opTop;
}

void Parser::onRowSep(int pos) {
  Frame& f = _frames[_depth - 1];
  if (f.kind != kMatrix) {
    fail(ParseError::InvalidMatrix, pos);
    return;
  }
  if (f.scriptState != kNone) {
    fail(ParseError::MissingArg, pos);
    return;
  }
  if (!endCell(f, pos)) return;
  f.curCols++;
  if (f.rows == 0) {
    f.cols = f.curCols;
  } else if (f.curCols != f.cols) {
    fail(ParseError::InvalidMatrix, pos);  // ragged row
    return;
  }
  f.rows++;
  if (f.rows > _maxRows || f.cols > _maxCols) {
    fail(ParseError::InvalidMatrix, pos);
    return;
  }
  f.curCols = 0;
  f.cellBase = _opTop;
}

int Parser::onBegin(const c32* src, int len, int i) {
  if (i >= len || src[i] != U'{') {
    fail(ParseError::InvalidMatrix, i);
    return i;
  }
  ++i;
  const int ns = i;
  while (i < len && src[i] != U'}') ++i;
  if (i >= len) {
    fail(ParseError::InvalidMatrix, ns);
    return i;
  }
  const int nl = i - ns;
  ++i;  // skip '}'
  u8 env;
  if (nameEq(src, ns, nl, "matrix")) {
    env = 0;
  } else if (nameEq(src, ns, nl, "bmatrix")) {
    env = 1;
  } else if (nameEq(src, ns, nl, "pmatrix")) {
    env = 2;
  } else {
    fail(ParseError::UnknownCommand, ns);
    return i;
  }
  if (!pushFrame(kMatrix, 0, false, NO_NODE)) return i;
  _frames[_depth - 1].env = env;
  return i;
}

int Parser::onEnd(const c32* src, int len, int i) {
  if (i >= len || src[i] != U'{') {
    fail(ParseError::InvalidMatrix, i);
    return i;
  }
  ++i;
  const int ns = i;
  while (i < len && src[i] != U'}') ++i;
  if (i >= len) {
    fail(ParseError::InvalidMatrix, ns);
    return i;
  }
  const int nl = i - ns;
  ++i;  // skip '}'
  Frame& f = _frames[_depth - 1];
  if (f.kind != kMatrix) {
    fail(ParseError::InvalidMatrix, ns);
    return i;
  }
  const char* lit = f.env == 1 ? "bmatrix" : (f.env == 2 ? "pmatrix" : "matrix");
  if (!nameEq(src, ns, nl, lit)) {
    fail(ParseError::InvalidMatrix, ns);  // \begin/\end mismatch
    return i;
  }
  if (f.scriptState != kNone) {
    fail(ParseError::MissingArg, i);
    return i;
  }
  // Finalize a pending (non-trailing) row.
  if (_opTop > f.cellBase || f.curCols > 0) {
    if (!endCell(f, i)) return i;
    f.curCols++;
    if (f.rows == 0) {
      f.cols = f.curCols;
    } else if (f.curCols != f.cols) {
      fail(ParseError::InvalidMatrix, i);
      return i;
    }
    f.rows++;
    if (f.rows > _maxRows || f.cols > _maxCols) {
      fail(ParseError::InvalidMatrix, i);
      return i;
    }
  }
  const u16 count = static_cast<u16>(_opTop - f.opBase);
  if (count != static_cast<u16>(f.rows * f.cols)) {
    fail(ParseError::InvalidMatrix, i);
    return i;
  }
  const Handle m = _store.makeMatrix(f.rows, f.cols,
                                     static_cast<MatrixEnv>(f.env),
                                     &_operands[f.opBase], count);
  _opTop = f.opBase;
  _depth--;
  if (!valid(m)) {
    fail(ParseError::OutOfMemory, i);
    return i;
  }
  feedOperand(m);
  return i;
}

ParseResult Parser::parse(const c32* src, int len) {
  _err = ParseError::Ok;
  _errPos = -1;
  _depth = 0;
  _opTop = 0;
  _curFace = Face::Roman;
  if (!_ok) return {NO_NODE, ParseError::OutOfMemory, 0};

  pushFrame(kRoot, 0, false, NO_NODE);

  int i = 0;
  while (i < len && _err == ParseError::Ok) {
    const c32 c = src[i];
    if (isSpace(c)) {
      ++i;
      continue;
    }
    if (c == U'{') {
      ++i;
      pushFrame(kBrace, 0, false, NO_NODE);
      continue;
    }
    if (c == U'}') {
      ++i;
      onCloseBrace(i);
      continue;
    }
    const Frame& top = _frames[_depth - 1];
    if (c == U'[' && top.kind == kSqrt && top.need == 1 &&
        !valid(top.index)) {
      ++i;
      pushFrame(kBracket, 0, false, NO_NODE);
      continue;
    }
    if (c == U']' && top.kind == kBracket) {
      ++i;
      onCloseBracket(i);
      continue;
    }
    if (c == U'^' || c == U'_') {
      ++i;
      onScript(c == U'^' ? kSup : kSub, i);
      continue;
    }
    if (c == U'&') {
      ++i;
      onColumnSep(i);
      continue;
    }
    if (c == U'\\') {
      if (i + 1 < len && src[i + 1] == U'\\') {  // row separator
        i += 2;
        onRowSep(i);
        continue;
      }
      ++i;
      i = onCommand(src, len, i);
      continue;
    }
    // Ordinary character.
    ++i;
    const Handle h = _store.makeChar(c, classify(c), _curFace);
    if (!valid(h)) {
      fail(ParseError::OutOfMemory, i);
      break;
    }
    feedOperand(h);
  }

  if (_err != ParseError::Ok) return {NO_NODE, _err, _errPos};

  // End of input: only the Root frame may remain open.
  if (_depth != 1) {
    const u8 k = _frames[_depth - 1].kind;
    ParseError e;
    if (k == kMatrix) {
      e = ParseError::InvalidMatrix;  // unclosed environment
    } else if (k == kBrace || k == kBracket) {
      e = ParseError::UnbalancedBrace;
    } else {
      e = ParseError::MissingArg;
    }
    return {NO_NODE, e, len};
  }
  Frame& root = _frames[0];
  if (root.scriptState != kNone) return {NO_NODE, ParseError::MissingArg, len};
  const u16 count = static_cast<u16>(_opTop - root.opBase);
  if (count == 0) return {NO_NODE, ParseError::Empty, 0};
  const Handle r = reduceRange(root.opBase, _opTop);
  if (!valid(r)) return {NO_NODE, ParseError::OutOfMemory, len};
  return {r, ParseError::Ok, -1};
}

}  // namespace statex
