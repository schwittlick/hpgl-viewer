#include "../src/export_png.h"
#include "test_harness.h"

#include <cstdio>
#include <string>
#include <unistd.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

static HpglDoc makeDoc(float x0, float y0, float x1, float y1) {
  HpglDoc doc;
  doc.minX = x0; doc.minY = y0;
  doc.maxX = x1; doc.maxY = y1;
  doc.strokes.push_back(Stroke{{{x0, y0}, {x1, y1}}, 1});
  return doc;
}

static PenStyle defaultPens() {
  PenStyle p;
  p.color     = {0.1f, 0.1f, 0.1f, 1.0f};
  p.thickness = 0.3f;
  return p;
}

static std::string tmpPath(const char *suffix) {
  return std::string("/tmp/test_export_png_") +
         std::to_string(getpid()) + suffix;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testEmptyDocReturnsFalse() {
  HpglDoc empty;
  PenStyle pens[10];
  for (auto &p : pens) p = defaultPens();
  bool ok = exportPng(empty, pens, tmpPath("_empty.png"));
  REQUIRE(!ok);
}

static void testSimpleDocProducesFile() {
  HpglDoc doc = makeDoc(0, 0, 4000, 4000); // 100mm × 100mm
  PenStyle pens[10];
  for (auto &p : pens) p = defaultPens();
  std::string path = tmpPath("_simple.png");
  bool ok = exportPng(doc, pens, path, 200);
  REQUIRE(ok);

  // File must be non-empty
  FILE *f = fopen(path.c_str(), "rb");
  REQUIRE(f != nullptr);
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fclose(f);
  REQUIRE(sz > 0);

  remove(path.c_str());
}

static void testUnwritablePathReturnsFalse() {
  HpglDoc doc = makeDoc(0, 0, 4000, 4000);
  PenStyle pens[10];
  for (auto &p : pens) p = defaultPens();
  bool ok = exportPng(doc, pens, "/nonexistent_dir/out.png", 200);
  REQUIRE(!ok);
}

static void testMultipleStrokes() {
  HpglDoc doc;
  doc.minX = 0; doc.minY = 0; doc.maxX = 4000; doc.maxY = 4000;
  doc.strokes.push_back(Stroke{{{0,0},{2000,0}}, 1});
  doc.strokes.push_back(Stroke{{{0,2000},{4000,2000}}, 2});
  doc.strokes.push_back(Stroke{{{3000,3000},{3000,3000}}, 3}); // dot stroke
  PenStyle pens[10];
  for (auto &p : pens) p = defaultPens();
  std::string path = tmpPath("_multi.png");
  bool ok = exportPng(doc, pens, path, 200);
  REQUIRE(ok);
  remove(path.c_str());
}

static void testWidthDeterminesOutputSize() {
  // Just ensure it doesn't crash with a small width
  HpglDoc doc = makeDoc(0, 0, 4000, 2000);
  PenStyle pens[10];
  for (auto &p : pens) p = defaultPens();
  std::string path = tmpPath("_small.png");
  bool ok = exportPng(doc, pens, path, 50);
  REQUIRE(ok);
  remove(path.c_str());
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
  run("empty doc returns false",       testEmptyDocReturnsFalse);
  run("simple doc produces file",      testSimpleDocProducesFile);
  run("unwritable path returns false", testUnwritablePathReturnsFalse);
  run("multiple strokes",              testMultipleStrokes);
  run("small width does not crash",    testWidthDeterminesOutputSize);
}
