#pragma once

#include "hpgl_parser.h"

struct ViewState {
  float panX  = 0.0f;
  float panY  = 0.0f;
  float scale = 1.0f;
};

// Compute pan/scale that fits doc into a canvas of the given pixel size,
// accounting for rotation. Returns an identity ViewState if doc is empty.
ViewState fitToCanvas(float canvasW, float canvasH, const HpglDoc &doc,
                      float rotation);

// As fitToCanvas, but for an explicit HPGL-unit rectangle rather than a
// document — used to fit the union of the drawing and the plotter/paper
// bounds so an off-paper drawing stays visible alongside the sheet.
// A rectangle with non-positive width or height is clamped to 1 unit.
ViewState fitToBounds(float canvasW, float canvasH,
                      float minX, float minY, float maxX, float maxY,
                      float rotation);
