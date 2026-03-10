#include "../src/hpgl_parser.h"

#include <cstdio>

// ── Minimal test harness ──────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;
static const char *g_test = "";

#define REQUIRE(expr)                                                           \
  do {                                                                          \
    if (expr) {                                                                 \
      ++g_pass;                                                                 \
    } else {                                                                    \
      ++g_fail;                                                                 \
      fprintf(stderr, "  FAIL  [%s]  line %d:  %s\n", g_test, __LINE__, #expr);\
    }                                                                           \
  } while (0)

static void run(const char *name, void (*fn)()) {
  g_test = name;
  fn();
}

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

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
