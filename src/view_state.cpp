#include "view_state.h"

#include <algorithm>
#include <cmath>

ViewState fitToBounds(float canvasW, float canvasH,
                      float minX, float minY, float maxX, float maxY,
                      float rotation) {
  float w = maxX - minX;
  float h = maxY - minY;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  float absC = fabsf(cosf(rotation));
  float absS = fabsf(sinf(rotation));
  float effW = w * absC + h * absS;
  float effH = w * absS + h * absC;

  constexpr float pad = 0.05f;
  ViewState vs;
  vs.scale = std::min(canvasW / effW, canvasH / effH) * (1.0f - 2.0f * pad);
  vs.panX  = canvasW * 0.5f - (minX + w * 0.5f) * vs.scale;
  vs.panY  = canvasH * 0.5f + (minY + h * 0.5f) * vs.scale;
  return vs;
}

ViewState fitToCanvas(float canvasW, float canvasH, const HpglDoc &doc,
                      float rotation) {
  if (doc.empty())
    return {};

  return fitToBounds(canvasW, canvasH, doc.minX, doc.minY, doc.maxX, doc.maxY,
                     rotation);
}
