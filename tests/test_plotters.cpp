#include "../src/plotters.h"
#include "test_harness.h"

#include <cmath>
#include <string>

static std::string g_registryPath; // argv[1] — the bundled data/plotters.json

static bool nearlyEqual(float a, float b, float eps = 1e-3f) {
  return fabsf(a - b) < eps;
}

// A minimal but complete entry, used as the base for most parsing tests.
static const char *kOnePlotter = R"json({
  "schema_version": 1,
  "plotters": [
    {
      "id": "TEST_A1",
      "name": "test_a1",
      "hpgl_model": "7595A",
      "default_for_model": true,
      "max_area": { "x": -100, "y": -50, "x2": 100, "y2": 50 },
      "margin": { "left": 10, "bottom": 2, "right": 4, "top": 6 },
      "units_per_mm": { "x": 40, "y": 40 },
      "paper": {
        "size_mm": { "w": 10, "h": 5 },
        "offset_mm": { "left": 1.5, "bottom": 0.25 },
        "verified": true
      },
      "note": "hello"
    }
  ]
})json";

// ── Parsing ───────────────────────────────────────────────────────────────────

static void test_parses_a_full_entry() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(kOnePlotter, reg, err));
  REQUIRE(err.empty());
  REQUIRE(reg.plotters.size() == 1);

  const Plotter &p = reg.plotters[0];
  REQUIRE(p.id == "TEST_A1");
  REQUIRE(p.name == "test_a1");
  REQUIRE(p.hpglModel == "7595A");
  REQUIRE(p.note == "hello");
  REQUIRE(p.defaultForModel);
  REQUIRE(nearlyEqual(p.maxArea.x, -100.f));
  REQUIRE(nearlyEqual(p.maxArea.y2, 50.f));
  REQUIRE(nearlyEqual(p.margin.left, 10.f));
  REQUIRE(nearlyEqual(p.margin.top, 6.f));
  REQUIRE(nearlyEqual(p.unitsPerMmX, 40.f));
  REQUIRE(p.paper.present);
  REQUIRE(p.paper.verified);
  REQUIRE(nearlyEqual(p.paper.wMm, 10.f));
  REQUIRE(nearlyEqual(p.paper.offsetLeftMm, 1.5f));
  REQUIRE(nearlyEqual(p.paper.offsetBottomMm, 0.25f));
}

static void test_lookup_by_id_and_name() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(kOnePlotter, reg, err));
  REQUIRE(reg.indexOfId("TEST_A1") == 0);
  REQUIRE(reg.indexOfName("test_a1") == 0);
  REQUIRE(reg.indexOfId("NOPE") == -1);
  REQUIRE(reg.indexOfName("nope") == -1);
}

static void test_optional_fields_default() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","max_area":{"x":0,"y":0,"x2":10,"y2":10}}]})",
      reg, err));
  const Plotter &p = reg.plotters[0];
  REQUIRE(!p.margin.any());
  REQUIRE(nearlyEqual(p.unitsPerMmX, 40.f)); // HPGL default
  REQUIRE(nearlyEqual(p.unitsPerMmY, 40.f));
  REQUIRE(!p.paper.present);
  REQUIRE(!p.defaultForModel);
}

static void test_entry_without_max_area_is_skipped() {
  // One unusable entry must not discard the usable ones around it.
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"NOAREA"},
                      {"id":"OK","max_area":{"x":0,"y":0,"x2":1,"y2":1}}]})",
      reg, err));
  REQUIRE(reg.plotters.size() == 1);
  REQUIRE(reg.plotters[0].id == "OK");
}

static void test_inverted_max_area_is_normalised() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","max_area":{"x":100,"y":50,"x2":-100,"y2":-50}}]})",
      reg, err));
  REQUIRE(nearlyEqual(reg.plotters[0].maxArea.x, -100.f));
  REQUIRE(nearlyEqual(reg.plotters[0].maxArea.x2, 100.f));
  REQUIRE(reg.plotters[0].maxArea.w() > 0.f);
}

static void test_zero_units_per_mm_falls_back() {
  // A zero scale would make every mm conversion a division by zero.
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","max_area":{"x":0,"y":0,"x2":1,"y2":1},
          "units_per_mm":{"x":0,"y":-3}}]})",
      reg, err));
  REQUIRE(nearlyEqual(reg.plotters[0].unitsPerMmX, 40.f));
  REQUIRE(nearlyEqual(reg.plotters[0].unitsPerMmY, 40.f));
}

static void test_paper_without_size_is_ignored() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","max_area":{"x":0,"y":0,"x2":1,"y2":1},
          "paper":{"offset_mm":{"left":5,"bottom":5}}}]})",
      reg, err));
  REQUIRE(!reg.plotters[0].paper.present);
}

static void test_rejects_malformed_json() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(!parsePlotterRegistry("{\"plotters\": [", reg, err));
  REQUIRE(!err.empty());
  REQUIRE(reg.plotters.empty());
}

static void test_rejects_missing_plotters_array() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(!parsePlotterRegistry(R"({"schema_version":1})", reg, err));
  REQUIRE(!err.empty());
}

static void test_rejects_registry_with_no_usable_entries() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(!parsePlotterRegistry(R"({"plotters":[{"id":"NOAREA"}]})", reg, err));
}

static void test_string_escapes() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","note":"a\"b\\c\tdé",
          "max_area":{"x":0,"y":0,"x2":1,"y2":1}}]})",
      reg, err));
  REQUIRE(reg.plotters[0].note == "a\"b\\c\tdé");
}

static void test_negative_and_exponent_numbers() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[{"id":"X","max_area":{"x":-1.5e2,"y":-0.5,"x2":2E1,"y2":3}}]})",
      reg, err));
  REQUIRE(nearlyEqual(reg.plotters[0].maxArea.x, -150.f));
  REQUIRE(nearlyEqual(reg.plotters[0].maxArea.y, -0.5f));
  REQUIRE(nearlyEqual(reg.plotters[0].maxArea.x2, 20.f));
}

static void test_load_from_missing_file_reports_error() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(!loadPlotterRegistry("/nonexistent/plotters.json", reg, err));
  REQUIRE(!err.empty());
}

// ── Geometry ──────────────────────────────────────────────────────────────────

static void test_effective_area_insets_by_margin() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(kOnePlotter, reg, err));
  Rect e = effectivePlotArea(reg.plotters[0]);
  // max_area (-100,-50)-(100,50) inset by L10 B2 R4 T6
  REQUIRE(nearlyEqual(e.x,  -90.f));
  REQUIRE(nearlyEqual(e.y,  -48.f));
  REQUIRE(nearlyEqual(e.x2,  96.f));
  REQUIRE(nearlyEqual(e.y2,  44.f));
}

static void test_effective_area_without_margin_equals_max_area() {
  Plotter p;
  p.maxArea = {0, 0, 100, 80};
  Rect e = effectivePlotArea(p);
  REQUIRE(nearlyEqual(e.x, 0.f) && nearlyEqual(e.y, 0.f));
  REQUIRE(nearlyEqual(e.x2, 100.f) && nearlyEqual(e.y2, 80.f));
}

static void test_crossing_margins_collapse_not_invert() {
  Plotter p;
  p.maxArea = {0, 0, 100, 100};
  p.margin  = {80, 0, 80, 0}; // left + right exceed the width
  Rect e = effectivePlotArea(p);
  REQUIRE(e.w() >= 0.f);
  REQUIRE(nearlyEqual(e.w(), 0.f));
  REQUIRE(nearlyEqual(e.h(), 100.f));
}

static void test_paper_rect_from_offsets() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(kOnePlotter, reg, err));
  Rect pr;
  REQUIRE(paperRect(reg.plotters[0], pr));
  // max_area.x -100, offset 1.5 mm at 40 u/mm → sheet starts 60 units left
  REQUIRE(nearlyEqual(pr.x, -160.f));
  REQUIRE(nearlyEqual(pr.y, -60.f));   // -50 - 0.25*40
  REQUIRE(nearlyEqual(pr.w(), 400.f)); // 10 mm * 40
  REQUIRE(nearlyEqual(pr.h(), 200.f)); //  5 mm * 40
}

static void test_paper_rect_absent_without_paper_data() {
  Plotter p;
  p.maxArea = {0, 0, 10, 10};
  Rect pr;
  REQUIRE(!paperRect(p, pr));
}

static void test_clearance_positive_when_inside() {
  Rect inner{100, 100, 900, 500};
  Rect outer{0, 0, 1000, 600};
  Clearance c = clearanceMm(inner, outer, 40.f, 40.f);
  REQUIRE(nearlyEqual(c.left,   2.5f));  // 100/40
  REQUIRE(nearlyEqual(c.right,  2.5f));  // (1000-900)/40
  REQUIRE(nearlyEqual(c.bottom, 2.5f));
  REQUIRE(nearlyEqual(c.top,    2.5f));
  REQUIRE(c.fits());
  REQUIRE(nearlyEqual(c.worst(), 2.5f));
}

static void test_clearance_negative_when_overrunning() {
  Rect inner{-40, 0, 1000, 600};
  Rect outer{0, 0, 1000, 600};
  Clearance c = clearanceMm(inner, outer, 40.f, 40.f);
  REQUIRE(nearlyEqual(c.left, -1.f)); // overruns the left edge by 1 mm
  REQUIRE(nearlyEqual(c.right, 0.f));
  REQUIRE(!c.fits());
  REQUIRE(nearlyEqual(c.worst(), -1.f));
}

static void test_clearance_uses_per_axis_scale() {
  Rect inner{80, 80, 920, 920};
  Rect outer{0, 0, 1000, 1000};
  Clearance c = clearanceMm(inner, outer, 40.f, 20.f);
  REQUIRE(nearlyEqual(c.left, 2.f)); // 80/40
  REQUIRE(nearlyEqual(c.bottom, 4.f)); // 80/20
}

static void test_clearance_guards_zero_scale() {
  Rect inner{40, 40, 60, 60};
  Rect outer{0, 0, 100, 100};
  Clearance c = clearanceMm(inner, outer, 0.f, 0.f);
  REQUIRE(nearlyEqual(c.left, 1.f)); // falls back to 40 units/mm
}

// ── Filename matching ─────────────────────────────────────────────────────────

static void test_matches_plotter_name_in_filename() {
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(kOnePlotter, reg, err));
  REQUIRE(matchPlotterByFilename(reg, "calendar_2026_test_a1.hpgl") == 0);
  REQUIRE(matchPlotterByFilename(reg, "CALENDAR_TEST_A1.HPGL") == 0);
  REQUIRE(matchPlotterByFilename(reg, "calendar_2026.hpgl") == -1);
}

static void test_longest_matching_name_wins() {
  // "hpdm_sx_a1" contains "hpdm_sx", so the more specific entry must win
  // regardless of the order they appear in the file.
  PlotterRegistry reg;
  std::string err;
  REQUIRE(parsePlotterRegistry(
      R"({"plotters":[
          {"id":"SHORT","name":"hpdm_sx","max_area":{"x":0,"y":0,"x2":1,"y2":1}},
          {"id":"LONG","name":"hpdm_sx_a1","max_area":{"x":0,"y":0,"x2":1,"y2":1}}]})",
      reg, err));
  REQUIRE(matchPlotterByFilename(reg, "art_hpdm_sx_a1_0.hpgl") == 1);
  REQUIRE(matchPlotterByFilename(reg, "art_hpdm_sx_0.hpgl") == 0);
}

// ── The bundled registry ──────────────────────────────────────────────────────

static void test_bundled_registry_loads() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  REQUIRE(err.empty());
  REQUIRE(reg.plotters.size() >= 15);

  // Every entry must have a usable plot area and a paper block to draw.
  for (const Plotter &p : reg.plotters) {
    REQUIRE(!p.id.empty());
    REQUIRE(!p.name.empty());
    REQUIRE(p.maxArea.w() > 0.f);
    REQUIRE(p.maxArea.h() > 0.f);
    REQUIRE(p.paper.present);
    REQUIRE(effectivePlotArea(p).w() > 0.f);
    REQUIRE(effectivePlotArea(p).h() > 0.f);
  }
}

static void test_bundled_registry_covers_requested_families() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  const char *ids[] = {
    "HP_7550A_A3", "HP_7550A_A4",
    "HP_DM_II_A0", "HP_DM_SX_A1", "HP_DM_RX_PLUS_A1",
    "ROLAND_DXY885", "ROLAND_DXY980", "ROLAND_DXY990",
    "ROLAND_DXY1200_A3", "ROLAND_DXY1300",
    "MUTOH_XP500_A1", "MUTOH_XP500_A2", "MUTOH_XP500_A3",
    "MUTOH_XP500_100x70cm", "MUTOH_XP500_500x297mm",
  };
  for (const char *id : ids)
    REQUIRE(reg.indexOfId(id) >= 0);
}

static void test_hpdm_sx_a1_margin_matches_documentation() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  int i = reg.indexOfId("HP_DM_SX_A1");
  REQUIRE(i >= 0);
  const Plotter &p = reg.plotters[i];
  // 25 mm left, 5 mm top/bottom at 40 units/mm
  REQUIRE(nearlyEqual(p.margin.left, 1000.f));
  REQUIRE(nearlyEqual(p.margin.bottom, 200.f));
  REQUIRE(nearlyEqual(p.margin.top, 200.f));

  Rect e = effectivePlotArea(p);
  REQUIRE(nearlyEqual(e.x, -15100.f));
  REQUIRE(nearlyEqual(e.y, -11400.f));
  REQUIRE(nearlyEqual(e.x2, 16100.f));
  REQUIRE(nearlyEqual(e.y2, 11400.f));
}

static void test_real_drawing_overruns_are_detected() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  int i = reg.indexOfId("HP_DM_SX_A1");
  REQUIRE(i >= 0);
  const Plotter &p = reg.plotters[i];

  // Parsed bounds of tests/data/calendar_2026_hpdm_sx_a1.hpgl, a real file
  // authored for this plotter.  It runs 8.4 mm past the usable plot area on
  // both the left and the right.
  Rect doc{-15436.f, -11136.f, 16436.f, 10635.2f};
  Clearance c = clearanceMm(doc, effectivePlotArea(p), p.unitsPerMmX, p.unitsPerMmY);
  REQUIRE(!c.fits());
  REQUIRE(nearlyEqual(c.left,  -8.4f));
  REQUIRE(nearlyEqual(c.right, -8.4f));
  REQUIRE(nearlyEqual(c.bottom, 6.6f));
  REQUIRE(nearlyEqual(c.top,   19.12f));

  // The two rectangles disagree about *why* each edge fails, which is the
  // distinction the overlay exists to draw: on the left the pen could
  // physically reach (16.6 mm of room against the machine maximum) and only
  // the deliberate 25 mm margin is violated, whereas on the right the
  // drawing runs past the hard limit and will actually be clipped.
  Clearance m = clearanceMm(doc, p.maxArea, p.unitsPerMmX, p.unitsPerMmY);
  REQUIRE(!m.fits());
  REQUIRE(nearlyEqual(m.left,  16.6f));
  REQUIRE(nearlyEqual(m.right, -8.4f));
  REQUIRE(m.bottom > 0.f && m.top > 0.f);
}

static void test_real_drawing_within_bounds_reports_fit() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  int i = reg.indexOfId("HP_7550A_A3");
  REQUIRE(i >= 0);
  const Plotter &p = reg.plotters[i];

  // Parsed bounds of tests/data/calendar_2026_hp7550a_a3.hpgl.
  Rect doc{313.f, 313.f, 15337.f, 10326.4f};
  Clearance c = clearanceMm(doc, effectivePlotArea(p), p.unitsPerMmX, p.unitsPerMmY);
  REQUIRE(c.fits());
  REQUIRE(nearlyEqual(c.left, 7.825f));
  // 320-unit right margin → x2 is 15650, leaving (15650-15337)/40 mm
  REQUIRE(nearlyEqual(c.right, 7.825f));
}

static void test_bundled_names_match_test_data_filenames() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  REQUIRE(reg.plotters[matchPlotterByFilename(
              reg, "calendar_2026_hpdm_sx_a1.hpgl")].id == "HP_DM_SX_A1");
  REQUIRE(reg.plotters[matchPlotterByFilename(
              reg, "calendar_2026_hpdm_sx_a0.hpgl")].id == "HP_DM_SX_A0");
  REQUIRE(reg.plotters[matchPlotterByFilename(
              reg, "composition100_manual_0b56ffcd_hp7550a_a3__256a341b_251010_135033_4.hpgl")]
              .id == "HP_7550A_A3");
  REQUIRE(reg.plotters[matchPlotterByFilename(
              reg, "c100_engine_665bacfa_hpdm_sx_a1__7bb8d6db_260309_103643_0.hpgl")]
              .id == "HP_DM_SX_A1");
  REQUIRE(reg.plotters[matchPlotterByFilename(
              reg, "composition100_manual_14d5a114_mutoh_xp500_a1__b2601bb5_260214_102422_0.hpgl")]
              .id == "MUTOH_XP500_A1");
  // The size-suffixed Mutoh configs must not be shadowed by the paper-size ones.
  REQUIRE(reg.plotters[matchPlotterByFilename(reg, "x_mutoh_xp500_100x70cm_0.hpgl")]
              .id == "MUTOH_XP500_100x70cm");
}

static void test_mutoh_a1_drawing_fits() {
  if (g_registryPath.empty()) return;
  PlotterRegistry reg;
  std::string err;
  REQUIRE(loadPlotterRegistry(g_registryPath, reg, err));
  int i = reg.indexOfId("MUTOH_XP500_A1");
  REQUIRE(i >= 0);
  const Plotter &p = reg.plotters[i];

  // The 600-unit right margin is the only inset on this machine.
  Rect e = effectivePlotArea(p);
  REQUIRE(nearlyEqual(e.x,  -16200.f));
  REQUIRE(nearlyEqual(e.x2,  15600.f));
  REQUIRE(nearlyEqual(e.y,  -11645.f));
  REQUIRE(nearlyEqual(e.y2,  11645.f));

  // Parsed bounds of
  // tests/data/composition100_manual_14d5a114_mutoh_xp500_a1__b2601bb5_260214_102422_0.hpgl
  Rect doc{-15309.f, -10844.f, 15310.f, 10845.f};
  Clearance c = clearanceMm(doc, e, p.unitsPerMmX, p.unitsPerMmY);
  REQUIRE(c.fits());
  REQUIRE(nearlyEqual(c.left,  22.275f));
  REQUIRE(nearlyEqual(c.right,  7.25f));  // eaten into by the right margin
  REQUIRE(nearlyEqual(c.bottom, 20.025f));
  REQUIRE(nearlyEqual(c.top,    20.0f));
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  if (argc > 1) g_registryPath = argv[1];

  run("parses a full entry",                 test_parses_a_full_entry);
  run("lookup by id and name",               test_lookup_by_id_and_name);
  run("optional fields default",             test_optional_fields_default);
  run("entry without max_area is skipped",   test_entry_without_max_area_is_skipped);
  run("inverted max_area is normalised",     test_inverted_max_area_is_normalised);
  run("zero units_per_mm falls back",        test_zero_units_per_mm_falls_back);
  run("paper without size is ignored",       test_paper_without_size_is_ignored);
  run("rejects malformed json",              test_rejects_malformed_json);
  run("rejects missing plotters array",      test_rejects_missing_plotters_array);
  run("rejects registry with no entries",    test_rejects_registry_with_no_usable_entries);
  run("string escapes",                      test_string_escapes);
  run("negative and exponent numbers",       test_negative_and_exponent_numbers);
  run("load from missing file errors",       test_load_from_missing_file_reports_error);

  run("effective area insets by margin",     test_effective_area_insets_by_margin);
  run("no margin equals max area",           test_effective_area_without_margin_equals_max_area);
  run("crossing margins collapse",           test_crossing_margins_collapse_not_invert);
  run("paper rect from offsets",             test_paper_rect_from_offsets);
  run("paper rect absent without data",      test_paper_rect_absent_without_paper_data);
  run("clearance positive when inside",      test_clearance_positive_when_inside);
  run("clearance negative when overrunning", test_clearance_negative_when_overrunning);
  run("clearance uses per-axis scale",       test_clearance_uses_per_axis_scale);
  run("clearance guards zero scale",         test_clearance_guards_zero_scale);

  run("matches plotter name in filename",    test_matches_plotter_name_in_filename);
  run("longest matching name wins",          test_longest_matching_name_wins);

  run("bundled registry loads",              test_bundled_registry_loads);
  run("bundled registry covers families",    test_bundled_registry_covers_requested_families);
  run("hpdm_sx_a1 margin matches docs",      test_hpdm_sx_a1_margin_matches_documentation);
  run("real drawing overruns detected",      test_real_drawing_overruns_are_detected);
  run("real drawing within bounds fits",     test_real_drawing_within_bounds_reports_fit);
  run("bundled names match test filenames",  test_bundled_names_match_test_data_filenames);
  run("mutoh a1 drawing fits",               test_mutoh_a1_drawing_fits);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
