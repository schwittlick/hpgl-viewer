#pragma once

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
};

struct HpglDoc {
  std::vector<Stroke> strokes;
  float minX = 1e30f, minY = 1e30f;
  float maxX = -1e30f, maxY = -1e30f;
  bool empty() const { return strokes.empty(); }
};

class HpglParser {
public:
  // Parse HPGL from an in-memory string.
  HpglDoc parse(const std::string &content);

  // Convenience: read file then parse.
  HpglDoc parseFile(const std::string &path);

private:
  void handleSP(const std::string &params);
  void handlePU(const std::string &params);
  void handlePD(const std::string &params);
  void handlePA(const std::string &params);

  void ensureStroke();
  void updateBounds(float x, float y);
  std::vector<float> parseCoords(const std::string &params);

  HpglDoc doc;
  int currentPen = 1;
  bool penDown    = false;
  float cx = 0, cy = 0;
  int curIdx = -1; // index into doc.strokes, -1 = none
};
