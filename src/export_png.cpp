#include "export_png.h"

#include "hpgl_fix.h" // kHpglUnitsPerMm

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <png.h>

// ── Software rasteriser ───────────────────────────────────────────────────────

// Paint a filled disk of radius r (pixels) at (cx, cy) with alpha blending
// over the existing RGBA pixel buffer.
static void paintDisk(std::vector<uint8_t> &px, int w, int h,
                      int cx, int cy, int r,
                      uint8_t red, uint8_t grn, uint8_t blu, float alpha) {
  int x0 = std::max(0, cx - r), x1 = std::min(w - 1, cx + r);
  int y0 = std::max(0, cy - r), y1 = std::min(h - 1, cy + r);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      int dx = x - cx, dy = y - cy;
      if (dx*dx + dy*dy > r*r) continue;
      int idx = (y * w + x) * 4;
      float a1 = 1.f - alpha;
      px[idx+0] = (uint8_t)(px[idx+0] * a1 + red * alpha);
      px[idx+1] = (uint8_t)(px[idx+1] * a1 + grn * alpha);
      px[idx+2] = (uint8_t)(px[idx+2] * a1 + blu * alpha);
      // alpha channel stays 255 (opaque)
    }
  }
}

// Rasterise a thick line segment from (x0,y0) to (x1,y1) by stamping
// filled disks at intervals of max(1, radius) along the segment.
static void rasteriseSegment(std::vector<uint8_t> &px, int w, int h,
                              float x0, float y0, float x1, float y1,
                              int radius,
                              uint8_t red, uint8_t grn, uint8_t blu,
                              float alpha) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx*dx + dy*dy);
  int   steps = std::max(1, static_cast<int>(ceilf(len / std::max(1, radius))));
  for (int i = 0; i <= steps; ++i) {
    float t  = static_cast<float>(i) / steps;
    int   cx = static_cast<int>(roundf(x0 + t * dx));
    int   cy = static_cast<int>(roundf(y0 + t * dy));
    paintDisk(px, w, h, cx, cy, radius, red, grn, blu, alpha);
  }
}

// ── PNG writer ────────────────────────────────────────────────────────────────

static bool writePng(const std::string &path, const std::vector<uint8_t> &pixels,
                     int w, int h) {
  FILE *fp = fopen(path.c_str(), "wb");
  if (!fp) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, nullptr); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  for (int y = 0; y < h; ++y) {
    auto *row = const_cast<png_bytep>(&pixels[(size_t)y * w * 4]);
    png_write_row(png, row);
  }

  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool exportPng(const HpglDoc &doc, const PenStyle pens[8],
               const std::string &path, float dpi) {
  if (doc.empty()) return false;

  float docW = doc.maxX - doc.minX;
  float docH = doc.maxY - doc.minY;
  if (docW < 1) docW = 1;
  if (docH < 1) docH = 1;

  // Physical size in inches → pixel dimensions at requested DPI, with 5% margin.
  constexpr float kMmPerInch = 25.4f;
  constexpr float margin = 0.05f;
  float docWMm = docW / kHpglUnitsPerMm;
  float docHMm = docH / kHpglUnitsPerMm;
  int   widthPx  = static_cast<int>(docWMm / kMmPerInch * dpi / (1.f - 2.f * margin));
  int   heightPx = static_cast<int>(docHMm / kMmPerInch * dpi / (1.f - 2.f * margin));

  // Scale: HPGL units → pixels
  float scale = static_cast<float>(widthPx) / docW * (1.f - 2.f * margin);

  float panX = widthPx  * margin - doc.minX * scale;
  float panY = heightPx * margin - doc.minY * scale;

  // RGBA pixel buffer — white background
  std::vector<uint8_t> pixels((size_t)widthPx * heightPx * 4, 0xFF);

  // Rasterise all strokes
  for (const auto &stroke : doc.strokes) {
    if (stroke.points.empty()) continue;

    int pi = std::max(0, std::min(stroke.pen - 1, 7));
    // Convert ImVec4 colour [0,1] → [0,255]
    uint8_t red = static_cast<uint8_t>(pens[pi].color.x * 255.f);
    uint8_t grn = static_cast<uint8_t>(pens[pi].color.y * 255.f);
    uint8_t blu = static_cast<uint8_t>(pens[pi].color.z * 255.f);
    float   alp = pens[pi].color.w;
    // Pen radius in pixels: thickness [mm] × (scale [px/HPGL] × kHpglUnitsPerMm)
    int radius = std::max(1, static_cast<int>(
        roundf(pens[pi].thickness * scale * kHpglUnitsPerMm * 0.5f)));

    auto toScreen = [&](const Vec2 &v) -> std::pair<float, float> {
      return {v.x * scale + panX, v.y * scale + panY};
    };

    // Single-point / dot stroke
    if (stroke.points.size() == 1 ||
        (stroke.points.size() == 2 && stroke.points[0] == stroke.points[1])) {
      auto [sx, sy] = toScreen(stroke.points[0]);
      paintDisk(pixels, widthPx, heightPx,
                static_cast<int>(roundf(sx)), static_cast<int>(roundf(sy)),
                radius, red, grn, blu, alp);
      continue;
    }

    for (size_t i = 0; i + 1 < stroke.points.size(); ++i) {
      auto [x0, y0] = toScreen(stroke.points[i]);
      auto [x1, y1] = toScreen(stroke.points[i + 1]);
      rasteriseSegment(pixels, widthPx, heightPx,
                       x0, y0, x1, y1, radius, red, grn, blu, alp);
    }
  }

  return writePng(path, pixels, widthPx, heightPx);
}
