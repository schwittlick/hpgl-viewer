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
