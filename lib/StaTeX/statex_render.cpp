#include "statex_render.h"

#include "statex_box.h"
#include "statex_draw.h"
#include "statex_layout.h"
#include "statex_node.h"

namespace statex {

Renderer::Renderer(u8* scratch, u32 scratchSize, RenderCaps caps)
    : _arena(scratch, scratchSize), _caps(caps) {}

ParseError Renderer::buildLayout(const c32* src, int len, float sizePx,
                                 BoxStore& boxes, Handle* rootBox) {
  // Everything allocated here is transient and freed before returning
  // (STX-MEM-05); only the box tree, which lives below `mk`, survives.
  const u32 mk = _arena.mark();
  {
    NodeStore nodes(_arena, _caps.maxNodes, _caps.maxNodeChildren);
    if (!nodes.ok()) return ParseError::OutOfMemory;
    Parser parser(_arena, nodes, _caps.maxDepth, _caps.maxOperands);
    if (!parser.ok()) return ParseError::OutOfMemory;

    ParseResult pr = parser.parse(src, len);
    if (pr.error != ParseError::Ok) return pr.error;

    Layout layout(_arena, nodes, boxes, sizePx);
    LayoutResult lr = layout.run(pr.root);
    if (!lr.ok) return lr.error;
    *rootBox = lr.rootBox;
  }
  _arena.rewind(mk);
  return ParseError::Ok;
}

ParseError Renderer::measure(const c32* src, int len, float sizePx,
                             RenderStats* stats) {
  _arena.reset();  // self-contained: no residue from prior calls (STX-API-02)

  BoxStore boxes(_arena, _caps.maxBoxes, _caps.maxBoxChildren);
  if (!boxes.ok()) return ParseError::OutOfMemory;

  Handle rootBox = NO_NODE;
  const ParseError e = buildLayout(src, len, sizePx, boxes, &rootBox);
  if (e != ParseError::Ok) {
    _arena.reset();
    return e;
  }

  if (stats != nullptr) {
    const Box& rb = boxes.get(rootBox);
    stats->width = rb.width;
    stats->height = rb.height;
    stats->depth = rb.depth;
    // Read before the reset below. Note the arena's high-water mark is
    // cumulative since construction -- Arena::reset() preserves it by design --
    // so on a Renderer that has also drawn, this reports that larger peak
    // rather than this call's. Only on a fresh Renderer is it the layout peak.
    stats->highWater = _arena.highWater();
  }

  _arena.reset();
  return ParseError::Ok;
}

ParseError Renderer::render(const c32* src, int len, float sizePx,
                            float originX, float baseline, Graphics2D& g,
                            RenderStats* stats, GlyphProbe* probe) {
  _arena.reset();  // self-contained: no residue from prior renders (STX-API-02)

  // Box store lives at the bottom of the arena: it must survive into the draw
  // phase after the atom/parse scratch is freed.
  BoxStore boxes(_arena, _caps.maxBoxes, _caps.maxBoxChildren);
  if (!boxes.ok()) return ParseError::OutOfMemory;

  Handle rootBox = NO_NODE;
  const ParseError e = buildLayout(src, len, sizePx, boxes, &rootBox);
  if (e != ParseError::Ok) {
    _arena.reset();
    return e;
  }

  if (stats != nullptr) {
    const Box& rb = boxes.get(rootBox);
    stats->width = rb.width;
    stats->height = rb.height;
    stats->depth = rb.depth;
  }

  const bool drawn =
      drawTree(_arena, boxes, rootBox, originX, baseline, g, probe);
  if (stats != nullptr) stats->highWater = _arena.highWater();

  if (!drawn) {
    _arena.reset();
    return ParseError::OutOfMemory;
  }
  return ParseError::Ok;
}

}  // namespace statex
