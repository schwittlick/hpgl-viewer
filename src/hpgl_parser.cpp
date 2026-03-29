#include "hpgl_parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// ── Private methods ───────────────────────────────────────────────────────────

std::vector<float> HpglParser::parseCoords(const std::string &params) {
  std::vector<float> v;
  std::stringstream ss(params);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    tok = trim(tok);
    if (!tok.empty()) {
      try {
        v.push_back(std::stof(tok));
      } catch (...) {
      }
    }
  }
  return v;
}

void HpglParser::updateBounds(float x, float y) {
  doc.minX = std::min(doc.minX, x);
  doc.minY = std::min(doc.minY, y);
  doc.maxX = std::max(doc.maxX, x);
  doc.maxY = std::max(doc.maxY, y);
}

void HpglParser::addPoint(float x, float y) {
  Stroke &s = doc.strokes[curIdx];
  s.points.push_back({x, y});
  s.bboxMin.x = std::min(s.bboxMin.x, x); s.bboxMin.y = std::min(s.bboxMin.y, y);
  s.bboxMax.x = std::max(s.bboxMax.x, x); s.bboxMax.y = std::max(s.bboxMax.y, y);
  updateBounds(x, y);
}

void HpglParser::ensureStroke() {
  bool needNew = (curIdx < 0) ||
                 (doc.strokes[curIdx].pen != currentPen);
  if (needNew) {
    doc.strokes.push_back({});
    curIdx = static_cast<int>(doc.strokes.size()) - 1;
    doc.strokes[curIdx].pen = currentPen;
    if (penDown)
      addPoint(cx, cy);
  }
}

void HpglParser::handleSP(const std::string &params) {
  auto v = parseCoords(params);
  if (!v.empty())
    currentPen = static_cast<int>(v[0]);
  curIdx = -1;
}

void HpglParser::handlePU(const std::string &params) {
  penDown = false;
  curIdx = -1;
  auto v = parseCoords(params);
  // HPGL allows multiple coordinate pairs on PU, but only the final position
  // matters (no pen-down stroke is produced). All intermediate positions are
  // intentionally discarded.
  for (size_t i = 0; i + 1 < v.size(); i += 2) {
    cx = v[i];
    cy = v[i + 1];
  }
}

void HpglParser::handlePD(const std::string &params) {
  penDown = true;
  auto v = parseCoords(params);
  ensureStroke();
  for (size_t i = 0; i + 1 < v.size(); i += 2) {
    cx = v[i];
    cy = v[i + 1];
    addPoint(cx, cy);
  }
}

void HpglParser::handlePA(const std::string &params) {
  auto v = parseCoords(params);
  for (size_t i = 0; i + 1 < v.size(); i += 2) {
    cx = v[i];
    cy = v[i + 1];
    if (penDown) {
      ensureStroke();
      addPoint(cx, cy);
    }
  }
}

// ── Public API ────────────────────────────────────────────────────────────────

HpglDoc HpglParser::parse(const std::string &content) {
  // Reset state for a fresh parse.
  doc      = {};
  currentPen = 1;
  penDown    = false;
  cx = cy    = 0;
  curIdx     = -1;

  size_t pos = 0;
  while (pos < content.size()) {
    // Skip whitespace and semicolons.
    while (pos < content.size() &&
           (content[pos] == ';' || content[pos] == ' ' ||
            content[pos] == '\n' || content[pos] == '\r' ||
            content[pos] == '\t'))
      ++pos;
    if (pos >= content.size())
      break;

    // Read 2-char command mnemonic.
    if (pos + 1 >= content.size())
      break;
    char c0 = static_cast<char>(toupper(static_cast<unsigned char>(content[pos])));
    char c1 = static_cast<char>(toupper(static_cast<unsigned char>(content[pos + 1])));
    pos += 2;

    // Collect parameters up to the next semicolon.
    std::string params;
    while (pos < content.size() && content[pos] != ';')
      params += content[pos++];
    params = trim(params);

    if      (c0 == 'S' && c1 == 'P') handleSP(params);
    else if (c0 == 'P' && c1 == 'U') handlePU(params);
    else if (c0 == 'P' && c1 == 'D') handlePD(params);
    else if (c0 == 'P' && c1 == 'A') handlePA(params);
  }

  // Fallback bounds for empty/degenerate documents.
  if (doc.minX > doc.maxX) {
    doc.minX = doc.minY = 0;
    doc.maxX = doc.maxY = 1;
  }

  return doc;
}

HpglDoc HpglParser::parseFile(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  return parse(content);
}
