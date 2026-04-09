#include "hpgl_fix.h"

#include <cmath>
#include <cstdio>


HpglDoc fixLongPenUps(const HpglDoc &src, float thresholdUnits,
                      float stepUnits, float cutoffX) {
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

      if (dist > thresholdUnits && prev.x <= cutoffX) {
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
      if (!last.points.empty() && last.pen == stroke.pen) {
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

bool exportHpgl(const HpglDoc &doc, const std::string &path, int vsValue) {
  FILE *f = fopen(path.c_str(), "w");
  if (!f) return false;

  fprintf(f, "IN;\n");
  int curPen = -1;
  for (const auto &stroke : doc.strokes) {
    if (stroke.points.empty()) continue;
    if (stroke.pen != curPen) {
      fprintf(f, "SP%d;\n", stroke.pen);
      curPen = stroke.pen;
    }
    const Vec2 &s = stroke.points.front();
    fprintf(f, "PU%.0f,%.0f;", s.x, s.y);
    if (stroke.points.size() == 1) {
      fprintf(f, "PD;\n");
    } else {
      fprintf(f, "VS%d;\n", vsValue);
      for (size_t i = 1; i < stroke.points.size(); ++i) {
        fprintf(f, "PD%.0f,%.0f;\n", stroke.points[i].x, stroke.points[i].y);
      }
    }
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
