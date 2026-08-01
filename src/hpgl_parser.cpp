#include "hpgl_parser.h"

#include "hpgl_font.h"

#include <algorithm>
#include <cmath>
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

// ── Labelling group ──────────────────────────────────────────────────────────

void HpglParser::resetLabelState() {
  charW = kDefaultCharW;
  charH = kDefaultCharH;
  dirX  = 1;
  dirY  = 0;
  slant = 0;
  labelOrigin = 1;
  labelTerm   = kDefaultLabelTerm;
}

// SI width,height — absolute character size in centimetres.  No parameters
// restores the default size.
void HpglParser::handleSI(const std::string &params) {
  auto v = parseCoords(params);
  if (v.size() < 2) {
    charW = kDefaultCharW;
    charH = kDefaultCharH;
    return;
  }
  charW = v[0] * kUnitsPerCm;
  charH = v[1] * kUnitsPerCm;
}

// LO n — label origin: where the label sits relative to the current position.
void HpglParser::handleLO(const std::string &params) {
  auto v = parseCoords(params);
  labelOrigin = v.empty() ? 1 : static_cast<int>(v[0]);
}

// DI run,rise — label direction.  No parameters restores horizontal text.
void HpglParser::handleDI(const std::string &params) {
  auto v = parseCoords(params);
  float run = 1, rise = 0;
  if (v.size() >= 2) {
    run  = v[0];
    rise = v[1];
  }
  float len = std::sqrt(run * run + rise * rise);
  if (len <= 0)
    return; // DI0,0 is undefined — keep the previous direction
  dirX = run / len;
  dirY = rise / len;
}

// SL tangent — character slant (italic).  No parameters means upright.
void HpglParser::handleSL(const std::string &params) {
  auto v = parseCoords(params);
  slant = v.empty() ? 0.f : v[0];
}

// DT c[,mode] — redefine the LB terminator.  The parameter is taken raw: it
// may be any printable character, including whitespace.
void HpglParser::handleDT(const std::string &raw) {
  labelTerm = raw.empty() ? kDefaultLabelTerm : raw[0];
}

// CP spaces,lines — move the pen by whole character cells without drawing.
// Positive `lines` moves up.  No parameters means carriage return + line feed.
void HpglParser::handleCP(const std::string &params) {
  auto v = parseCoords(params);
  float spaces = 0, lines = -1;
  if (v.size() >= 2) {
    spaces = v[0];
    lines  = v[1];
  }
  const float along = spaces * 1.5f * charW;
  const float up    = lines * 2.f * charH;
  cx += along * dirX - up * dirY;
  cy += along * dirY + up * dirX;
  curIdx = -1;
}

// LB text — draw text with the plotter's stick font.
void HpglParser::handleLB(const std::string &text) {
  const float cellW = 1.5f * charW;  // HP-GL character cell width
  const float lineH = 2.f * charH;   // line-to-line spacing
  const float sx = cellW / hpgl_font::kCellWidth;
  const float sy = charH / hpgl_font::kCapHeight;

  // Font-unit → page transform.  `along` runs in the label direction, `up`
  // is perpendicular to it; slant shears `along` by the height.
  const float ox = cx, oy = cy;
  auto place = [&](float along, float up) {
    const float a = along + up * slant;
    return Vec2{ox + a * dirX - up * dirY, oy + a * dirY + up * dirX};
  };

  // Measure each line so LO can offset the block.
  std::vector<float> lineWidths;
  float width = 0;
  for (unsigned char ch : text) {
    if (ch == '\n') {
      lineWidths.push_back(width);
      width = 0;
    } else if (ch >= ' ') {
      width += hpgl_font::glyph(ch).advance * sx;
    }
  }
  lineWidths.push_back(width);

  // LO 1..9 select one of nine anchor points; 11..19 are the same anchors
  // nudged half a cell away from the point so a label clears a plotted symbol.
  int lo = labelOrigin;
  bool offsetOrigin = lo >= 11 && lo <= 19;
  if (offsetOrigin)
    lo -= 10;
  if (lo < 1 || lo > 9) {
    lo = 1; // out of range — fall back to the default origin
    offsetOrigin = false;
  }
  const int hAnchor = (lo - 1) / 3; // 0 left, 1 centre, 2 right
  const int vAnchor = (lo - 1) % 3; // 0 bottom, 1 centre, 2 top

  const int lines = static_cast<int>(lineWidths.size());
  const float blockH = charH + (lines - 1) * lineH;
  float baseAlong = 0;
  float baseUp = (lines - 1) * lineH - vAnchor * 0.5f * blockH;
  if (offsetOrigin) {
    baseAlong += (hAnchor == 0 ? 0.5f : hAnchor == 2 ? -0.5f : 0.f) * cellW;
    baseUp    += (vAnchor == 0 ? 0.5f : vAnchor == 2 ? -0.5f : 0.f) * charH;
  }

  int line = 0;
  float along = baseAlong - hAnchor * 0.5f * lineWidths[0];
  float up    = baseUp;
  for (unsigned char ch : text) {
    if (ch == '\n') {
      ++line;
      along = baseAlong - hAnchor * 0.5f * lineWidths[line];
      up -= lineH;
      continue;
    }
    if (ch == '\r') {
      along = baseAlong - hAnchor * 0.5f * lineWidths[line];
      continue;
    }
    if (ch < ' ')
      continue; // other control characters are not plotted
    const hpgl_font::FontGlyph &g = hpgl_font::glyph(ch);
    for (int s = 0; s < g.strokeCount; ++s) {
      const hpgl_font::FontStroke &fs = g.strokes[s];
      doc.strokes.push_back({});
      curIdx = static_cast<int>(doc.strokes.size()) - 1;
      doc.strokes[curIdx].pen = currentPen;
      for (int i = 0; i < fs.count; ++i) {
        Vec2 p = place(along + fs.xy[i * 2] * sx, up + fs.xy[i * 2 + 1] * sy);
        addPoint(p.x, p.y);
      }
    }
    along += g.advance * sx;
  }

  // The pen ends up where the next character would start.
  Vec2 end = place(along, up);
  cx = end.x;
  cy = end.y;
  curIdx = -1; // never continue a pen-down stroke into label geometry
}

// ── Public API ────────────────────────────────────────────────────────────────

HpglDoc HpglParser::parse(const std::string &content,
                          std::atomic<float> *progress) {
  // Reset state for a fresh parse.
  doc      = {};
  currentPen = 1;
  penDown    = false;
  cx = cy    = 0;
  curIdx     = -1;
  resetLabelState();

  size_t pos = 0;
  size_t nextProgressAt = 0;
  // Update progress every ~64 KB of input — atomic stores are cheap but the
  // rate-limit avoids cache-line ping-pong with the polling thread.
  constexpr size_t kProgressStride = 64 * 1024;
  while (pos < content.size()) {
    if (progress && pos >= nextProgressAt) {
      progress->store(static_cast<float>(pos) /
                      static_cast<float>(content.size()),
                      std::memory_order_relaxed);
      nextProgressAt = pos + kProgressStride;
    }
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

    // LB is the one command whose parameter is free text: it runs to the
    // label terminator (ETX by default, redefinable with DT) and may contain
    // semicolons, so it cannot go through the generic parameter scan below.
    if (c0 == 'L' && c1 == 'B') {
      size_t end = content.find(labelTerm, pos);
      if (end == std::string::npos)
        end = content.size();
      handleLB(content.substr(pos, end - pos));
      pos = std::min(end + 1, content.size()); // skip the terminator
      continue;
    }

    // Collect parameters up to the next semicolon.
    std::string raw;
    while (pos < content.size() && content[pos] != ';')
      raw += content[pos++];
    std::string params = trim(raw);

    if      (c0 == 'S' && c1 == 'P') handleSP(params);
    else if (c0 == 'P' && c1 == 'U') handlePU(params);
    else if (c0 == 'P' && c1 == 'D') handlePD(params);
    else if (c0 == 'P' && c1 == 'A') handlePA(params);
    else if (c0 == 'S' && c1 == 'I') handleSI(params);
    else if (c0 == 'L' && c1 == 'O') handleLO(params);
    else if (c0 == 'D' && c1 == 'I') handleDI(params);
    else if (c0 == 'S' && c1 == 'L') handleSL(params);
    else if (c0 == 'C' && c1 == 'P') handleCP(params);
    else if (c0 == 'D' && c1 == 'T') handleDT(raw);
    else if ((c0 == 'I' && c1 == 'N') || (c0 == 'D' && c1 == 'F'))
      resetLabelState();
  }

  // Fallback bounds for empty/degenerate documents.
  if (doc.minX > doc.maxX) {
    doc.minX = doc.minY = 0;
    doc.maxX = doc.maxY = 1;
  }

  if (progress) progress->store(1.0f, std::memory_order_relaxed);
  return doc;
}

HpglDoc HpglParser::parseFile(const std::string &path,
                              std::atomic<float> *progress) {
  std::ifstream f(path);
  if (!f)
    return {};
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  return parse(content, progress);
}
