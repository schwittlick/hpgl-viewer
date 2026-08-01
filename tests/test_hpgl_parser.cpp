#include "../src/hpgl_font.h"
#include "../src/hpgl_parser.h"
#include "test_harness.h"

// ── Helper ────────────────────────────────────────────────────────────────────

static HpglDoc parse(const char *src) { return HpglParser{}.parse(src); }

static bool approx(float a, float b, float eps = 0.05f) {
  return std::fabs(a - b) < eps;
}

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

// ── Labels (LB and the labelling group) ──────────────────────────────────────

// Scale from font design units to plotter units for a given SI width/height
// (centimetres), mirroring what the parser does.
static float labelSX(float wCm) {
  return 1.5f * wCm * kUnitsPerCm / hpgl_font::kCellWidth;
}
// Width of a string as the parser advances the pen, in plotter units.
// Control characters are not plotted and do not advance the pen.
static float labelWidth(const char *text, float wCm) {
  float w = 0;
  for (const char *p = text; *p; ++p)
    if (static_cast<unsigned char>(*p) >= ' ')
      w += hpgl_font::glyph(static_cast<unsigned char>(*p)).advance;
  return w * labelSX(wCm);
}

static void test_lb_draws_glyph_strokes() {
  // 'H' is three strokes in the stick font: two stems and the crossbar.
  auto doc = parse("PA1000,2000;LBH\x03");
  REQUIRE(doc.strokes.size() ==
          static_cast<size_t>(hpgl_font::glyph('H').strokeCount));
  REQUIRE(!doc.empty());
}

static void test_lb_sits_on_the_baseline_at_the_current_position() {
  // Default character size is 0.19 × 0.27 cm; 'H' spans the full cap height
  // starting at the current pen position.
  auto doc = parse("PA1000,2000;LBH\x03");
  REQUIRE(approx(doc.minY, 2000.f));
  REQUIRE(approx(doc.maxY, 2000.f + kDefaultCharH));
  REQUIRE(doc.minX > 1000.f); // left side bearing
}

static void test_si_sets_character_height() {
  auto doc = parse("SI0.5,1.0;PA0,0;LBH\x03");
  REQUIRE(approx(doc.maxY - doc.minY, 1.0f * kUnitsPerCm));
}

static void test_si_without_params_restores_default_size() {
  auto doc = parse("SI0.5,1.0;SI;PA0,0;LBH\x03");
  REQUIRE(approx(doc.maxY - doc.minY, kDefaultCharH));
}

static void test_lb_advances_pen_by_glyph_widths() {
  // After a label the pen sits where the next character would start, so a
  // bare PD drops a point exactly one label width along.
  auto doc = parse("SI0.5,1.0;PA0,0;LBABC\x03PD;");
  REQUIRE(!doc.strokes.empty());
  auto &end = doc.strokes.back().points;
  REQUIRE(end.size() == 1);
  REQUIRE(approx(end[0].x, labelWidth("ABC", 0.5f)));
  REQUIRE(approx(end[0].y, 0.f));
}

static void test_lb_space_advances_without_drawing() {
  auto doc = parse("SI0.5,1.0;PA0,0;LB  \x03PD;");
  REQUIRE(doc.strokes.size() == 1); // only the PD point
  REQUIRE(approx(doc.strokes[0].points[0].x, labelWidth("  ", 0.5f)));
}

static void test_lb_semicolon_is_label_text_not_a_separator() {
  auto doc = parse("SI0.5,1.0;PA0,0;LBa;b\x03PD;");
  REQUIRE(approx(doc.strokes.back().points[0].x, labelWidth("a;b", 0.5f)));
}

static void test_lb_resumes_command_parsing_after_terminator() {
  auto doc = parse("PA0,0;LBA\x03PU5000,5000;PD6000,5000;");
  auto &last = doc.strokes.back().points;
  REQUIRE(last.size() == 2);
  REQUIRE((last[0] == Vec2{5000, 5000}));
  REQUIRE((last[1] == Vec2{6000, 5000}));
}

static void test_lb_without_terminator_consumes_rest_of_file() {
  auto doc = parse("PA0,0;LBAB");
  REQUIRE(!doc.empty());
}

static void test_dt_redefines_the_label_terminator() {
  // '#' now ends the label, so the ETX no longer does: the label runs on to
  // "cd" instead of stopping after "ab".
  auto doc = parse("SI0.5,1.0;DT#;PA0,0;LBab\x03" "cd#PD;");
  REQUIRE(approx(doc.strokes.back().points[0].x, labelWidth("abcd", 0.5f)));
}

static void test_dt_without_params_restores_etx() {
  auto doc = parse("SI0.5,1.0;DT#;DT;PA0,0;LBab\x03PD;");
  REQUIRE(approx(doc.strokes.back().points[0].x, labelWidth("ab", 0.5f)));
}

static void test_lo5_centres_the_label_on_the_current_position() {
  auto doc = parse("SI0.5,1.0;LO5;PA0,0;LBH\x03");
  const float h = 1.0f * kUnitsPerCm;
  REQUIRE(approx(doc.minY, -h / 2));
  REQUIRE(approx(doc.maxY, h / 2));
  // Horizontally centred on the label's advance width.
  const float w = labelWidth("H", 0.5f);
  REQUIRE(approx(doc.minX, -w / 2 + hpgl_font::glyph('H').strokes[0].xy[0] *
                                        labelSX(0.5f)));
}

static void test_lo3_hangs_the_label_below_the_current_position() {
  auto doc = parse("SI0.5,1.0;LO3;PA0,0;LBH\x03");
  REQUIRE(approx(doc.maxY, 0.f));
  REQUIRE(approx(doc.minY, -1.0f * kUnitsPerCm));
  REQUIRE(doc.minX > 0.f); // still left-aligned
}

static void test_lo7_right_aligns_the_label() {
  auto doc = parse("SI0.5,1.0;LO7;PA0,0;LBH\x03");
  REQUIRE(approx(doc.minY, 0.f));      // bottom-anchored like LO1
  REQUIRE(doc.maxX < 0.f);             // whole label left of the origin
  REQUIRE(approx(doc.maxX, -labelWidth("H", 0.5f) +
                               hpgl_font::glyph('H').strokes[1].xy[0] *
                                   labelSX(0.5f)));
}

static void test_lo_out_of_range_falls_back_to_lo1() {
  auto doc = parse("SI0.5,1.0;LO99;PA0,0;LBH\x03");
  REQUIRE(approx(doc.minY, 0.f));
  REQUIRE(doc.minX > 0.f);
}

static void test_di_rotates_the_label() {
  // DI0,1 runs the text upwards, so the cap height extends to -x.
  auto doc = parse("SI0.5,1.0;DI0,1;PA0,0;LBH\x03");
  REQUIRE(approx(doc.minX, -1.0f * kUnitsPerCm)); // cap height now runs -x
  REQUIRE(approx(doc.maxX, 0.f));                 // baseline stays at x = 0
  REQUIRE(doc.minY > 0.f);                        // advance runs along +y
}

static void test_di_without_params_restores_horizontal_text() {
  auto doc = parse("SI0.5,1.0;DI0,1;DI;PA0,0;LBH\x03");
  REQUIRE(approx(doc.maxY, 1.0f * kUnitsPerCm));
  REQUIRE(doc.minX > 0.f);
}

static void test_sl_slants_the_glyph_top_forward() {
  auto upright = parse("SI0.5,1.0;PA0,0;LBI\x03");
  auto slanted = parse("SI0.5,1.0;SL1;PA0,0;LBI\x03");
  // 'I' is a single vertical stem; slanting shifts its top by the height.
  auto &s = slanted.strokes[0].points;
  const float dx = std::fabs(s.front().x - s.back().x);
  REQUIRE(approx(dx, 1.0f * kUnitsPerCm));
  REQUIRE(approx(upright.strokes[0].points.front().x,
                 upright.strokes[0].points.back().x));
}

static void test_lb_newline_starts_a_new_line() {
  // Line spacing is two character heights; LO1 anchors the bottom line.
  auto doc = parse("SI0.5,1.0;PA0,0;LBH\nH\x03");
  const float h = 1.0f * kUnitsPerCm;
  REQUIRE(approx(doc.minY, 0.f));
  REQUIRE(approx(doc.maxY, 3 * h)); // 2h line feed + one cap height
  REQUIRE(doc.strokes.size() ==
          2 * static_cast<size_t>(hpgl_font::glyph('H').strokeCount));
}

static void test_lb_carriage_return_returns_to_line_start() {
  auto doc = parse("SI0.5,1.0;PA0,0;LBHHH\rH\x03");
  // The overtyped H must land back at the first H's position.
  REQUIRE(approx(doc.minX, doc.strokes.back().bboxMin.x));
}

static void test_lb_ignores_unprintable_control_characters() {
  auto plain = parse("SI0.5,1.0;PA0,0;LBA\x03PD;");
  auto noisy = parse("SI0.5,1.0;PA0,0;LB\x01"
                     "A\x02\x03PD;");
  REQUIRE(plain.strokes.size() == noisy.strokes.size());
  REQUIRE(approx(plain.strokes.back().points[0].x,
                 noisy.strokes.back().points[0].x));
}

static void test_lb_uses_the_current_pen() {
  auto doc = parse("SP4;PA0,0;LBH\x03");
  REQUIRE(!doc.empty());
  for (auto &s : doc.strokes)
    REQUIRE(s.pen == 4);
}

static void test_lb_does_not_extend_a_pen_down_stroke() {
  // A label drawn while the pen is down must not be appended to the stroke
  // in progress, and the following PA must start a fresh stroke.
  auto doc = parse("PD;PA1000,0;LBH\x03PA2000,0;");
  REQUIRE(doc.strokes.front().points.size() == 2); // origin → 1000,0
  REQUIRE(doc.strokes.back().points.back() == (Vec2{2000, 0}));
}

static void test_cp_moves_the_pen_by_character_cells() {
  // Two cells right (1.5 × character width each) and one line down.
  auto doc = parse("SI0.5,1.0;PA0,0;CP2,-1;PD;");
  auto &p = doc.strokes.back().points[0];
  REQUIRE(approx(p.x, 2 * 1.5f * 0.5f * kUnitsPerCm));
  REQUIRE(approx(p.y, -2 * 1.0f * kUnitsPerCm));
}

static void test_cp_without_params_is_a_carriage_return_line_feed() {
  auto doc = parse("SI0.5,1.0;PA0,0;CP;PD;");
  auto &p = doc.strokes.back().points[0];
  REQUIRE(approx(p.x, 0.f));
  REQUIRE(approx(p.y, -2 * 1.0f * kUnitsPerCm));
}

static void test_in_resets_the_label_state() {
  auto doc = parse("SI1.0,2.0;LO5;DI0,1;IN;PA0,0;LBH\x03");
  REQUIRE(approx(doc.minY, 0.f));                     // LO back to 1
  REQUIRE(approx(doc.maxY, kDefaultCharH));           // SI back to default
  REQUIRE(doc.minX > 0.f);                            // DI back to horizontal
}

static void test_parser_reuse_resets_label_state() {
  HpglParser p;
  p.parse("SI1.0,2.0;DT#;LO5;");
  auto doc = p.parse("PA0,0;LBH\x03");
  REQUIRE(approx(doc.minY, 0.f));
  REQUIRE(approx(doc.maxY, kDefaultCharH));
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
  run("LB draws glyph strokes",             test_lb_draws_glyph_strokes);
  run("LB sits on baseline at position",    test_lb_sits_on_the_baseline_at_the_current_position);
  run("SI sets character height",           test_si_sets_character_height);
  run("SI no params restores default",      test_si_without_params_restores_default_size);
  run("LB advances pen by glyph widths",    test_lb_advances_pen_by_glyph_widths);
  run("LB space advances without drawing",  test_lb_space_advances_without_drawing);
  run("LB semicolon is label text",         test_lb_semicolon_is_label_text_not_a_separator);
  run("LB resumes parsing after terminator",test_lb_resumes_command_parsing_after_terminator);
  run("LB unterminated consumes rest",      test_lb_without_terminator_consumes_rest_of_file);
  run("DT redefines label terminator",      test_dt_redefines_the_label_terminator);
  run("DT no params restores ETX",          test_dt_without_params_restores_etx);
  run("LO5 centres label on position",      test_lo5_centres_the_label_on_the_current_position);
  run("LO3 hangs label below position",     test_lo3_hangs_the_label_below_the_current_position);
  run("LO7 right-aligns label",             test_lo7_right_aligns_the_label);
  run("LO out of range falls back to 1",    test_lo_out_of_range_falls_back_to_lo1);
  run("DI rotates label",                   test_di_rotates_the_label);
  run("DI no params restores horizontal",   test_di_without_params_restores_horizontal_text);
  run("SL slants glyph top forward",        test_sl_slants_the_glyph_top_forward);
  run("LB newline starts new line",         test_lb_newline_starts_a_new_line);
  run("LB CR returns to line start",        test_lb_carriage_return_returns_to_line_start);
  run("LB ignores control characters",      test_lb_ignores_unprintable_control_characters);
  run("LB uses current pen",                test_lb_uses_the_current_pen);
  run("LB does not extend pen-down stroke", test_lb_does_not_extend_a_pen_down_stroke);
  run("CP moves pen by character cells",    test_cp_moves_the_pen_by_character_cells);
  run("CP no params is CR+LF",              test_cp_without_params_is_a_carriage_return_line_feed);
  run("IN resets label state",              test_in_resets_the_label_state);
  run("parser reuse resets label state",    test_parser_reuse_resets_label_state);
  run("progress reaches 1.0 after parse",   test_progress_reaches_one_after_parse);
  run("progress null pointer no crash",     test_progress_null_pointer_does_not_crash);
  run("progress empty input reaches 1.0",   test_progress_empty_input_reaches_one);
  run("progress increases on large input",  test_progress_increases_on_large_input);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
