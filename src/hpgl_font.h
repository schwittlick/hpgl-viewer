#pragma once

// Single-stroke vector font used to render HP-GL LB (label) text.
//
// Real plotters draw labels with a built-in "stick" font; we use a
// Hershey-derived fixed-width version of it (see src/hpgl_font.cpp for the
// licence acknowledgements and tools/gen_hpgl_font.py for how the table is
// produced).
//
// The font is fixed-width, like the plotter's own: every character is drawn
// inside the same box and the pen advances one character cell per character,
// whatever the glyph.  Callers must not derive spacing from glyph geometry.
//
// Design-unit convention, matching the source font:
//   - the baseline is y = 0, x = 0 is the character's left edge
//   - a character spans x = 0 .. kGlyphWidth and an uppercase letter reaches
//     y = kCapHeight; descenders and accents fall outside that box
// Glyphs are scaled by the caller: HP-GL's SI command gives the character
// width and height in centimetres, so kGlyphWidth maps to the SI width and
// kCapHeight to the SI height.  The character cell the pen advances by is
// wider than the glyph (1.5 × the SI width), which is what separates one
// character from the next.

namespace hpgl_font {

// One polyline of a glyph: `count` x,y pairs drawn pen-down in order.
struct FontStroke {
  const float *xy;
  int count;
};

struct FontGlyph {
  const FontStroke *strokes;
  int strokeCount;
};

// Left edge → right edge of a character.
constexpr float kGlyphWidth = 4.f;

// Baseline → top of an uppercase letter.
constexpr float kCapHeight = 8.f;

// Glyph for a byte value.  Codes below 32 render blank; the range above 126
// follows HP's Roman-8 encoding.
const FontGlyph &glyph(unsigned char c);

} // namespace hpgl_font
