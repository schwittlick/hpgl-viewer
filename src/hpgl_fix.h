#pragma once

#include "hpgl_parser.h"

#include <string>

inline constexpr float kHpglUnitsPerMm = 40.0f;
inline constexpr float kHpglUnitsPerCm = kHpglUnitsPerMm * 10.0f;
inline constexpr int   kWaypointPen    = 8;

// For every inter-stroke pen-up move whose Euclidean length exceeds
// thresholdUnits, insert single-point pen-down stops (pen kWaypointPen) spaced
// stepUnits apart along the direct path.
HpglDoc fixLongPenUps(const HpglDoc &src, float thresholdUnits, float stepUnits);

// Write doc as HPGL.  Single-point strokes become a pen-down touch.
// Multi-point strokes are preceded by VS<vsValue>; (velocity select, 1–8).
// Ends with PU; SP0; to park the pen.
bool exportHpgl(const HpglDoc &doc, const std::string &path, int vsValue = 1);

struct DocStats {
  int   numPaths  = 0;
  float penDownMm = 0.0f;
  float penUpMm   = 0.0f;
};

// Compute path count and total pen-down / pen-up travel in millimetres.
// Empty strokes (no points) are excluded from numPaths.
DocStats computeDocStats(const HpglDoc &doc);

// Merge consecutive same-pen strokes whose gap (end of A → start of B) is
// ≤ the per-pen threshold given in HPGL units (thresholdsUnits[i] applies to
// pen i+1).  Chain-merging is applied: A→B→C are all merged when each
// consecutive gap is within threshold.
HpglDoc mergeCloseStrokes(const HpglDoc &src, const float thresholdsUnits[10]);

// Split strokes whose pen-down polyline length exceeds maxLengthUnits into
// shorter consecutive strokes.  Each split inserts a coincident pen-up + pen-
// down at the split point (the previous stroke ends there; the next begins
// there).  Single-point strokes and waypoint dots pass through unchanged.
// A non-positive maxLengthUnits is a no-op.
HpglDoc splitLongStrokes(const HpglDoc &src, float maxLengthUnits);

// Partition a document into dots-only and lines-only sub-documents.
// A "dot" is a stroke with a single point or with all points coincident.
// Empty strokes are dropped.  Document bounds are recomputed for each.
struct DotsLinesSplit { HpglDoc dots; HpglDoc lines; };
DotsLinesSplit splitDotsAndLines(const HpglDoc &src);

// Derive the output paths used by the dots/lines split export: inserts
// "_dots" / "_lines" before the last extension (or appends "_dots.hpgl" /
// "_lines.hpgl" if there is no extension).
std::string dotsPath(const std::string &src);
std::string linesPath(const std::string &src);

// Derive the output path for an exported fix: inserts "_fixed" before the
// last extension, or appends "_fixed.hpgl" if there is no extension.
std::string fixedPath(const std::string &src);
