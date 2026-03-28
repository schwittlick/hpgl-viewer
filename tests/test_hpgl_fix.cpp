#include "../src/hpgl_fix.h"
#include "../src/hpgl_parser.h"
#include "test_harness.h"

// ── Tests ─────────────────────────────────────────────────────────────────────

static std::string g_dataDir;

// fixLongPenUps: waypoints use pen 8

static void test_waypoints_use_pen8() {
  // Two strokes 2000 HPGL units apart (50 cm) — well above any threshold.
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 2000; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {10.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{2000.f, 0.f}, {2010.f, 0.f}}, 1});

  // threshold=400 units (10 cm), step=400 units, cutoff=maxX
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f, doc.maxX);

  bool found_pen8 = false;
  for (auto &s : fixed.strokes)
    if (s.pen == 8) found_pen8 = true;
  REQUIRE(found_pen8);

  // All inserted waypoints must be single-point (dot) strokes with pen 8
  for (auto &s : fixed.strokes) {
    if (s.pen == 8) {
      REQUIRE(s.points.size() == 1 || (s.points.size() == 2 && s.points[0] == s.points[1]));
    }
  }
}

static void test_no_waypoints_below_threshold() {
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 100; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {10.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{100.f, 0.f}, {110.f, 0.f}}, 1});

  // threshold=800 units (20 cm) — the 100-unit gap is well below
  HpglDoc fixed = fixLongPenUps(doc, 800.f, 400.f, doc.maxX);

  for (auto &s : fixed.strokes)
    REQUIRE(s.pen != 8);
}

static void test_waypoints_outside_cutoff_not_inserted() {
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 5000; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{4000.f, 0.f}, {4010.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{4900.f, 0.f}, {4910.f, 0.f}}, 1});

  // cutoff = 10% = 500 units; both strokes start way past that
  float cutoff = doc.minX + 0.1f * (doc.maxX - doc.minX);
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f, cutoff);

  for (auto &s : fixed.strokes)
    REQUIRE(s.pen != 8);
}

// exportHpgl: file must end with PU;\nSP0;\n

static void test_export_ends_with_sp0() {
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 100; doc.minY = 0; doc.maxY = 100;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 100.f}}, 1});

  const std::string tmp = "/tmp/test_export_sp0.hpgl";
  REQUIRE(exportHpgl(doc, tmp));

  std::string content = readFile(tmp);
  REQUIRE(endsWith(content, "PU;\nSP0;\n"));
  remove(tmp.c_str());
}

static void test_export_roundtrip_preserves_strokes() {
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 200; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{200.f, 0.f}, {300.f, 0.f}}, 2});

  const std::string tmp = "/tmp/test_export_roundtrip.hpgl";
  REQUIRE(exportHpgl(doc, tmp));

  HpglDoc reloaded = HpglParser{}.parseFile(tmp);
  REQUIRE(reloaded.strokes.size() == doc.strokes.size());
  REQUIRE(reloaded.strokes[0].pen == 1);
  REQUIRE(reloaded.strokes[1].pen == 2);
  remove(tmp.c_str());
}

// Integration: fix the real data file, export, re-read, verify pen 8 + SP0

static void test_fix_data_file_has_pen8_and_sp0() {
  std::string inPath = g_dataDir + "/composition100_manual_14d5a114_mutoh_xp500_a1__b2601bb5_260214_102422_0.hpgl";
  HpglDoc doc = HpglParser{}.parseFile(inPath);
  REQUIRE(!doc.empty());

  // threshold=10 cm, step=2 cm, cutoff=100% of width
  float threshold = 10.f * kHpglUnitsPerCm;
  float step      =  2.f * kHpglUnitsPerCm;
  float cutoff    = doc.maxX;
  HpglDoc fixed   = fixLongPenUps(doc, threshold, step, cutoff);

  // Must have inserted at least one pen-8 waypoint
  bool found_pen8 = false;
  for (auto &s : fixed.strokes)
    if (s.pen == 8) { found_pen8 = true; break; }
  REQUIRE(found_pen8);

  // Export and verify SP0 at end
  const std::string tmp = "/tmp/test_fix_data_file.hpgl";
  REQUIRE(exportHpgl(fixed, tmp));

  std::string content = readFile(tmp);
  REQUIRE(endsWith(content, "PU;\nSP0;\n"));

  // Re-parse: pen-8 strokes survive the round-trip
  HpglDoc reloaded = HpglParser{}.parseFile(tmp);
  bool found_pen8_reloaded = false;
  for (auto &s : reloaded.strokes)
    if (s.pen == 8) { found_pen8_reloaded = true; break; }
  REQUIRE(found_pen8_reloaded);

  remove(tmp.c_str());
}

// ── computeDocStats tests ─────────────────────────────────────────────────────

static void test_stats_empty_doc() {
  HpglDoc doc;
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths  == 0);
  REQUIRE(s.penDownMm == 0.0f);
  REQUIRE(s.penUpMm   == 0.0f);
}

static void test_stats_single_stroke_pen_down_dist() {
  // One stroke: (0,0) → (400,0) — 400 HPGL units = 10 mm
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {400.f, 0.f}}, 1});
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths  == 1);
  REQUIRE(s.penDownMm == 10.0f);
  REQUIRE(s.penUpMm   == 0.0f);
}

static void test_stats_pen_up_gap_between_strokes() {
  // Stroke 1 ends at (0,0); stroke 2 starts at (800,0) — gap = 800 units = 20 mm
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{800.f, 0.f}}, 1});
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths == 2);
  REQUIRE(s.penUpMm  == 20.0f);
}

static void test_stats_multiple_strokes() {
  // Two strokes each 400 units long; gap of 400 units between them
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {400.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{800.f, 0.f}, {1200.f, 0.f}}, 1});
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths  == 2);
  REQUIRE(s.penDownMm == 20.0f); // 10 mm + 10 mm
  REQUIRE(s.penUpMm   == 10.0f); // gap = 400 units
}

static void test_stats_empty_strokes_ignored_in_pen_up() {
  // An empty stroke between two real ones should not produce a pen-up distance
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{}, 1});               // empty — no endpoints
  doc.strokes.push_back(Stroke{{{400.f, 0.f}}, 1});
  DocStats s = computeDocStats(doc);
  REQUIRE(s.penUpMm == 0.0f); // empty stroke breaks the chain
}

static void test_stats_num_paths_excludes_empty_strokes() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1}); // real
  doc.strokes.push_back(Stroke{{}, 1});            // empty — must not be counted
  doc.strokes.push_back(Stroke{{{1.f, 0.f}}, 1}); // real
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths == 2); // only the two non-empty strokes
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  g_dataDir = argc > 1 ? argv[1] : "tests/data";

  run("waypoints use pen 8",                  test_waypoints_use_pen8);
  run("no waypoints below threshold",         test_no_waypoints_below_threshold);
  run("waypoints outside cutoff not inserted",test_waypoints_outside_cutoff_not_inserted);
  run("export ends with SP0",                 test_export_ends_with_sp0);
  run("export roundtrip preserves strokes",   test_export_roundtrip_preserves_strokes);
  run("fix data file has pen8 and SP0",       test_fix_data_file_has_pen8_and_sp0);
  run("stats empty doc",                      test_stats_empty_doc);
  run("stats single stroke pen-down dist",    test_stats_single_stroke_pen_down_dist);
  run("stats pen-up gap between strokes",     test_stats_pen_up_gap_between_strokes);
  run("stats multiple strokes",               test_stats_multiple_strokes);
  run("stats empty strokes ignored in pen-up",test_stats_empty_strokes_ignored_in_pen_up);
  run("stats numPaths excludes empty strokes", test_stats_num_paths_excludes_empty_strokes);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
