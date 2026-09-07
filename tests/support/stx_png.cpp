#include "stx_png.h"

#include <cstdio>
#include <cstring>

namespace stximg {
namespace {

// --- CRC32 / big-endian helpers --------------------------------------------

std::uint32_t crcTableEntry(int n) {
  std::uint32_t c = static_cast<std::uint32_t>(n);
  for (int k = 0; k < 8; ++k) {
    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
  }
  return c;
}

std::uint32_t crc32(const std::uint8_t* data, size_t len) {
  static std::uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (int i = 0; i < 256; ++i) table[i] = crcTableEntry(i);
    built = true;
  }
  std::uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFu;
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void chunk(std::vector<std::uint8_t>& out, const char tag[4],
           const std::vector<std::uint8_t>& body) {
  be32(out, static_cast<std::uint32_t>(body.size()));
  const size_t start = out.size();
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(tag[i]));
  out.insert(out.end(), body.begin(), body.end());
  be32(out, crc32(out.data() + start, out.size() - start));
}

// --- deflate, fixed-Huffman with greedy LZ77 --------------------------------
//
// Stored blocks would be simpler but produce ~500 KB per diagnostic image,
// and these are regenerated on every ctest run. Fixed Huffman plus a greedy
// matcher gets them to a few KB without pulling zlib into the test tree: the
// content is mostly flat background, which after the PNG Up filter is long
// runs of zero bytes -- exactly what LZ77 is good at.

struct BitWriter {
  std::vector<std::uint8_t> out;
  std::uint32_t bits = 0;
  int count = 0;

  /** Deflate packs bits into bytes starting at the least-significant bit. */
  void put(std::uint32_t value, int n) {
    bits |= (value & ((1u << n) - 1u)) << count;
    count += n;
    while (count >= 8) {
      out.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
      bits >>= 8;
      count -= 8;
    }
  }
  /** Huffman codes are defined MSB-first, so they go out reversed. */
  void putCode(std::uint32_t code, int n) {
    std::uint32_t r = 0;
    for (int i = 0; i < n; ++i) r |= ((code >> i) & 1u) << (n - 1 - i);
    put(r, n);
  }
  void flush() {
    if (count > 0) {
      out.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
      bits = 0;
      count = 0;
    }
  }
};

/** Emit a literal/length symbol using the fixed Huffman table (RFC 1951 §3.2.6). */
void putLitLen(BitWriter& w, int sym) {
  if (sym <= 143) {
    w.putCode(static_cast<std::uint32_t>(0x30 + sym), 8);
  } else if (sym <= 255) {
    w.putCode(static_cast<std::uint32_t>(0x190 + sym - 144), 9);
  } else if (sym <= 279) {
    w.putCode(static_cast<std::uint32_t>(sym - 256), 7);
  } else {
    w.putCode(static_cast<std::uint32_t>(0xC0 + sym - 280), 8);
  }
}

const int kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                          15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                          67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,    13,
                           17,   25,   33,   49,   65,   97,    129,  193,
                           257,  385,  513,  769,  1025, 1537,  2049, 3073,
                           4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putLength(BitWriter& w, int len) {
  int c = 28;
  while (c > 0 && kLenBase[c] > len) --c;
  putLitLen(w, 257 + c);
  if (kLenExtra[c] > 0) {
    w.put(static_cast<std::uint32_t>(len - kLenBase[c]), kLenExtra[c]);
  }
}

void putDistance(BitWriter& w, int dist) {
  int c = 29;
  while (c > 0 && kDistBase[c] > dist) --c;
  w.putCode(static_cast<std::uint32_t>(c), 5);  // fixed: 5-bit codes 0..29
  if (kDistExtra[c] > 0) {
    w.put(static_cast<std::uint32_t>(dist - kDistBase[c]), kDistExtra[c]);
  }
}

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;

std::uint32_t hash3(const std::uint8_t* p) {
  return ((static_cast<std::uint32_t>(p[0]) << 16) ^
          (static_cast<std::uint32_t>(p[1]) << 8) ^ p[2]) *
             2654435761u >>
         (32 - kHashBits);
}

std::vector<std::uint8_t> deflateFixed(const std::vector<std::uint8_t>& in) {
  BitWriter w;
  w.put(1, 1);  // BFINAL
  w.put(1, 2);  // BTYPE = 01, fixed Huffman

  std::vector<int> head(kHashSize, -1);
  std::vector<int> prev(in.size(), -1);

  const int n = static_cast<int>(in.size());
  int i = 0;
  while (i < n) {
    int bestLen = 0, bestDist = 0;
    if (i + kMinMatch <= n) {
      const std::uint32_t h = hash3(&in[static_cast<size_t>(i)]);
      int cand = head[h];
      // Bounded chain walk: this is a diagnostic-image writer, not a
      // general-purpose compressor, so cap the effort per position.
      int tries = 32;
      while (cand >= 0 && tries-- > 0) {
        const int dist = i - cand;
        if (dist <= 0 || dist > kWindow) break;
        int len = 0;
        const int maxLen = (n - i) < kMaxMatch ? (n - i) : kMaxMatch;
        while (len < maxLen && in[static_cast<size_t>(cand + len)] ==
                                   in[static_cast<size_t>(i + len)]) {
          ++len;
        }
        if (len > bestLen) {
          bestLen = len;
          bestDist = dist;
          if (len >= kMaxMatch) break;
        }
        cand = prev[static_cast<size_t>(cand)];
      }
      prev[static_cast<size_t>(i)] = head[h];
      head[h] = i;
    }

    if (bestLen >= kMinMatch) {
      putLength(w, bestLen);
      putDistance(w, bestDist);
      // Register the positions we skipped so later matches can find them.
      for (int k = 1; k < bestLen; ++k) {
        const int p = i + k;
        if (p + kMinMatch <= n) {
          const std::uint32_t h2 = hash3(&in[static_cast<size_t>(p)]);
          prev[static_cast<size_t>(p)] = head[h2];
          head[h2] = p;
        }
      }
      i += bestLen;
    } else {
      putLitLen(w, in[static_cast<size_t>(i)]);
      ++i;
    }
  }

  putLitLen(w, 256);  // end of block
  w.flush();
  return w.out;
}

std::vector<std::uint8_t> zlibWrap(const std::vector<std::uint8_t>& raw) {
  std::vector<std::uint8_t> z;
  z.push_back(0x78);  // CM=8, CINFO=7 (32K window)
  z.push_back(0x01);  // FCHECK making (0x78<<8|0x01) % 31 == 0
  const std::vector<std::uint8_t> body = deflateFixed(raw);
  z.insert(z.end(), body.begin(), body.end());
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521u;
    b = (b + a) % 65521u;
  }
  be32(z, (b << 16) | a);
  return z;
}

}  // namespace

bool writePng(const RgbImage& im, const std::string& path) {
  if (im.w <= 0 || im.h <= 0) return false;

  // Filter each scanline with type 2 (Up): subtracting the row above turns the
  // large flat background areas into runs of zero, which the matcher above
  // then collapses to almost nothing.
  const size_t rowBytes = static_cast<size_t>(im.w) * 3;
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<size_t>(im.h) * (rowBytes + 1));
  std::vector<std::uint8_t> prevRow(rowBytes, 0);
  for (int y = 0; y < im.h; ++y) {
    raw.push_back(2);  // filter: Up
    const size_t row = rgbIndex(0, y, im.w);
    for (size_t x = 0; x < rowBytes; ++x) {
      const std::uint8_t cur = im.px[row + x];
      raw.push_back(static_cast<std::uint8_t>(cur - prevRow[x]));
      prevRow[x] = cur;
    }
  }

  std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

  std::vector<std::uint8_t> ihdr;
  be32(ihdr, static_cast<std::uint32_t>(im.w));
  be32(ihdr, static_cast<std::uint32_t>(im.h));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type 2 = truecolour RGB
  ihdr.push_back(0);  // deflate
  ihdr.push_back(0);  // adaptive filtering
  ihdr.push_back(0);  // no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", zlibWrap(raw));
  chunk(out, "IEND", {});

  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  std::fclose(f);
  return ok;
}

}  // namespace stximg
