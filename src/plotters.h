#pragma once

#include <string>
#include <vector>

// ── Plotter registry ──────────────────────────────────────────────────────────
//
// Describes the physical plotting envelope of a plotter so a loaded HPGL
// drawing can be checked against it.  HPGL files are authored in the same
// native unit space as `max_area`, so every rectangle here overlays a document
// directly — no transform needed.
//
// Three nested rectangles, outermost first:
//
//   paper      the physical sheet
//   maxArea    the furthest the pen can reach ("plotter bounds")
//   effArea    maxArea inset by a deliberate per-edge margin — the area a
//              drawing should actually stay within
//
// Only maxArea and the margin come from plotter documentation.  The paper
// rectangle needs a measured offset that no plotter manual states; entries
// carry `verified = false` until someone measures a real plot.

struct Rect {
  float x = 0, y = 0, x2 = 0, y2 = 0;
  float w() const { return x2 - x; }
  float h() const { return y2 - y; }
};

// Per-edge inset in HPGL units.  Positive shrinks that edge inward.
struct PlotterMargin {
  float left = 0, bottom = 0, right = 0, top = 0;
  bool any() const { return left || bottom || right || top; }
};

// The physical sheet.  `offsetLeftMm` is the distance from the sheet's left
// edge to maxArea.x, `offsetBottomMm` from the sheet's bottom edge to
// maxArea.y — i.e. how far in from the paper edge the pen can first reach.
// Negative means the plot area runs off the sheet.
struct PaperSpec {
  bool  present  = false;
  bool  verified = false;
  float wMm = 0, hMm = 0;
  float offsetLeftMm = 0, offsetBottomMm = 0;
};

struct Plotter {
  std::string   id, name, hpglModel, note;
  Rect          maxArea;
  PlotterMargin margin;
  float         unitsPerMmX = 40.f, unitsPerMmY = 40.f;
  bool          defaultForModel = false;
  PaperSpec     paper;
};

struct PlotterRegistry {
  std::vector<Plotter> plotters;
  bool empty() const { return plotters.empty(); }
  // Index of the entry with this id / name, or -1.
  int indexOfId(const std::string &id) const;
  int indexOfName(const std::string &name) const;
};

// ── Loading ───────────────────────────────────────────────────────────────────

// Parse a registry from JSON text.  Returns false and fills err on malformed
// input or a missing/empty "plotters" array.  Entries missing "max_area" are
// skipped rather than failing the whole file.
bool parsePlotterRegistry(const std::string &json, PlotterRegistry &out,
                          std::string &err);

// Read a file, then parse it.  Returns false and fills err if unreadable.
bool loadPlotterRegistry(const std::string &path, PlotterRegistry &out,
                         std::string &err);

// Candidate registry locations, most specific first: $HPGL_VIEWER_PLOTTERS,
// the per-user config dir, the installed data dir, then data/plotters.json
// relative to the working directory (so the app runs from a source tree).
std::vector<std::string> defaultRegistryPaths();

// Try each defaultRegistryPaths() entry in turn.  On success `usedPath` is the
// file that loaded.  On failure err lists what was tried.
bool loadPlotterRegistryFromDefaults(PlotterRegistry &out,
                                     std::string &usedPath, std::string &err);

// ── Geometry ──────────────────────────────────────────────────────────────────

// maxArea inset by the margin.  Margins that meet or cross collapse the rect
// to zero size rather than producing an inverted rectangle.
Rect effectivePlotArea(const Plotter &p);

// The sheet in HPGL units.  Returns false when the entry has no paper block.
bool paperRect(const Plotter &p, Rect &out);

// Per-edge clearance in millimetres from `inner` to `outer`.  Positive means
// inner is inside outer with that much room to spare on that edge; negative
// means inner overruns that edge by that much.
struct Clearance {
  float left = 0, right = 0, bottom = 0, top = 0;
  // True when the inner rect is fully inside the outer one.
  bool fits() const { return left >= 0 && right >= 0 && bottom >= 0 && top >= 0; }
  // Smallest clearance across the four edges — the worst edge.
  float worst() const;
};
Clearance clearanceMm(const Rect &inner, const Rect &outer,
                      float unitsPerMmX, float unitsPerMmY);

// Index of the plotter whose `name` appears in `filename`, or -1.  Longest
// name wins, so "hpdm_sx_a1" beats a hypothetical "hpdm_sx".  Matching is
// case-insensitive.  Exported files are conventionally named
// "<work>_<plotter-name>_<hash>.hpgl", which is what this keys off.
int matchPlotterByFilename(const PlotterRegistry &reg,
                           const std::string &filename);
