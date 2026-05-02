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

  // threshold=400 units (10 cm), step=400 units
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f);

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
  HpglDoc fixed = fixLongPenUps(doc, 800.f, 400.f);

  for (auto &s : fixed.strokes)
    REQUIRE(s.pen != 8);
}

static void test_waypoints_inserted_for_long_gap() {
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 5000; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{4000.f, 0.f}, {4010.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{4900.f, 0.f}, {4910.f, 0.f}}, 1});

  // gap = 890 units > threshold 400 → waypoints inserted
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f);

  bool found_pen8 = false;
  for (auto &s : fixed.strokes)
    if (s.pen == 8) found_pen8 = true;
  REQUIRE(found_pen8);
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

  // threshold=10 cm, step=2 cm
  float threshold = 10.f * kHpglUnitsPerCm;
  float step      =  2.f * kHpglUnitsPerCm;
  HpglDoc fixed   = fixLongPenUps(doc, threshold, step);

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

static void test_stats_empty_strokes_skipped_in_pen_up_chain() {
  // An empty stroke between two real ones is skipped entirely — it produces
  // no plotter movement, so the pen-up gap is measured from stroke[0] to
  // stroke[2] directly: (0,0)→(400,0) = 400 units = 10 mm.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{}, 1});               // empty — skipped
  doc.strokes.push_back(Stroke{{{400.f, 0.f}}, 1});
  DocStats s = computeDocStats(doc);
  REQUIRE(s.penUpMm == 10.0f);
}

static void test_stats_num_paths_excludes_empty_strokes() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1}); // real
  doc.strokes.push_back(Stroke{{}, 1});            // empty — must not be counted
  doc.strokes.push_back(Stroke{{{1.f, 0.f}}, 1}); // real
  DocStats s = computeDocStats(doc);
  REQUIRE(s.numPaths == 2); // only the two non-empty strokes
}

// ── fixedPath ────────────────────────────────────────────────────────────────

static void test_fixed_path_with_extension() {
  REQUIRE(fixedPath("foo/bar.hpgl") == "foo/bar_fixed.hpgl");
}

static void test_fixed_path_no_extension() {
  REQUIRE(fixedPath("foo/bar") == "foo/bar_fixed.hpgl");
}

static void test_fixed_path_preserves_non_hpgl_extension() {
  REQUIRE(fixedPath("/tmp/plot.plt") == "/tmp/plot_fixed.plt");
}

// ── fixLongPenUps edge cases ──────────────────────────────────────────────────

static void test_single_stroke_doc_returned_unchanged() {
  // A document with only one stroke has no inter-stroke gaps — must be a no-op.
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 100; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f);
  REQUIRE(fixed.strokes.size() == 1);
  for (auto &s : fixed.strokes) REQUIRE(s.pen != kWaypointPen);
}

static void test_step_larger_than_dist_inserts_no_waypoints() {
  // Gap of 800 units, step of 1000 units → floor(800/1000) == 0 waypoints.
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 800; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{800.f, 0.f}}, 1});
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 1000.f);
  for (auto &s : fixed.strokes) REQUIRE(s.pen != kWaypointPen);
}

static void test_threshold_boundary_equal_does_not_insert() {
  // dist == thresholdUnits: the guard is strict >, so no waypoints.
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 400; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{400.f, 0.f}}, 1});
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 100.f);
  for (auto &s : fixed.strokes) REQUIRE(s.pen != kWaypointPen);
}

static void test_long_gap_inserts_waypoints() {
  // gap = 1000 > threshold 400 → waypoints inserted.
  HpglDoc doc;
  doc.minX = 0; doc.maxX = 1000; doc.minY = 0; doc.maxY = 0;
  doc.strokes.push_back(Stroke{{{500.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{1500.f, 0.f}}, 1});
  HpglDoc fixed = fixLongPenUps(doc, 400.f, 400.f);
  bool found = false;
  for (auto &s : fixed.strokes) if (s.pen == kWaypointPen) found = true;
  REQUIRE(found);
}

// ── mergeCloseStrokes ────────────────────────────────────────────────────────

static const float *uniformThresholds(float t) {
  static float buf[10];
  for (int i = 0; i < 10; ++i) buf[i] = t;
  return buf;
}

static void test_merge_same_pen_within_threshold() {
  // Gap of 10 units, threshold of 15 → strokes must be merged.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  REQUIRE(out.strokes.size() == 1);
}

static void test_merge_same_pen_outside_threshold() {
  // Gap of 10 units, threshold of 5 → strokes must NOT be merged.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(5.f));
  REQUIRE(out.strokes.size() == 2);
}

static void test_merge_different_pens_not_merged() {
  // Same gap, both pens within threshold, but different pen numbers → no merge.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 2});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  REQUIRE(out.strokes.size() == 2);
}

static void test_merge_chain_three_strokes() {
  // A→B and B→C both within threshold → all three merged into one stroke.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{210.f, 0.f}, {300.f, 0.f}}, 1});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  REQUIRE(out.strokes.size() == 1);
}

static void test_merge_preserves_all_points() {
  // Merged stroke contains all points from both input strokes in order.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  REQUIRE(out.strokes.size() == 1);
  REQUIRE(out.strokes[0].points.size() == 4);
  REQUIRE((out.strokes[0].points[0] == Vec2{  0.f, 0.f}));
  REQUIRE((out.strokes[0].points[1] == Vec2{100.f, 0.f}));
  REQUIRE((out.strokes[0].points[2] == Vec2{110.f, 0.f}));
  REQUIRE((out.strokes[0].points[3] == Vec2{200.f, 0.f}));
}

static void test_merge_per_pen_threshold() {
  // Pen 1 has a large threshold, pen 2 has zero → only pen-1 strokes merge.
  static float buf[10] = {};
  buf[0] = 50.f; // pen 1 (index 0)
  buf[1] =  0.f; // pen 2 (index 1)
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1}); // gap 10, pen 1 → merge
  doc.strokes.push_back(Stroke{{{300.f, 0.f}, {400.f, 0.f}}, 2});
  doc.strokes.push_back(Stroke{{{310.f, 0.f}, {500.f, 0.f}}, 2}); // gap 10, pen 2 → no merge
  HpglDoc out = mergeCloseStrokes(doc, buf);
  REQUIRE(out.strokes.size() == 3); // merged pen-1 pair + 2 separate pen-2 strokes
}

static void test_merge_empty_strokes_pass_through() {
  // Empty strokes are not merged with neighbours and are preserved.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{}, 1}); // empty
  doc.strokes.push_back(Stroke{{{110.f, 0.f}, {200.f, 0.f}}, 1});
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  // The empty stroke breaks the chain — strokes before and after it are separate.
  REQUIRE(out.strokes.size() == 3);
}

static void test_merge_exact_threshold_distance_merges() {
  // Distance exactly equals threshold (≤ comparison) → must merge.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{115.f, 0.f}, {200.f, 0.f}}, 1}); // gap = 15
  HpglDoc out = mergeCloseStrokes(doc, uniformThresholds(15.f));
  REQUIRE(out.strokes.size() == 1);
}

// ── splitLongStrokes ─────────────────────────────────────────────────────────

static void test_split_short_stroke_unchanged() {
  // Stroke length 100 < max 400 → unchanged.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  REQUIRE(out.strokes.size() == 1);
  REQUIRE(out.strokes[0].points.size() == 2);
}

static void test_split_one_segment_above_max() {
  // Single segment of 1000 units, max = 400 → 1000/400 = 2.5 → 3 fragments.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {1000.f, 0.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  REQUIRE(out.strokes.size() == 3);
  // Each fragment must start where the previous one ended.
  REQUIRE((out.strokes[0].points.back() == out.strokes[1].points.front()));
  REQUIRE((out.strokes[1].points.back() == out.strokes[2].points.front()));
  // First fragment is exactly maxLength long
  REQUIRE((out.strokes[0].points.front() == Vec2{0.f, 0.f}));
  REQUIRE((out.strokes[0].points.back()  == Vec2{400.f, 0.f}));
  // Last fragment ends at the original endpoint
  REQUIRE((out.strokes[2].points.back()  == Vec2{1000.f, 0.f}));
}

static void test_split_preserves_pen() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {1000.f, 0.f}}, 5});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  for (auto &s : out.strokes) REQUIRE(s.pen == 5);
}

static void test_split_exact_multiple_no_extra_fragment() {
  // 800 units, max 400 → exactly 2 equal fragments, no trailing single point.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {800.f, 0.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  REQUIRE(out.strokes.size() == 2);
  REQUIRE((out.strokes[0].points.back()  == Vec2{400.f, 0.f}));
  REQUIRE((out.strokes[1].points.front() == Vec2{400.f, 0.f}));
  REQUIRE((out.strokes[1].points.back()  == Vec2{800.f, 0.f}));
}

static void test_split_polyline_accumulates_across_segments() {
  // Polyline of 4 segments × 100 units = 400 total; max = 250.
  // Expected: split mid-segment after 250 units cumulative.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{
    {0.f, 0.f}, {100.f, 0.f}, {200.f, 0.f}, {300.f, 0.f}, {400.f, 0.f}
  }, 1});
  HpglDoc out = splitLongStrokes(doc, 250.f);
  REQUIRE(out.strokes.size() == 2);
  REQUIRE((out.strokes[0].points.back()  == Vec2{250.f, 0.f}));
  REQUIRE((out.strokes[1].points.front() == Vec2{250.f, 0.f}));
  REQUIRE((out.strokes[1].points.back()  == Vec2{400.f, 0.f}));
}

static void test_split_zero_max_is_noop() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {1000.f, 0.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 0.f);
  REQUIRE(out.strokes.size() == 1);
  REQUIRE(out.strokes[0].points.size() == 2);
}

static void test_split_single_point_passes_through() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{50.f, 50.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  REQUIRE(out.strokes.size() == 1);
  REQUIRE(out.strokes[0].points.size() == 1);
}

static void test_split_waypoint_pen_passes_through() {
  // Pen-8 dot strokes from fixLongPenUps must not be split.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {0.f, 0.f}}, kWaypointPen});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  REQUIRE(out.strokes.size() == 1);
  REQUIRE(out.strokes[0].pen == kWaypointPen);
}

static void test_split_export_roundtrip_creates_pen_up_commands() {
  // The whole point of the feature: each split fragment must round-trip through
  // export as its own PU…PD sequence, i.e. the plotter physically lifts the pen.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {1000.f, 0.f}}, 1});

  HpglDoc split = splitLongStrokes(doc, 400.f);
  REQUIRE(split.strokes.size() == 3);

  const std::string tmp = "/tmp/test_split_roundtrip.hpgl";
  REQUIRE(exportHpgl(split, tmp));

  // Count PU occurrences.  The first fragment uses PU<x>,<y>; (1 PU).  Each
  // of the 2 inter-fragment lifts uses PU<x+1>,<y>;PU<x>,<y>; (2 PUs each).
  // Final park adds 1.  Total: 1 + 2*2 + 1 = 6.
  std::string content = readFile(tmp);
  int pu_count = 0;
  for (size_t i = 0; (i = content.find("PU", i)) != std::string::npos; ++i)
    ++pu_count;
  REQUIRE(pu_count == 6);

  // Each inter-fragment lift must be "PU<x+1>,<y>;PU<x>,<y>;" — move 1 unit
  // away then return (pen still up) so the plotter registers a physical lift.
  // Two such lifts expected (between 3 fragments).
  // First split lands at (400,0) → expect PU401,0;PU400,0;
  REQUIRE(content.find("PU401,0;PU400,0;") != std::string::npos);
  // Second split lands at (800,0) → expect PU801,0;PU800,0;
  REQUIRE(content.find("PU801,0;PU800,0;") != std::string::npos);
  REQUIRE(content.find("PU;\nSP0;\n") != std::string::npos);

  // Re-parse: each fragment must come back as a distinct stroke with two points.
  HpglDoc reloaded = HpglParser{}.parseFile(tmp);
  REQUIRE(reloaded.strokes.size() == 3);
  for (auto &s : reloaded.strokes) REQUIRE(s.points.size() == 2);
  remove(tmp.c_str());
}

static void test_split_bbox_populated() {
  // Fragments must have a finite bbox so renderer culling works correctly.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {1000.f, 0.f}}, 1});
  HpglDoc out = splitLongStrokes(doc, 400.f);
  for (auto &s : out.strokes) {
    REQUIRE(s.bboxMin.x <= s.bboxMax.x);
    REQUIRE(s.bboxMin.y <= s.bboxMax.y);
    REQUIRE(s.bboxMin.x < 1e29f); // not the sentinel default
  }
}

// ── exportHpgl VS velocity ────────────────────────────────────────────────────

static void test_export_vs_emitted_before_pd_multipoint() {
  // Multi-point stroke: VS<n>; must appear in the output.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  const std::string tmp = "/tmp/test_export_vs_multi.hpgl";
  REQUIRE(exportHpgl(doc, tmp, 3));
  std::string content = readFile(tmp);
  REQUIRE(content.find("VS3;") != std::string::npos);
  remove(tmp.c_str());
}

static void test_export_vs_not_emitted_for_dot() {
  // Single-point (dot) stroke: VS<n>; must NOT appear.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{50.f, 50.f}}, 1});
  const std::string tmp = "/tmp/test_export_vs_dot.hpgl";
  REQUIRE(exportHpgl(doc, tmp, 5));
  std::string content = readFile(tmp);
  REQUIRE(content.find("VS5;") == std::string::npos);
  remove(tmp.c_str());
}

static void test_export_vs_default_is_1() {
  // Default vsValue=1: multi-point stroke must get VS1;.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  const std::string tmp = "/tmp/test_export_vs_default.hpgl";
  REQUIRE(exportHpgl(doc, tmp)); // no vsValue → defaults to 1
  std::string content = readFile(tmp);
  REQUIRE(content.find("VS1;") != std::string::npos);
  remove(tmp.c_str());
}

static void test_export_vs_each_multipoint_stroke_gets_vs() {
  // Two multi-point strokes → VS appears twice.
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}, {100.f, 0.f}}, 1});
  doc.strokes.push_back(Stroke{{{200.f, 0.f}, {300.f, 0.f}}, 1});
  const std::string tmp = "/tmp/test_export_vs_twice.hpgl";
  REQUIRE(exportHpgl(doc, tmp, 2));
  std::string content = readFile(tmp);
  size_t first = content.find("VS2;");
  REQUIRE(first != std::string::npos);
  REQUIRE(content.find("VS2;", first + 1) != std::string::npos);
  remove(tmp.c_str());
}

// ── exportHpgl edge cases ─────────────────────────────────────────────────────

static void test_export_single_point_stroke_roundtrip() {
  // A single-point stroke should survive export → re-parse as a single point.
  HpglDoc doc;
  doc.minX = 50; doc.maxX = 50; doc.minY = 50; doc.maxY = 50;
  doc.strokes.push_back(Stroke{{{50.f, 50.f}}, 1});

  const std::string tmp = "/tmp/test_single_point.hpgl";
  REQUIRE(exportHpgl(doc, tmp));

  HpglDoc reloaded = HpglParser{}.parseFile(tmp);
  REQUIRE(reloaded.strokes.size() == 1);
  REQUIRE(reloaded.strokes[0].points.size() == 1);
  REQUIRE((reloaded.strokes[0].points[0] == Vec2{50.f, 50.f}));
  remove(tmp.c_str());
}

static void test_export_unwritable_path_returns_false() {
  HpglDoc doc;
  doc.strokes.push_back(Stroke{{{0.f, 0.f}}, 1});
  REQUIRE(!exportHpgl(doc, "/no_such_dir/out.hpgl"));
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  g_dataDir = argc > 1 ? argv[1] : "tests/data";

  run("waypoints use pen 8",                  test_waypoints_use_pen8);
  run("no waypoints below threshold",         test_no_waypoints_below_threshold);
  run("waypoints inserted for long gap",       test_waypoints_inserted_for_long_gap);
  run("export ends with SP0",                 test_export_ends_with_sp0);
  run("export roundtrip preserves strokes",   test_export_roundtrip_preserves_strokes);
  run("fix data file has pen8 and SP0",       test_fix_data_file_has_pen8_and_sp0);
  run("stats empty doc",                      test_stats_empty_doc);
  run("stats single stroke pen-down dist",    test_stats_single_stroke_pen_down_dist);
  run("stats pen-up gap between strokes",     test_stats_pen_up_gap_between_strokes);
  run("stats multiple strokes",               test_stats_multiple_strokes);
  run("stats empty strokes skipped in chain", test_stats_empty_strokes_skipped_in_pen_up_chain);
  run("stats numPaths excludes empty strokes", test_stats_num_paths_excludes_empty_strokes);
  run("fixedPath with extension",              test_fixed_path_with_extension);
  run("fixedPath no extension",               test_fixed_path_no_extension);
  run("fixedPath preserves non-hpgl ext",     test_fixed_path_preserves_non_hpgl_extension);
  run("single-stroke doc unchanged by fix",   test_single_stroke_doc_returned_unchanged);
  run("step > dist inserts no waypoints",     test_step_larger_than_dist_inserts_no_waypoints);
  run("threshold boundary equal: no insert",  test_threshold_boundary_equal_does_not_insert);
  run("long gap inserts waypoints",           test_long_gap_inserts_waypoints);
  run("export single-point stroke roundtrip", test_export_single_point_stroke_roundtrip);
  run("export unwritable path returns false", test_export_unwritable_path_returns_false);

  run("merge same pen within threshold",      test_merge_same_pen_within_threshold);
  run("merge same pen outside threshold",     test_merge_same_pen_outside_threshold);
  run("merge different pens not merged",      test_merge_different_pens_not_merged);
  run("merge chain three strokes",            test_merge_chain_three_strokes);
  run("merge preserves all points",           test_merge_preserves_all_points);
  run("merge per-pen threshold",              test_merge_per_pen_threshold);
  run("merge empty strokes pass through",     test_merge_empty_strokes_pass_through);
  run("merge exact threshold distance",       test_merge_exact_threshold_distance_merges);
  run("split: short stroke unchanged",        test_split_short_stroke_unchanged);
  run("split: one segment above max",         test_split_one_segment_above_max);
  run("split: preserves pen",                 test_split_preserves_pen);
  run("split: exact multiple no extra frag",  test_split_exact_multiple_no_extra_fragment);
  run("split: polyline accumulates",          test_split_polyline_accumulates_across_segments);
  run("split: zero max is no-op",             test_split_zero_max_is_noop);
  run("split: single point passes through",   test_split_single_point_passes_through);
  run("split: waypoint pen passes through",   test_split_waypoint_pen_passes_through);
  run("split: bbox populated",                test_split_bbox_populated);
  run("split: export roundtrip yields PUs",   test_split_export_roundtrip_creates_pen_up_commands);

  run("export VS emitted for multipoint",     test_export_vs_emitted_before_pd_multipoint);
  run("export VS not emitted for dot",        test_export_vs_not_emitted_for_dot);
  run("export VS default is 1",               test_export_vs_default_is_1);
  run("export VS emitted per stroke",         test_export_vs_each_multipoint_stroke_gets_vs);

  printf("\n%d/%d passed\n", g_pass, g_pass + g_fail);
  return g_fail > 0 ? 1 : 0;
}
