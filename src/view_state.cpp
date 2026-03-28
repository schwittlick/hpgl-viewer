#include "view_state.h"

#include <algorithm>
#include <cmath>

ViewState fitToCanvas(float canvasW, float canvasH, const HpglDoc &doc,
                      float rotation) {
  if (doc.empty())
    return {};

  float docW = doc.maxX - doc.minX;
  float docH = doc.maxY - doc.minY;
  if (docW < 1) docW = 1;
  if (docH < 1) docH = 1;

  float absC = fabsf(cosf(rotation));
  float absS = fabsf(sinf(rotation));
  float effW = docW * absC + docH * absS;
  float effH = docW * absS + docH * absC;

  constexpr float pad = 0.05f;
  ViewState vs;
  vs.scale = std::min(canvasW / effW, canvasH / effH) * (1.0f - 2.0f * pad);
  vs.panX  = canvasW * 0.5f - (doc.minX + docW * 0.5f) * vs.scale;
  vs.panY  = canvasH * 0.5f - (doc.minY + docH * 0.5f) * vs.scale;
  return vs;
}
