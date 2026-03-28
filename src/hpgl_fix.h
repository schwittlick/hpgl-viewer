#pragma once

#include "hpgl_parser.h"

#include <string>

inline constexpr float kHpglUnitsPerMm = 40.0f;
inline constexpr float kHpglUnitsPerCm = kHpglUnitsPerMm * 10.0f;
inline constexpr int   kWaypointPen    = 8;

// For every inter-stroke pen-up move whose Euclidean length exceeds
// thresholdUnits, insert single-point pen-down stops (pen kWaypointPen) spaced
// stepUnits apart along the direct path.  Only moves whose start X is
// within cutoffX are considered.
HpglDoc fixLongPenUps(const HpglDoc &src, float thresholdUnits,
                      float stepUnits, float cutoffX);

// Write doc as HPGL.  Single-point strokes become a pen-down touch.
// Ends with PU; SP0; to park the pen.
bool exportHpgl(const HpglDoc &doc, const std::string &path);

struct DocStats {
  int   numPaths  = 0;
  float penDownMm = 0.0f;
  float penUpMm   = 0.0f;
};

// Compute path count and total pen-down / pen-up travel in millimetres.
// Empty strokes (no points) are excluded from numPaths.
DocStats computeDocStats(const HpglDoc &doc);

// Derive the output path for an exported fix: inserts "_fixed" before the
// last extension, or appends "_fixed.hpgl" if there is no extension.
std::string fixedPath(const std::string &src);
