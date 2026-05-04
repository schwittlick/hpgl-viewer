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

// Odd number of coordinates — trailing value silently dropped

static void test_pd_odd_coords_drops_last() {
  // PD100,200,300 — three values: pair (100,200) used, lone 300 dropped.
  auto doc = parse("PD100,200,300;PU;");
  REQUIRE(doc.strokes.size() == 1);
  auto &pts = doc.strokes[0].points;
  // origin anchor + one destination; 300 is silently discarded
  REQUIRE(pts.size() == 2);
  REQUIRE((pts[1] == Vec2{100.f, 200.f}));
}

// Non-numeric token — silently skipped

static void test_pd_bad_token_skipped() {
  // "1a2" is not a valid float — stof throws, token is dropped.
  auto doc = parse("PD1a2,300;PU;");
  // Only one valid value (300), which is also dropped (odd count after bad token).
  // Regardless of exact result, the parser must not crash.
  (void)doc; // just confirm it doesn't throw/crash
  REQUIRE(true);
}

// SP with no argument — pen unchanged

static void test_sp_no_arg_leaves_pen_unchanged() {
  auto doc = parse("SP3;SP;PD100,0;PU;");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE(doc.strokes[0].pen == 3); // bare SP; does not reset pen
}

// SP0 (park) — treated as pen 0, resets cur stroke

static void test_sp0_resets_current_stroke() {
  // After SP0 a new PD must open a fresh stroke with pen 0.
  auto doc = parse("SP1;PD100,0;SP0;PD200,0;PU;");
  REQUIRE(doc.strokes.size() == 2);
  REQUIRE(doc.strokes[0].pen == 1);
  REQUIRE(doc.strokes[1].pen == 0);
}

// Input without terminating semicolon on last command

static void test_no_trailing_semicolon_parsed() {
  // "PD100,200" with no closing semicolon — params reach end-of-string.
  auto doc = parse("PD100,200");
  REQUIRE(doc.strokes.size() == 1);
  REQUIRE((doc.strokes[0].points.back() == Vec2{100.f, 200.f}));
}

// ── Progress reporting ───────────────────────────────────────────────────────

static void test_progress_reaches_one_after_parse() {
  std::atomic<float> progress{0.0f};
  HpglParser{}.parse("PD100,200;PA0,0,50,50;", &progress);
  REQUIRE(progress.load() == 1.0f);
}

static void test_progress_null_pointer_does_not_crash() {
  // Parser must accept nullptr for the progress argument (default arg path).
  HpglDoc doc = HpglParser{}.parse("PD100,200;", nullptr);
  REQUIRE(doc.strokes.size() == 1);
}

static void test_progress_empty_input_reaches_one() {
  // Even with empty input the parser must mark progress complete on return.
  std::atomic<float> progress{0.0f};
  HpglParser{}.parse("", &progress);
  REQUIRE(progress.load() == 1.0f);
}

static void test_progress_increases_on_large_input() {
  // Build a >256 KB input — well past the 64 KB stride — so the in-loop
  // updater fires multiple times.  We can't observe intermediate values
  // synchronously, but we can verify the final value is 1.0 and that the
  // parser handled the volume correctly (every PD produces one stroke).
  std::string content;
  content.reserve(300 * 1024);
  int strokes = 0;
  while (content.size() < 256 * 1024) {
    content += "PD100,200;PU;";
    ++strokes;
  }
  std::atomic<float> progress{0.0f};
  HpglDoc doc = HpglParser{}.parse(content, &progress);
  REQUIRE(progress.load() == 1.0f);
  REQUIRE(static_cast<int>(doc.strokes.size()) == strokes);
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
  run("PD odd coords drops trailing value", test_pd_odd_coords_drops_last);
  run("PD bad token silently skipped",      test_pd_bad_token_skipped);
  run("SP no arg leaves pen unchanged",     test_sp_no_arg_leaves_pen_unchanged);
  run("SP0 resets current stroke",          test_sp0_resets_current_stroke);
  run("no trailing semicolon parsed",       test_no_trailing_semicolon_parsed);
  run("progress reaches 1.0 after parse",   test_progress_reaches_one_after_parse);
  run("progress null pointer no crash",     test_progress_null_pointer_does_not_crash);
  run("progress empty input reaches 1.0",   test_progress_empty_input_reaches_one);
  run("progress increases on large input",  test_progress_increases_on_large_input);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
