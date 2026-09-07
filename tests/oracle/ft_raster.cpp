#include "ft_raster.h"

#include <cstdlib>
#include <map>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace stxoracle {
namespace {

/**
 * MicroTeX names faces relative to its own resource root ("res/fonts/..."),
 * which is where they lived before the firmware moved to the SDF atlas and the
 * binaries were dropped. They are restored under tests/oracle/fonts/, so strip
 * the prefix and re-root.
 */
std::string resolvePath(const std::string& microtexPath) {
  static const std::string kPrefix = "res/fonts/";
  std::string rel = microtexPath;
  const size_t at = rel.find(kPrefix);
  if (at != std::string::npos) rel = rel.substr(at + kPrefix.size());
  return texFontRoot() + "/" + rel;
}

}  // namespace

std::string texFontRoot() {
  const char* env = std::getenv("STATEX_TEX_FONTS");
  if (env != nullptr && *env != '\0') return env;
#ifdef STATEX_TEX_FONT_DIR
  return STATEX_TEX_FONT_DIR;
#else
  return "tests/oracle/fonts";
#endif
}

struct FreeTypeRasteriser::Impl {
  FT_Library lib = nullptr;
  bool started = false;
  std::string error;
  std::map<std::string, FT_Face> faces;  // resolved path -> face (nullptr = tried and failed)

  FT_Face face(const std::string& microtexPath) {
    const std::string path = resolvePath(microtexPath);
    const auto it = faces.find(path);
    if (it != faces.end()) return it->second;
    FT_Face f = nullptr;
    if (FT_New_Face(lib, path.c_str(), 0, &f) != 0) f = nullptr;
    faces[path] = f;
    return f;
  }
};

FreeTypeRasteriser::FreeTypeRasteriser() : _impl(new Impl) {
  if (FT_Init_FreeType(&_impl->lib) != 0) {
    _impl->error = "FT_Init_FreeType failed";
    return;
  }
  _impl->started = true;
}

FreeTypeRasteriser::~FreeTypeRasteriser() {
  if (_impl->started) {
    for (auto& kv : _impl->faces) {
      if (kv.second != nullptr) FT_Done_Face(kv.second);
    }
    FT_Done_FreeType(_impl->lib);
  }
  delete _impl;
}

bool FreeTypeRasteriser::ok() const { return _impl->started; }
const std::string& FreeTypeRasteriser::error() const { return _impl->error; }
int FreeTypeRasteriser::faceCount() const {
  return static_cast<int>(_impl->faces.size());
}

bool FreeTypeRasteriser::draw(const OracleGlyph& g, stximg::Image& target) {
  if (!_impl->started) return false;
  FT_Face face = _impl->face(g.fontPath);
  if (face == nullptr) return false;
  if (g.emPx <= 0.0f) return false;

  // 26.6 fixed point, so fractional sizes survive -- MicroTeX scales scripts
  // by non-integer factors and rounding here would shift them.
  const FT_F26Dot6 size = static_cast<FT_F26Dot6>(g.emPx * 64.0f + 0.5f);
  if (FT_Set_Char_Size(face, size, size, 72, 72) != 0) return false;

  // TeX slots are font positions. These faces carry a cmap covering them, but
  // fall back to treating the slot as a raw glyph index if it does not.
  FT_UInt index = FT_Get_Char_Index(face, g.slot);
  if (index == 0) index = g.slot;
  if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) != 0) return false;
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return false;

  const FT_Bitmap& bm = face->glyph->bitmap;
  if (bm.width == 0 || bm.rows == 0) return true;  // a space: nothing to draw
  const int x0 = static_cast<int>(g.x + 0.5f) + face->glyph->bitmap_left;
  const int y0 = static_cast<int>(g.baselineY + 0.5f) - face->glyph->bitmap_top;

  for (unsigned row = 0; row < bm.rows; ++row) {
    for (unsigned col = 0; col < bm.width; ++col) {
      const unsigned char v =
          bm.buffer[static_cast<size_t>(row) * static_cast<size_t>(bm.pitch) +
                    col];
      if (v == 0) continue;
      const int px = x0 + static_cast<int>(col);
      const int py = y0 + static_cast<int>(row);
      if (v > target.at(px, py)) target.set(px, py, v);
    }
  }
  return true;
}

}  // namespace stxoracle
