#pragma once

#include "hpgl_parser.h"

#include <cmath>
#include <cstdio>
#include <string>

// For every inter-stroke pen-up move whose Euclidean length exceeds
// thresholdUnits, insert single-point pen-down stops (pen 8) spaced
// stepUnits apart along the direct path.  Only moves whose start X is
// within cutoffX are considered.
static HpglDoc fixLongPenUps(const HpglDoc &src, float thresholdUnits,
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
          result.strokes.push_back(Stroke{{wp, wp}, 8}); // dot, pen 8
        }
      }
    }

    result.strokes.push_back(stroke);
  }

  return result;
}

// Write doc as HPGL.  Single-point strokes become a pen-down touch.
// Ends with PU; SP0; to park the pen.
static bool exportHpgl(const HpglDoc &doc, const std::string &path) {
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
