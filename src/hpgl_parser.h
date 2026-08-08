#pragma once

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

struct Vec2 {
  float x, y;
  bool operator==(const Vec2 &o) const {
    constexpr float kEpsilon = 0.001f;
    return std::abs(x - o.x) < kEpsilon && std::abs(y - o.y) < kEpsilon;
  }
};

struct Stroke {
  std::vector<Vec2> points;
  int pen = 1;
  // Source layer index, stamped when visible layers are merged for rendering.
  // Lets the renderer apply a per-layer solid-colour override on top of pens.
  int layer = 0;
  // Per-stroke AABB in HPGL units, populated by HpglParser.
  Vec2 bboxMin{ 1e30f,  1e30f};
  Vec2 bboxMax{-1e30f, -1e30f};
};

struct HpglDoc {
  std::vector<Stroke> strokes;
  float minX = 1e30f, minY = 1e30f;
  float maxX = -1e30f, maxY = -1e30f;
  bool empty() const { return strokes.empty(); }
};

// One plotter unit is 0.025 mm, so a centimetre is 400 units.  HP-GL's
// labelling commands (SI, ES) size characters in centimetres.
constexpr float kUnitsPerCm = 400.f;

// Default character size when a file labels without setting one (HP's A/A4
// default of 0.19 × 0.27 cm).
constexpr float kDefaultCharW = 0.19f * kUnitsPerCm;
constexpr float kDefaultCharH = 0.27f * kUnitsPerCm;

// Default LB terminator (ETX), changeable with DT.
constexpr char kDefaultLabelTerm = '\x03';

class HpglParser {
public:
  // Parse HPGL from an in-memory string.  If progress is non-null, the
  // parser updates it to the fraction of bytes consumed (0.0 → 1.0) so
  // a worker thread can report progress to a UI.
  HpglDoc parse(const std::string &content,
                std::atomic<float> *progress = nullptr);

  // Convenience: read file then parse.  Progress reflects the parse phase
  // (file-read time is small relative to parse on large inputs).
  HpglDoc parseFile(const std::string &path,
                    std::atomic<float> *progress = nullptr);

private:
  void handleSP(const std::string &params);
  void handlePU(const std::string &params);
  void handlePD(const std::string &params);
  void handlePA(const std::string &params);

  // Labelling group.
  void handleSI(const std::string &params);
  void handleLO(const std::string &params);
  void handleDI(const std::string &params);
  void handleSL(const std::string &params);
  void handleES(const std::string &params);
  void handleDT(const std::string &raw);
  void handleCP(const std::string &params);
  void handleLB(const std::string &text);
  void resetLabelState();

  // Text metrics, in plotter units, with ES applied.
  float cellWidth() const { return 1.5f * charW; }         // one character cell
  float extraAdvance() const { return esSpaces * cellWidth(); } // ES per char
  float lineSpacing() const { return 2.f * charH * (1.f + esLines); }

  void ensureStroke();
  void updateBounds(float x, float y);
  void addPoint(float x, float y); // push point + update stroke and doc bounds
  std::vector<float> parseCoords(const std::string &params);

  HpglDoc doc;
  int currentPen = 1;
  bool penDown    = false;
  float cx = 0, cy = 0;
  int curIdx = -1; // index into doc.strokes, -1 = none

  // Labelling state, reset by IN/DF.  Character sizes are plotter units.
  float charW = kDefaultCharW, charH = kDefaultCharH; // SI
  float dirX = 1, dirY = 0;                           // DI (unit vector)
  float slant = 0;                                    // SL (tangent)
  int labelOrigin = 1;                                // LO
  char labelTerm = kDefaultLabelTerm;                 // DT
  // ES extra space, as fractions of the character cell width and of the
  // baseline-to-baseline line spacing.  Negative values tighten the text.
  float esSpaces = 0, esLines = 0;                    // ES
};
