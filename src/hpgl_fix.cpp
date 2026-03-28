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

bool exportHpgl(const HpglDoc &doc, const std::string &path) {
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
      fprintf(f, "PD");
      for (size_t i = 1; i < stroke.points.size(); ++i) {
        if (i > 1) fputc(',', f);
        fprintf(f, "%.0f,%.0f", stroke.points[i].x, stroke.points[i].y);
      }
      fprintf(f, ";\n");
    }
  }
  fprintf(f, "PU;\n");
  fprintf(f, "SP0;\n");
  fclose(f);
  return true;
}

DocStats computeDocStats(const HpglDoc &doc) {
  DocStats stats;
  for (const auto &s : doc.strokes)
    if (!s.points.empty()) ++stats.numPaths;

  for (const auto &s : doc.strokes) {
    for (size_t i = 0; i + 1 < s.points.size(); ++i) {
      float dx = s.points[i+1].x - s.points[i].x;
      float dy = s.points[i+1].y - s.points[i].y;
      stats.penDownMm += sqrtf(dx*dx + dy*dy);
    }
  }
  for (size_t i = 0; i + 1 < doc.strokes.size(); ++i) {
    const auto &a = doc.strokes[i];
    const auto &b = doc.strokes[i+1];
    if (a.points.empty() || b.points.empty()) continue;
    float dx = b.points.front().x - a.points.back().x;
    float dy = b.points.front().y - a.points.back().y;
    stats.penUpMm += sqrtf(dx*dx + dy*dy);
  }
  stats.penDownMm /= kHpglUnitsPerMm;
  stats.penUpMm   /= kHpglUnitsPerMm;
  return stats;
}
