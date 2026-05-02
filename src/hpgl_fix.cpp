#include "hpgl_fix.h"

#include <cmath>
#include <cstdio>


HpglDoc fixLongPenUps(const HpglDoc &src, float thresholdUnits, float stepUnits) {
  HpglDoc result;
  result.minX = src.minX; result.maxX = src.maxX;
  result.minY = src.minY; result.maxY = src.maxY;

  for (size_t i = 0; i < src.strokes.size(); ++i) {
    const auto &stroke = src.strokes[i];
    if (stroke.points.empty()) { result.strokes.push_back(stroke); continue; }

    if (i > 0 && !src.strokes[i - 1].points.empty()) {
      Vec2  prev = src.strokes[i - 1].points.back();
      Vec2  dst  = stroke.points.front();
      float dx   = dst.x - prev.x;
      float dy   = dst.y - prev.y;
      float dist = sqrtf(dx*dx + dy*dy);

      if (dist > thresholdUnits) {
        int steps = (int)(dist / stepUnits);
        for (int k = 1; k <= steps; ++k) {
          float t  = (float)k * stepUnits / dist;
          Vec2  wp = {prev.x + t * dx, prev.y + t * dy};
          result.strokes.push_back(Stroke{{wp, wp}, kWaypointPen}); // dot
        }
      }
    }

    result.strokes.push_back(stroke);
  }

  return result;
}

HpglDoc mergeCloseStrokes(const HpglDoc &src, const float thresholdsUnits[10]) {
  HpglDoc result;
  result.minX = src.minX; result.maxX = src.maxX;
  result.minY = src.minY; result.maxY = src.maxY;

  for (const auto &stroke : src.strokes) {
    if (stroke.points.empty()) { result.strokes.push_back(stroke); continue; }

    if (!result.strokes.empty()) {
      Stroke &last = result.strokes.back();
      if (!last.points.empty() && last.pen == stroke.pen &&
          stroke.pen != kWaypointPen) {
        int penIdx = stroke.pen - 1;
        float threshold = (penIdx >= 0 && penIdx < 10) ? thresholdsUnits[penIdx] : 0.0f;
        Vec2  endPrev   = last.points.back();
        Vec2  startCur  = stroke.points.front();
        float dx        = startCur.x - endPrev.x;
        float dy        = startCur.y - endPrev.y;
        float distSq    = dx*dx + dy*dy;
        if (distSq <= threshold * threshold) {
          for (const auto &p : stroke.points)
            last.points.push_back(p);
          last.bboxMin.x = std::min(last.bboxMin.x, stroke.bboxMin.x);
          last.bboxMin.y = std::min(last.bboxMin.y, stroke.bboxMin.y);
          last.bboxMax.x = std::max(last.bboxMax.x, stroke.bboxMax.x);
          last.bboxMax.y = std::max(last.bboxMax.y, stroke.bboxMax.y);
          continue;
        }
      }
    }

    result.strokes.push_back(stroke);
  }

  return result;
}

static void updateBbox(Stroke &s, Vec2 p) {
  s.bboxMin.x = std::min(s.bboxMin.x, p.x);
  s.bboxMin.y = std::min(s.bboxMin.y, p.y);
  s.bboxMax.x = std::max(s.bboxMax.x, p.x);
  s.bboxMax.y = std::max(s.bboxMax.y, p.y);
}

HpglDoc splitLongStrokes(const HpglDoc &src, float maxLengthUnits) {
  HpglDoc result;
  result.minX = src.minX; result.maxX = src.maxX;
  result.minY = src.minY; result.maxY = src.maxY;
  if (maxLengthUnits <= 0.0f) { result.strokes = src.strokes; return result; }

  for (const auto &stroke : src.strokes) {
    if (stroke.points.size() < 2 || stroke.pen == kWaypointPen) {
      result.strokes.push_back(stroke);
      continue;
    }

    Stroke current;
    current.pen = stroke.pen;
    current.points.push_back(stroke.points[0]);
    updateBbox(current, stroke.points[0]);
    float budget = maxLengthUnits;

    for (size_t i = 1; i < stroke.points.size(); ++i) {
      Vec2  a   = stroke.points[i - 1];
      Vec2  b   = stroke.points[i];
      float dx  = b.x - a.x;
      float dy  = b.y - a.y;
      float seg = sqrtf(dx*dx + dy*dy);

      while (seg > budget) {
        float t  = budget / seg;
        Vec2  sp = {a.x + t * dx, a.y + t * dy};
        current.points.push_back(sp);
        updateBbox(current, sp);
        result.strokes.push_back(current);

        current = Stroke{};
        current.pen = stroke.pen;
        current.points.push_back(sp);
        updateBbox(current, sp);

        a   = sp;
        dx  = b.x - a.x;
        dy  = b.y - a.y;
        seg = sqrtf(dx*dx + dy*dy);
        budget = maxLengthUnits;
      }

      current.points.push_back(b);
      updateBbox(current, b);
      budget -= seg;

      if (budget <= 0.0f && i + 1 < stroke.points.size()) {
        result.strokes.push_back(current);
        current = Stroke{};
        current.pen = stroke.pen;
        current.points.push_back(b);
        updateBbox(current, b);
        budget = maxLengthUnits;
      }
    }

    if (current.points.size() >= 2)
      result.strokes.push_back(current);
  }

  return result;
}

bool exportHpgl(const HpglDoc &doc, const std::string &path, int vsValue) {
  FILE *f = fopen(path.c_str(), "w");
  if (!f) return false;

  fprintf(f, "IN;\n");
  int  curPen  = -1;
  bool havePos = false;
  Vec2 lastPos{0.f, 0.f};
  for (const auto &stroke : doc.strokes) {
    if (stroke.points.empty()) continue;
    if (stroke.pen != curPen) {
      fprintf(f, "SP%d;\n", stroke.pen);
      curPen = stroke.pen;
    }
    const Vec2 &s = stroke.points.front();
    // If the new stroke starts where the previous one ended, lift the pen,
    // move 1 HPGL unit away (guarantees the plotter registers a physical lift),
    // then return to the split point — all while pen is up — before the next
    // PD re-engages it.
    if (havePos && s == lastPos)
      fprintf(f, "PU%.0f,%.0f;PU%.0f,%.0f;", s.x + 1.f, s.y, s.x, s.y);
    else
      fprintf(f, "PU%.0f,%.0f;", s.x, s.y);
    if (stroke.points.size() == 1) {
      fprintf(f, "PD;\n");
    } else {
      fprintf(f, "VS%d;\n", vsValue);
      for (size_t i = 1; i < stroke.points.size(); ++i) {
        fprintf(f, "PD%.0f,%.0f;\n", stroke.points[i].x, stroke.points[i].y);
      }
    }
    lastPos = stroke.points.back();
    havePos = true;
  }
  fprintf(f, "PU;\n");
  fprintf(f, "SP0;\n");
  fclose(f);
  return true;
}

DocStats computeDocStats(const HpglDoc &doc) {
  DocStats stats;
  const Stroke *prev = nullptr; // last non-empty stroke seen
  for (const auto &s : doc.strokes) {
    if (s.points.empty()) continue;
    ++stats.numPaths;
    // pen-down distance within this stroke
    for (size_t i = 0; i + 1 < s.points.size(); ++i) {
      float dx = s.points[i+1].x - s.points[i].x;
      float dy = s.points[i+1].y - s.points[i].y;
      stats.penDownMm += sqrtf(dx*dx + dy*dy);
    }
    // pen-up gap from the end of the previous stroke to the start of this one
    if (prev) {
      float dx = s.points.front().x - prev->points.back().x;
      float dy = s.points.front().y - prev->points.back().y;
      stats.penUpMm += sqrtf(dx*dx + dy*dy);
    }
    prev = &s;
  }
  stats.penDownMm /= kHpglUnitsPerMm;
  stats.penUpMm   /= kHpglUnitsPerMm;
  return stats;
}

std::string fixedPath(const std::string &src) {
  auto dot = src.rfind('.');
  if (dot == std::string::npos) return src + "_fixed.hpgl";
  return src.substr(0, dot) + "_fixed" + src.substr(dot);
}
