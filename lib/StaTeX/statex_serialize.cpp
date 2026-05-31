#include "statex_serialize.h"

namespace statex {
namespace {

constexpr int kMaxDepth = 64;

struct Writer {
  char* out;
  int cap;
  int len;
  bool overflow;

  void put(char c) {
    if (len < cap - 1) {
      out[len++] = c;
    } else {
      overflow = true;
    }
  }
  void puts(const char* s) {
    while (*s) put(*s++);
  }
  void putu(u32 v) {
    char tmp[10];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) {
      tmp[n++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    }
    while (n > 0) put(tmp[--n]);
  }
};

void dump(const NodeStore& s, Handle h, Writer& w, int depth) {
  if (depth > kMaxDepth) {
    w.overflow = true;
    return;
  }
  if (!valid(h)) {
    w.put('.');
    return;
  }
  const Node& n = s.get(h);
  switch (n.kind) {
    case Kind::Char:
      if (n.ch >= 0x20 && n.ch < 0x7F) {
        w.put(static_cast<char>(n.ch));
      } else {
        // Non-ASCII glyph (e.g. a symbol-table codepoint) -> `<U+XXXX>`.
        w.puts("<U+");
        const char* hex = "0123456789ABCDEF";
        bool started = false;
        for (int shift = 20; shift >= 0; shift -= 4) {
          const u8 nib = static_cast<u8>((n.ch >> shift) & 0xF);
          if (nib != 0 || started || shift <= 12) {  // min 4 digits
            w.put(hex[nib]);
            started = true;
          }
        }
        w.put('>');
      }
      break;
    case Kind::Row:
      w.put('[');
      for (u16 i = 0; i < n.children.count; ++i) {
        if (i) w.put(' ');
        dump(s, s.child(h, i), w, depth + 1);
      }
      w.put(']');
      break;
    case Kind::Frac:
      w.puts(n.frac.rule ? "(frac " : "(atop ");
      dump(s, n.frac.num, w, depth + 1);
      w.put(' ');
      dump(s, n.frac.den, w, depth + 1);
      w.put(')');
      break;
    case Kind::Script:
      w.puts("(scr ");
      dump(s, n.script.base, w, depth + 1);
      w.puts(" ^");
      dump(s, n.script.sup, w, depth + 1);
      w.puts(" _");
      dump(s, n.script.sub, w, depth + 1);
      w.put(')');
      break;
    case Kind::Sqrt:
      if (valid(n.sqrt.index)) {
        w.puts("(sqrt[");
        dump(s, n.sqrt.index, w, depth + 1);
        w.puts("] ");
      } else {
        w.puts("(sqrt ");
      }
      dump(s, n.sqrt.base, w, depth + 1);
      w.put(')');
      break;
    case Kind::Matrix: {
      switch (n.matrix.env) {
        case MatrixEnv::Bracket: w.puts("(bmat["); break;
        case MatrixEnv::Paren: w.puts("(pmat["); break;
        default: w.puts("(mat["); break;
      }
      w.putu(n.matrix.rows);
      w.put('x');
      w.putu(n.matrix.cols);
      w.puts("] ");
      const u16 total = static_cast<u16>(n.matrix.rows * n.matrix.cols);
      for (u16 k = 0; k < total; ++k) {
        if (k) w.put(' ');
        dump(s, s.cellAt(h, k), w, depth + 1);
      }
      w.put(')');
      break;
    }
    default:
      w.put('?');
      break;
  }
}

}  // namespace

int serialize(const NodeStore& store, Handle h, char* out, int cap) {
  if (cap <= 0) return -1;
  Writer w{out, cap, 0, false};
  dump(store, h, w, 0);
  if (w.len < cap) out[w.len] = '\0';
  return w.overflow ? -1 : w.len;
}

namespace {

void putGlyph(Writer& w, c32 ch) {
  if (ch >= 0x20 && ch < 0x7F) {
    w.put(static_cast<char>(ch));
    return;
  }
  w.puts("<U+");
  const char* hex = "0123456789ABCDEF";
  bool started = false;
  for (int shift = 20; shift >= 0; shift -= 4) {
    const u8 nib = static_cast<u8>((ch >> shift) & 0xF);
    if (nib != 0 || started || shift <= 12) {
      w.put(hex[nib]);
      started = true;
    }
  }
  w.put('>');
}

void dumpBox(const BoxStore& s, Handle h, Writer& w, int depth) {
  if (depth > kMaxDepth) {
    w.overflow = true;
    return;
  }
  if (!valid(h)) {
    w.put('.');
    return;
  }
  const Box& b = s.get(h);
  switch (b.kind) {
    case BoxKind::Char:
      putGlyph(w, b.ch);
      break;
    case BoxKind::Rule:
      w.put('R');
      break;
    case BoxKind::HList:
    case BoxKind::VList: {
      w.put('(');
      w.put(b.kind == BoxKind::HList ? 'H' : 'V');
      for (u16 i = 0; i < b.children.count; ++i) {
        w.put(' ');
        dumpBox(s, s.child(h, i), w, depth + 1);
      }
      w.put(')');
      break;
    }
    default:
      w.put('?');
      break;
  }
}

}  // namespace

int serializeBox(const BoxStore& store, Handle h, char* out, int cap) {
  if (cap <= 0) return -1;
  Writer w{out, cap, 0, false};
  dumpBox(store, h, w, 0);
  if (w.len < cap) out[w.len] = '\0';
  return w.overflow ? -1 : w.len;
}

}  // namespace statex
