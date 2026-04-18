#include "../src/view_state.h"
#include "test_harness.h"

#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

static HpglDoc makeDoc(float x0, float y0, float x1, float y1) {
  HpglDoc doc;
  doc.minX = x0; doc.minY = y0;
  doc.maxX = x1; doc.maxY = y1;
  doc.strokes.push_back(Stroke{{{x0, y0}, {x1, y1}}, 1});
  return doc;
}

static bool nearlyEqual(float a, float b, float eps = 1e-4f) {
  return fabsf(a - b) < eps;
}

// ── Empty doc ─────────────────────────────────────────────────────────────────

static void test_empty_doc_returns_identity() {
  HpglDoc doc; // no strokes → empty()
  ViewState vs = fitToCanvas(800.f, 600.f, doc, 0.f);
  REQUIRE(vs.panX  == 0.f);
  REQUIRE(vs.panY  == 0.f);
  REQUIRE(vs.scale == 1.f);
}

// ── Scale ─────────────────────────────────────────────────────────────────────

static void test_square_doc_in_square_canvas() {
  // doc 0–100 x 0–100, canvas 200×200, no rotation
  // effW = effH = 100
  // scale = min(200/100, 200/100) * (1 - 2*0.05) = 2.0 * 0.9 = 1.8
  HpglDoc doc = makeDoc(0, 0, 100, 100);
  ViewState vs = fitToCanvas(200.f, 200.f, doc, 0.f);
  REQUIRE(nearlyEqual(vs.scale, 1.8f));
}

static void test_wide_doc_constrained_by_width() {
  // doc 0–200 x 0–100, canvas 100×200, no rotation
  // effW=200, effH=100 → scale limited by width: 100/200 * 0.9 = 0.45
  HpglDoc doc = makeDoc(0, 0, 200, 100);
  ViewState vs = fitToCanvas(100.f, 200.f, doc, 0.f);
  REQUIRE(nearlyEqual(vs.scale, 0.45f));
}

static void test_tall_doc_constrained_by_height() {
  // doc 0–100 x 0–200, canvas 200×100 → scale limited by height: 100/200 * 0.9 = 0.45
  HpglDoc doc = makeDoc(0, 0, 100, 200);
  ViewState vs = fitToCanvas(200.f, 100.f, doc, 0.f);
  REQUIRE(nearlyEqual(vs.scale, 0.45f));
}

// ── Pan / centering ───────────────────────────────────────────────────────────

static void test_doc_at_origin_centered_in_canvas() {
  // doc 0–100 x 0–100, canvas 200×200, scale=1.8
  // panX = 100 - (0 + 50)*1.8 = 100 - 90 = 10
  // panY = 100 + (0 + 50)*1.8 = 100 + 90 = 190
  HpglDoc doc = makeDoc(0, 0, 100, 100);
  ViewState vs = fitToCanvas(200.f, 200.f, doc, 0.f);
  REQUIRE(nearlyEqual(vs.panX, 10.f));
  REQUIRE(nearlyEqual(vs.panY, 190.f));
}

static void test_doc_offset_from_origin() {
  // doc 100–200 x 0–100, canvas 200×200
  // scale = 1.8, docW = 100, mid = 150
  // panX = 100 - 150 * 1.8 = 100 - 270 = -170
  // panY = 100 + 50 * 1.8 = 100 + 90 = 190
  HpglDoc doc = makeDoc(100, 0, 200, 100);
  ViewState vs = fitToCanvas(200.f, 200.f, doc, 0.f);
  REQUIRE(nearlyEqual(vs.panX, -170.f));
  REQUIRE(nearlyEqual(vs.panY,  190.f));
}

// ── Rotation ─────────────────────────────────────────────────────────────────

static void test_rotation_90_swaps_effective_dims() {
  // doc 0–200 x 0–50 (wide), canvas 300×300
  // no rotation: effW=200, effH=50 → scale = min(300/200, 300/50) * 0.9 = 1.5 * 0.9 = 1.35
  // 90° rotation: effW=50, effH=200 → scale = min(300/50, 300/200) * 0.9 = 1.5 * 0.9 = 1.35
  // (symmetric in this case, but both should be 1.35)
  HpglDoc doc = makeDoc(0, 0, 200, 50);
  ViewState vs0 = fitToCanvas(300.f, 300.f, doc, 0.f);
  ViewState vs90 = fitToCanvas(300.f, 300.f, doc, static_cast<float>(M_PI_2));
  REQUIRE(nearlyEqual(vs0.scale,  1.35f));
  REQUIRE(nearlyEqual(vs90.scale, 1.35f));
}

static void test_rotation_90_wide_canvas() {
  // doc 0–100 x 0–10 (very wide), 200×200 canvas
  // no rotation:  effW=100, effH=10 → scale = min(200/100, 200/10)*0.9 = 2*0.9=1.8
  // 90° rotation: effW=10,  effH=100 → scale = min(200/10, 200/100)*0.9 = 2*0.9=1.8
  // Still symmetric because canvas is square.
  HpglDoc doc = makeDoc(0, 0, 100, 10);
  ViewState vs0  = fitToCanvas(200.f, 200.f, doc, 0.f);
  ViewState vs90 = fitToCanvas(200.f, 200.f, doc, static_cast<float>(M_PI_2));
  REQUIRE(nearlyEqual(vs0.scale,  1.8f));
  REQUIRE(nearlyEqual(vs90.scale, 1.8f));
}

static void test_rotation_90_non_square_canvas() {
  // doc 0–100 x 0–100, canvas 400×200
  // no rotation:  effW=100, effH=100 → scale = min(400/100, 200/100)*0.9 = 2*0.9=1.8
  // 90° rotation: same effW=100, effH=100 (square doc) → same scale
  HpglDoc doc = makeDoc(0, 0, 100, 100);
  ViewState vs0  = fitToCanvas(400.f, 200.f, doc, 0.f);
  ViewState vs90 = fitToCanvas(400.f, 200.f, doc, static_cast<float>(M_PI_2));
  REQUIRE(nearlyEqual(vs0.scale,  1.8f));
  REQUIRE(nearlyEqual(vs90.scale, 1.8f));
}

// ── Degenerate docs ───────────────────────────────────────────────────────────

static void test_zero_width_doc_uses_min_size() {
  // A degenerate doc with zero width/height should not divide by zero.
  HpglDoc doc;
  doc.minX = 50; doc.maxX = 50; doc.minY = 50; doc.maxY = 50;
  doc.strokes.push_back(Stroke{{{50.f, 50.f}}, 1});
  ViewState vs = fitToCanvas(200.f, 200.f, doc, 0.f);
  // docW/H clamped to 1 → scale = 200/1 * 0.9 = 180
  REQUIRE(nearlyEqual(vs.scale, 180.f));
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
  run("empty doc returns identity",         test_empty_doc_returns_identity);
  run("square doc in square canvas",        test_square_doc_in_square_canvas);
  run("wide doc constrained by width",      test_wide_doc_constrained_by_width);
  run("tall doc constrained by height",     test_tall_doc_constrained_by_height);
  run("doc at origin centered in canvas",   test_doc_at_origin_centered_in_canvas);
  run("doc offset from origin",             test_doc_offset_from_origin);
  run("rotation 90 swaps effective dims",   test_rotation_90_swaps_effective_dims);
  run("rotation 90 wide canvas",            test_rotation_90_wide_canvas);
  run("rotation 90 non-square canvas",      test_rotation_90_non_square_canvas);
  run("zero-width doc uses min size",       test_zero_width_doc_uses_min_size);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
