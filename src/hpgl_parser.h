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

  void ensureStroke();
  void updateBounds(float x, float y);
  void addPoint(float x, float y); // push point + update stroke and doc bounds
  std::vector<float> parseCoords(const std::string &params);

  HpglDoc doc;
  int currentPen = 1;
  bool penDown    = false;
  float cx = 0, cy = 0;
  int curIdx = -1; // index into doc.strokes, -1 = none
};
