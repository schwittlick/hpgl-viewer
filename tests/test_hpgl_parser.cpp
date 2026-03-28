#include "../src/hpgl_parser.h"
#include "test_harness.h"

// ── Helper ────────────────────────────────────────────────────────────────────

static HpglDoc parse(const char *src) { return HpglParser{}.parse(src); }

// ── Tests ─────────────────────────────────────────────────────────────────────

// Empty / degenerate input

static void test_empty_string_is_empty() {
  REQUIRE(parse("").empty());
}

static void test_whitespace_only_is_empty() {
  REQUIRE(parse("   \n\t  ").empty());
}

static void test_empty_doc_has_fallback_bounds() {
  auto doc = parse("");
  REQUIRE(doc.minX == 0 && doc.minY == 0);
  REQUIRE(doc.maxX == 1 && doc.maxY == 1);
}

// PD / PU basics

static void test_pd_with_coords_produces_stroke_from_origin() {
  auto doc = parse("PD100,200;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 2);
  REQUIRE((pts[0] == Vec2{0, 0}));
  REQUIRE((pts[1] == Vec2{100, 200}));
}

static void test_pd_then_pa_draws_line() {
  auto doc = parse("PD;PA100,200;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 2);
  REQUIRE((pts[0] == Vec2{0, 0}));
  REQUIRE((pts[1] == Vec2{100, 200}));
}

static void test_pu_with_coords_moves_pen() {
  // PU500,600 should position the pen so the next PD anchors at 500,600.
  auto doc = parse("PU500,600;PD;PA100,200;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 2);
  REQUIRE((pts[0] == Vec2{500, 600}));
  REQUIRE((pts[1] == Vec2{100, 200}));
}

static void test_pd_multiple_coord_pairs() {
  auto doc = parse("PD100,100,200,200,300,300;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 4); // origin + 3 destinations
  REQUIRE((pts[1] == Vec2{100, 100}));
  REQUIRE((pts[2] == Vec2{200, 200}));
  REQUIRE((pts[3] == Vec2{300, 300}));
}

// PA

static void test_pa_pen_up_creates_no_stroke() {
  REQUIRE(parse("PA100,200;").empty());
}

static void test_pa_pen_up_updates_position() {
  // Regression: PA pen-up must update cx/cy so the next PD anchors correctly.
  auto doc = parse("PA500,600;PD;PA100,200;PU;");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE((doc.strokes[0].points[0] == Vec2{500, 600}));
}

static void test_pa_pen_down_draws_from_current_pos() {
  auto doc = parse("PD;PA100,0,200,0;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 3);
  REQUIRE((pts[0] == Vec2{0, 0}));
  REQUIRE((pts[1] == Vec2{100, 0}));
  REQUIRE((pts[2] == Vec2{200, 0}));
}

// SP — pen selection

static void test_sp_selects_pen() {
  auto doc = parse("SP3;PD100,100;PU;");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE(doc.strokes[0].pen == 3);
}

static void test_pen_change_mid_draw_creates_new_stroke() {
  auto doc = parse("PD100,0;SP2;PA200,0;PU;");
  REQUIRE(doc.strokes.size() == 2);
  REQUIRE(doc.strokes[0].pen == 1);
  REQUIRE(doc.strokes[1].pen == 2);
}

// Bounds

static void test_bounds_cover_all_drawn_points() {
  auto doc = parse("PD100,200,300,400;PU;");
  REQUIRE(doc.minX == 0 && doc.minY == 0); // origin anchor
  REQUIRE(doc.maxX == 300 && doc.maxY == 400);
}

static void test_pa_pen_down_updates_bounds() {
  auto doc = parse("PU100,100;PD;PA500,800;PU;");
  REQUIRE(doc.minX == 100 && doc.minY == 100);
  REQUIRE(doc.maxX == 500 && doc.maxY == 800);
}

// Robustness

static void test_lowercase_commands_accepted() {
  REQUIRE(parse("pd100,200;pu;").strokes.size() == 1);
}

static void test_unknown_commands_skipped() {
  REQUIRE(parse("IN;VS10;PD100,100;PU;").strokes.size() == 1);
}

static void test_parser_reuse() {
  HpglParser p;
  auto doc1 = p.parse("PD100,100;PU;");
  auto doc2 = p.parse("PD200,200;PU;");
  REQUIRE(doc1.strokes.size() == 1);
  REQUIRE(doc2.strokes.size() == 1);
  REQUIRE((doc2.strokes[0].points.back() == Vec2{200, 200}));
}

// Multiple strokes / pen-up gaps
// (These matter for pen-up move rendering: last point of stroke[i] →
//  first point of stroke[i+1] is what gets drawn as a pen-up line.)

static void test_two_strokes_have_correct_gap_endpoints() {
  // Stroke 1 ends at 100,0; stroke 2 starts at 200,0.
  auto doc = parse("PD100,0;PU200,0;PD300,0;PU;");
  REQUIRE(doc.strokes.size() == 2);
  REQUIRE((doc.strokes[0].points.back()  == Vec2{100, 0}));
  REQUIRE((doc.strokes[1].points.front() == Vec2{200, 0}));
}

static void test_three_strokes_gap_chain() {
  auto doc = parse("PD10,0;PU20,0;PD30,0;PU40,0;PD50,0;PU;");
  REQUIRE(doc.strokes.size() == 3);
  REQUIRE((doc.strokes[0].points.back()  == Vec2{10, 0}));
  REQUIRE((doc.strokes[1].points.front() == Vec2{20, 0}));
  REQUIRE((doc.strokes[1].points.back()  == Vec2{30, 0}));
  REQUIRE((doc.strokes[2].points.front() == Vec2{40, 0}));
}

static void test_pu_no_coords_does_not_move_pen() {
  // PU with no coords lifts the pen but position stays.
  auto doc = parse("PD100,0;PU;PD200,0;PU;");
  REQUIRE(doc.strokes.size() == 2);
  REQUIRE((doc.strokes[1].points.front() == Vec2{100, 0}));
}

// Single-point / dot strokes

static void test_pd_no_coords_creates_single_point_stroke() {
  // PD with no coords starts a stroke at current position.
  // The stroke has just the anchor point (no destination).
  auto doc = parse("PU50,50;PD;PU;");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE(doc.strokes[0].points.size() == 1);
  REQUIRE((doc.strokes[0].points[0] == Vec2{50, 50}));
}

// Floating-point coordinates

static void test_float_coordinates_parsed() {
  auto doc = parse("PD12.5,34.75;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  REQUIRE(pts.size() == 2);
  REQUIRE((pts[1] == Vec2{12.5f, 34.75f}));
}

static void test_negative_coordinates() {
  auto doc = parse("PU-100,-200;PD0,0;PU;");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE((doc.strokes[0].points.front() == Vec2{-100, -200}));
  REQUIRE(doc.minX == -100 && doc.minY == -200);
}

// Bounds with pen-up moves (pen-up moves must NOT expand bounds)

static void test_pu_move_does_not_expand_bounds() {
  // PU travel to 9999,9999 that never becomes a stroke anchor must not
  // expand bounds. The pen lifts, moves to 9999,9999, then moves back to
  // 0,0 before going down — so only (0,0)..(100,100) should be in bounds.
  auto doc = parse("PU9999,9999;PU0,0;PD100,100;PU;");
  REQUIRE(doc.maxX == 100 && doc.maxY == 100);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
  run("empty string is empty",              test_empty_string_is_empty);
  run("whitespace-only is empty",           test_whitespace_only_is_empty);
  run("empty doc has fallback bounds",      test_empty_doc_has_fallback_bounds);
  run("PD with coords from origin",         test_pd_with_coords_produces_stroke_from_origin);
  run("PD then PA draws line",              test_pd_then_pa_draws_line);
  run("PU with coords moves pen",           test_pu_with_coords_moves_pen);
  run("PD multiple coord pairs",            test_pd_multiple_coord_pairs);
  run("PA pen-up creates no stroke",        test_pa_pen_up_creates_no_stroke);
  run("PA pen-up updates position",         test_pa_pen_up_updates_position);
  run("PA pen-down draws from current pos", test_pa_pen_down_draws_from_current_pos);
  run("SP selects pen",                     test_sp_selects_pen);
  run("pen change creates new stroke",      test_pen_change_mid_draw_creates_new_stroke);
  run("bounds cover all drawn points",      test_bounds_cover_all_drawn_points);
  run("PA pen-down updates bounds",         test_pa_pen_down_updates_bounds);
  run("lowercase commands accepted",        test_lowercase_commands_accepted);
  run("unknown commands skipped",           test_unknown_commands_skipped);
  run("parser reuse",                       test_parser_reuse);
  run("two strokes gap endpoints",          test_two_strokes_have_correct_gap_endpoints);
  run("three strokes gap chain",            test_three_strokes_gap_chain);
  run("PU no coords does not move pen",     test_pu_no_coords_does_not_move_pen);
  run("PD no coords single-point stroke",   test_pd_no_coords_creates_single_point_stroke);
  run("float coordinates parsed",           test_float_coordinates_parsed);
  run("negative coordinates",               test_negative_coordinates);
  run("PU move does not expand bounds",     test_pu_move_does_not_expand_bounds);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
