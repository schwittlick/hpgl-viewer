#pragma once

// Single-stroke vector font used to render HP-GL LB (label) text.
//
// Real plotters draw labels with a built-in "stick" font; we approximate it
// with Hershey Sans 1-stroke (see src/hpgl_font.cpp for the licence
// acknowledgements and tools/gen_hpgl_font.py for how the table is produced).
//
// Design-unit convention, matching the source font:
//   - the baseline is y = 0, x = 0 is the character's left edge
//   - an uppercase letter reaches y = kCapHeight
//   - descenders reach y = -220, accents/ascenders up to y = 788
// Glyphs are scaled by the caller: HP-GL's SI command gives the character
// width and height in centimetres, so kCellWidth maps to one character cell
// (1.5 × the SI width) and kCapHeight maps to the SI height.

namespace hpgl_font {

// One polyline of a glyph: `count` x,y pairs drawn pen-down in order.
struct FontStroke {
  const float *xy;
  int count;
};

struct FontGlyph {
  float advance; // pen movement to the next character, in design units
  const FontStroke *strokes;
  int strokeCount;
};

// Baseline → top of an uppercase letter.
constexpr float kCapHeight = 662.f;

// Nominal advance of one character cell (the mean A–Z advance).
extern const float kCellWidth;

// Glyph for an ASCII code.  Codes outside 32..126 render blank.
const FontGlyph &glyph(unsigned char c);

} // namespace hpgl_font
