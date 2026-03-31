#include "../src/renderer.h"
#include "test_harness.h"

#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool near(float a, float b, float eps = 1e-3f) {
  return fabsf(a - b) < eps;
}

static bool nearVec(ImVec2 a, ImVec2 b, float eps = 1e-3f) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps);
}

// ── unrotateCanvas ────────────────────────────────────────────────────────────

static void test_unrotate_identity_no_rotation() {
  // cosR=1, sinR=0 → output equals input
  REQUIRE(nearVec(unrotateCanvas(30.f, 70.f, 200.f, 100.f, 1.f, 0.f),
                  {30.f, 70.f}));
}

static void test_unrotate_center_is_invariant() {
  // The centre of the canvas maps to itself regardless of rotation angle.
  float cW = 200.f, cH = 100.f;
  float cx = cW * 0.5f, cy = cH * 0.5f;
  float angle = static_cast<float>(M_PI) / 3.f; // 60°
  float cosR = cosf(angle), sinR = sinf(angle);
  REQUIRE(nearVec(unrotateCanvas(cx, cy, cW, cH, cosR, sinR), {cx, cy}));
}

static void test_unrotate_90deg() {
  // 90° rotation, canvas 200×100, point (150, 75):
  //   u = 150 - 100 = 50,  v = 75 - 50 = 25
  //   rx = u*cos(90) + v*sin(90) + 100 = 0 + 25 + 100 = 125
  //   ry = -u*sin(90) + v*cos(90) + 50 = -50 +  0 + 50 =   0
  float cosR = 0.f, sinR = 1.f; // cos/sin of 90°
  REQUIRE(nearVec(unrotateCanvas(150.f, 75.f, 200.f, 100.f, cosR, sinR),
                  {125.f, 0.f}));
}

static void test_unrotate_180deg() {
  // 180°: cosR=-1, sinR=0
  //   u = 30-100=-70, v=70-50=20
  //   rx = (-70)*(-1) + 20*0 + 100 = 70+100 = 170
  //   ry = -(-70)*0 + 20*(-1) + 50 = -20+50 = 30
  float cosR = -1.f, sinR = 0.f;
  REQUIRE(nearVec(unrotateCanvas(30.f, 70.f, 200.f, 100.f, cosR, sinR),
                  {170.f, 30.f}));
}

static void test_unrotate_is_self_inverse_at_0() {
  // Applying unrotate twice with the same (trivial) rotation gives identity.
  float cW = 300.f, cH = 200.f;
  ImVec2 p = {80.f, 140.f};
  auto once = unrotateCanvas(p.x, p.y, cW, cH, 1.f, 0.f);
  auto twice = unrotateCanvas(once.x, once.y, cW, cH, 1.f, 0.f);
  REQUIRE(nearVec(twice, p));
}

// ── xfPoint ───────────────────────────────────────────────────────────────────

static void test_xf_center_hpgl_maps_to_canvas_center() {
  // doc 0–100 x 0–100, scale=1.8, pan computed by fitToCanvas (10,10),
  // no rotation, origin at (0,0), canvas 200×200.
  // HPGL centre (50,50) should map to canvas centre (100,100).
  ImVec2 origin{0.f, 0.f};
  REQUIRE(nearVec(xfPoint(50.f, 50.f, origin, 10.f, 10.f, 1.8f,
                          200.f, 200.f, 1.f, 0.f),
                  {100.f, 100.f}));
}

static void test_xf_scale_doubles_distance_from_center() {
  // With scale=2 and pan chosen so HPGL origin → canvas origin,
  // a point 10 HPGL units to the right should be 20 px to the right.
  // panX = cW*0.5 - 0 = 100, panY = cH*0.5 - 0 = 100 (HPGL origin at canvas centre)
  // xfPoint(10,0) → sx = 10*2+100-100=20, sy=0+100-100=0 → (0+20+100, 0+0+100)=(120,100)
  ImVec2 origin{0.f, 0.f};
  ImVec2 a = xfPoint( 0.f, 0.f, origin, 100.f, 100.f, 2.f, 200.f, 200.f, 1.f, 0.f);
  ImVec2 b = xfPoint(10.f, 0.f, origin, 100.f, 100.f, 2.f, 200.f, 200.f, 1.f, 0.f);
  REQUIRE(near(b.x - a.x, 20.f));
  REQUIRE(near(b.y - a.y,  0.f));
}

static void test_xf_origin_offset_shifts_result() {
  // Moving the canvas origin by (50,30) shifts the screen result by the same amount.
  ImVec2 origin0{  0.f,  0.f};
  ImVec2 origin1{ 50.f, 30.f};
  ImVec2 p0 = xfPoint(40.f, 60.f, origin0, 0.f, 0.f, 1.f, 200.f, 200.f, 1.f, 0.f);
  ImVec2 p1 = xfPoint(40.f, 60.f, origin1, 0.f, 0.f, 1.f, 200.f, 200.f, 1.f, 0.f);
  REQUIRE(near(p1.x - p0.x, 50.f));
  REQUIRE(near(p1.y - p0.y, 30.f));
}

// ── Round-trip: xfPoint then unrotateCanvas recovers the HPGL coords ──────────
// Algebraically proven: unrotate ∘ xfPoint ≡ identity (up to pan/scale).

static void run_roundtrip(float hx, float hy,
                          float panX, float panY, float scale,
                          float cW, float cH, float rotation) {
  ImVec2 origin{20.f, 15.f};
  float cosR = cosf(rotation), sinR = sinf(rotation);

  ImVec2 screen = xfPoint(hx, hy, origin,
                          panX, panY, scale, cW, cH, cosR, sinR);
  // mouse pos relative to canvas
  float mx = screen.x - origin.x;
  float my = screen.y - origin.y;
  ImVec2 unrot = unrotateCanvas(mx, my, cW, cH, cosR, sinR);

  float recovered_x = (unrot.x - panX) / scale;
  float recovered_y = (unrot.y - panY) / scale;

  REQUIRE(near(recovered_x, hx));
  REQUIRE(near(recovered_y, hy));
}

static void test_roundtrip_no_rotation() {
  run_roundtrip(123.f, 456.f, 10.f, 10.f, 1.8f, 400.f, 300.f, 0.f);
}

static void test_roundtrip_45deg_rotation() {
  run_roundtrip(200.f, 100.f, 50.f, 80.f, 2.0f, 400.f, 400.f,
                static_cast<float>(M_PI_4));
}

static void test_roundtrip_90deg_rotation() {
  run_roundtrip(300.f, 150.f, 20.f, 60.f, 1.5f, 600.f, 400.f,
                static_cast<float>(M_PI_2));
}

static void test_roundtrip_negative_coords() {
  run_roundtrip(-50.f, -80.f, 200.f, 150.f, 1.0f, 300.f, 300.f, 0.f);
}

// ── initPenColors ─────────────────────────────────────────────────────────────

static void test_init_pen_colors_pen1_is_red() {
  PenStyle pens[10];
  initPenColors(pens);
  // Pen 1 (index 0) should be red dominant
  REQUIRE(pens[0].color.x > 0.5f);
  REQUIRE(pens[0].color.y < 0.3f);
  REQUIRE(pens[0].color.z < 0.3f);
  REQUIRE(pens[0].color.w == 1.0f);
}

static void test_init_pen_colors_pen6_is_dark() {
  PenStyle pens[10];
  initPenColors(pens);
  // Pen 6 (index 5) should be near-black
  REQUIRE(pens[5].color.x < 0.2f);
  REQUIRE(pens[5].color.y < 0.2f);
  REQUIRE(pens[5].color.z < 0.2f);
}

static void test_init_pen_colors_all_ten_distinct() {
  PenStyle pens[10];
  initPenColors(pens);
  // Verify each pen has the expected dominant channel (the 10 hardcoded colors).
  // Index: 0=red, 1=green, 2=blue, 3=orange, 4=brown, 5=black, 6=yellow, 7=purple, 8=light-blue, 9=magenta
  REQUIRE(pens[0].color.x > 0.5f && pens[0].color.y < 0.3f && pens[0].color.z < 0.3f); // red
  REQUIRE(pens[1].color.y > 0.4f && pens[1].color.x < 0.3f && pens[1].color.z < 0.3f); // green
  REQUIRE(pens[2].color.z > 0.4f && pens[2].color.x < 0.3f && pens[2].color.y < 0.3f); // blue
  REQUIRE(pens[3].color.x > 0.5f && pens[3].color.y > 0.3f && pens[3].color.z < 0.3f); // orange (R>G>B)
  REQUIRE(pens[4].color.x > pens[4].color.z && pens[4].color.y > pens[4].color.z);      // brown (warm)
  REQUIRE(pens[5].color.x < 0.1f && pens[5].color.y < 0.1f && pens[5].color.z < 0.1f); // black
  REQUIRE(pens[6].color.x > 0.5f && pens[6].color.y > 0.5f && pens[6].color.z < 0.3f); // yellow
  REQUIRE(pens[7].color.z > 0.3f && pens[7].color.x > 0.0f && pens[7].color.y < 0.2f); // purple
  REQUIRE(pens[8].color.z > 0.5f && pens[8].color.x < pens[8].color.z);                 // light blue
  REQUIRE(pens[9].color.x > 0.5f && pens[9].color.z > 0.5f && pens[9].color.y < 0.3f); // magenta
}

static void test_init_pen_colors_all_fully_opaque() {
  PenStyle pens[10];
  initPenColors(pens);
  for (int i = 0; i < 10; ++i)
    REQUIRE(pens[i].color.w == 1.0f);
}

static void test_init_pen_colors_default_thickness() {
  PenStyle pens[10];
  initPenColors(pens);
  for (int i = 0; i < 10; ++i)
    REQUIRE(pens[i].thickness == 0.3f);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
  run("unrotate identity no rotation",    test_unrotate_identity_no_rotation);
  run("unrotate center is invariant",     test_unrotate_center_is_invariant);
  run("unrotate 90°",                     test_unrotate_90deg);
  run("unrotate 180°",                    test_unrotate_180deg);
  run("unrotate self-inverse at 0°",      test_unrotate_is_self_inverse_at_0);
  run("xf center HPGL → canvas center",  test_xf_center_hpgl_maps_to_canvas_center);
  run("xf scale doubles distance",        test_xf_scale_doubles_distance_from_center);
  run("xf origin offset shifts result",   test_xf_origin_offset_shifts_result);
  run("roundtrip no rotation",            test_roundtrip_no_rotation);
  run("roundtrip 45° rotation",           test_roundtrip_45deg_rotation);
  run("roundtrip 90° rotation",           test_roundtrip_90deg_rotation);
  run("roundtrip negative coords",        test_roundtrip_negative_coords);
  run("initPenColors pen1 is red",        test_init_pen_colors_pen1_is_red);
  run("initPenColors pen6 is dark",       test_init_pen_colors_pen6_is_dark);
  run("initPenColors all 10 distinct",    test_init_pen_colors_all_ten_distinct);
  run("initPenColors all opaque",         test_init_pen_colors_all_fully_opaque);
  run("initPenColors default thickness",  test_init_pen_colors_default_thickness);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
