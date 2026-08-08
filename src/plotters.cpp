#include "plotters.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#ifndef HPGL_DATADIR
#define HPGL_DATADIR "/usr/local/share"
#endif

namespace fs = std::filesystem;

// ── Minimal JSON reader ───────────────────────────────────────────────────────
//
// The registry is a small, hand-maintained file, so a dependency-free
// recursive-descent parser is cheaper than pulling in a JSON library.  It
// accepts the JSON grammar as specified (no comments, no trailing commas) and
// reports the byte offset of the first problem.

namespace {

struct JVal;
using JObj = std::vector<std::pair<std::string, JVal>>;
using JArr = std::vector<JVal>;

struct JVal {
  enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
  bool                  b   = false;
  double                num = 0;
  std::string           str;
  std::shared_ptr<JArr> arr;
  std::shared_ptr<JObj> obj;
};

constexpr int kMaxDepth = 64; // guards against stack exhaustion on hostile input

class JParser {
public:
  explicit JParser(const std::string &s) : s_(s) {}

  bool parse(JVal &out) {
    if (!value(out)) return false;
    skip();
    return true; // trailing content is tolerated
  }

  const std::string &error() const { return err_; }

private:
  const std::string &s_;
  size_t             at_    = 0;
  int                depth_ = 0;
  std::string        err_;

  bool fail(const char *what) {
    if (err_.empty()) {
      std::ostringstream ss;
      ss << what << " at byte " << at_;
      err_ = ss.str();
    }
    return false;
  }

  void skip() {
    while (at_ < s_.size()) {
      char c = s_[at_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++at_;
      else break;
    }
  }

  bool value(JVal &v) {
    if (depth_ > kMaxDepth) return fail("nesting too deep");
    skip();
    if (at_ >= s_.size()) return fail("unexpected end of input");
    switch (s_[at_]) {
      case '{': return object(v);
      case '[': return array(v);
      case '"': v.kind = JVal::Str; return string(v.str);
      case 't': return literal("true",  v, JVal::Bool, true);
      case 'f': return literal("false", v, JVal::Bool, false);
      case 'n': return literal("null",  v, JVal::Null, false);
      default:  return number(v);
    }
  }

  bool literal(const char *lit, JVal &v, JVal::Kind kind, bool b) {
    size_t n = strlen(lit);
    if (s_.compare(at_, n, lit) != 0) return fail("invalid literal");
    at_ += n;
    v.kind = kind;
    v.b    = b;
    return true;
  }

  bool number(JVal &v) {
    const char *start = s_.c_str() + at_;
    char       *end   = nullptr;
    double      d     = strtod(start, &end);
    if (end == start) return fail("expected a value");
    at_ += static_cast<size_t>(end - start);
    v.kind = JVal::Num;
    v.num  = d;
    return true;
  }

  // Appends the UTF-8 encoding of a Unicode code point.
  static void appendUtf8(std::string &out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned &out) {
    if (at_ + 4 > s_.size()) return fail("truncated \\u escape");
    out = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[at_ + i];
      int  d;
      if      (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return fail("bad hex digit in \\u escape");
      out = out * 16 + static_cast<unsigned>(d);
    }
    at_ += 4;
    return true;
  }

  bool string(std::string &out) {
    if (at_ >= s_.size() || s_[at_] != '"') return fail("expected a string");
    ++at_;
    out.clear();
    while (true) {
      if (at_ >= s_.size()) return fail("unterminated string");
      char c = s_[at_++];
      if (c == '"') return true;
      if (c != '\\') { out += c; continue; }
      if (at_ >= s_.size()) return fail("unterminated escape");
      char e = s_[at_++];
      switch (e) {
        case '"':  out += '"';  break;
        case '\\': out += '\\'; break;
        case '/':  out += '/';  break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        case 'u': {
          unsigned cp = 0;
          if (!hex4(cp)) return false;
          // Combine a surrogate pair when a matching low surrogate follows.
          if (cp >= 0xD800 && cp <= 0xDBFF && at_ + 1 < s_.size() &&
              s_[at_] == '\\' && s_[at_ + 1] == 'u') {
            size_t save = at_;
            at_ += 2;
            unsigned lo = 0;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              unsigned full = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              out += static_cast<char>(0xF0 | (full >> 18));
              out += static_cast<char>(0x80 | ((full >> 12) & 0x3F));
              out += static_cast<char>(0x80 | ((full >> 6) & 0x3F));
              out += static_cast<char>(0x80 | (full & 0x3F));
              break;
            }
            at_ = save; // not a pair — fall through and emit cp alone
          }
          appendUtf8(out, cp);
          break;
        }
        default: return fail("unknown escape");
      }
    }
  }

  bool array(JVal &v) {
    ++at_; // '['
    ++depth_;
    v.kind = JVal::Arr;
    v.arr  = std::make_shared<JArr>();
    skip();
    if (at_ < s_.size() && s_[at_] == ']') { ++at_; --depth_; return true; }
    while (true) {
      JVal item;
      if (!value(item)) return false;
      v.arr->push_back(std::move(item));
      skip();
      if (at_ >= s_.size()) return fail("unterminated array");
      if (s_[at_] == ',') { ++at_; continue; }
      if (s_[at_] == ']') { ++at_; --depth_; return true; }
      return fail("expected ',' or ']'");
    }
  }

  bool object(JVal &v) {
    ++at_; // '{'
    ++depth_;
    v.kind = JVal::Obj;
    v.obj  = std::make_shared<JObj>();
    skip();
    if (at_ < s_.size() && s_[at_] == '}') { ++at_; --depth_; return true; }
    while (true) {
      skip();
      std::string key;
      if (!string(key)) return false;
      skip();
      if (at_ >= s_.size() || s_[at_] != ':') return fail("expected ':'");
      ++at_;
      JVal val;
      if (!value(val)) return false;
      v.obj->emplace_back(std::move(key), std::move(val));
      skip();
      if (at_ >= s_.size()) return fail("unterminated object");
      if (s_[at_] == ',') { ++at_; continue; }
      if (s_[at_] == '}') { ++at_; --depth_; return true; }
      return fail("expected ',' or '}'");
    }
  }
};

const JVal *member(const JVal &v, const char *key) {
  if (v.kind != JVal::Obj || !v.obj) return nullptr;
  for (const auto &kv : *v.obj)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

float numOr(const JVal &v, const char *key, float def) {
  const JVal *m = member(v, key);
  return (m && m->kind == JVal::Num) ? static_cast<float>(m->num) : def;
}

std::string strOr(const JVal &v, const char *key) {
  const JVal *m = member(v, key);
  return (m && m->kind == JVal::Str) ? m->str : std::string{};
}

bool boolOr(const JVal &v, const char *key, bool def) {
  const JVal *m = member(v, key);
  return (m && m->kind == JVal::Bool) ? m->b : def;
}

std::string toLower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

// ── Registry lookup ───────────────────────────────────────────────────────────

int PlotterRegistry::indexOfId(const std::string &id) const {
  for (size_t i = 0; i < plotters.size(); ++i)
    if (plotters[i].id == id) return static_cast<int>(i);
  return -1;
}

int PlotterRegistry::indexOfName(const std::string &name) const {
  for (size_t i = 0; i < plotters.size(); ++i)
    if (plotters[i].name == name) return static_cast<int>(i);
  return -1;
}

// ── Parsing ───────────────────────────────────────────────────────────────────

bool parsePlotterRegistry(const std::string &json, PlotterRegistry &out,
                          std::string &err) {
  out.plotters.clear();
  err.clear();

  JVal     root;
  JParser  parser(json);
  if (!parser.parse(root)) {
    err = "malformed JSON: " + parser.error();
    return false;
  }

  const JVal *list = member(root, "plotters");
  if (!list || list->kind != JVal::Arr || !list->arr) {
    err = "no \"plotters\" array";
    return false;
  }

  for (const JVal &e : *list->arr) {
    if (e.kind != JVal::Obj) continue;

    // An entry without a plot area describes nothing we can draw — skip it
    // rather than rejecting the whole file.
    const JVal *area = member(e, "max_area");
    if (!area || area->kind != JVal::Obj) continue;

    Plotter p;
    p.id        = strOr(e, "id");
    p.name      = strOr(e, "name");
    p.hpglModel = strOr(e, "hpgl_model");
    p.note      = strOr(e, "note");
    p.defaultForModel = boolOr(e, "default_for_model", false);

    p.maxArea.x  = numOr(*area, "x",  0.f);
    p.maxArea.y  = numOr(*area, "y",  0.f);
    p.maxArea.x2 = numOr(*area, "x2", 0.f);
    p.maxArea.y2 = numOr(*area, "y2", 0.f);
    // Tolerate an entry written with the corners the wrong way round.
    if (p.maxArea.x2 < p.maxArea.x) std::swap(p.maxArea.x, p.maxArea.x2);
    if (p.maxArea.y2 < p.maxArea.y) std::swap(p.maxArea.y, p.maxArea.y2);

    if (const JVal *m = member(e, "margin")) {
      p.margin.left   = numOr(*m, "left",   0.f);
      p.margin.bottom = numOr(*m, "bottom", 0.f);
      p.margin.right  = numOr(*m, "right",  0.f);
      p.margin.top    = numOr(*m, "top",    0.f);
    }

    if (const JVal *u = member(e, "units_per_mm")) {
      p.unitsPerMmX = numOr(*u, "x", 40.f);
      p.unitsPerMmY = numOr(*u, "y", 40.f);
    }
    // A zero or negative scale would make every mm conversion nonsense.
    if (p.unitsPerMmX <= 0.f) p.unitsPerMmX = 40.f;
    if (p.unitsPerMmY <= 0.f) p.unitsPerMmY = 40.f;

    if (const JVal *pa = member(e, "paper")) {
      const JVal *size = member(*pa, "size_mm");
      if (size && size->kind == JVal::Obj) {
        float w = numOr(*size, "w", 0.f);
        float h = numOr(*size, "h", 0.f);
        if (w > 0.f && h > 0.f) {
          p.paper.present  = true;
          p.paper.wMm      = w;
          p.paper.hMm      = h;
          p.paper.verified = boolOr(*pa, "verified", false);
          if (const JVal *off = member(*pa, "offset_mm")) {
            p.paper.offsetLeftMm   = numOr(*off, "left",   0.f);
            p.paper.offsetBottomMm = numOr(*off, "bottom", 0.f);
          }
        }
      }
    }

    out.plotters.push_back(std::move(p));
  }

  if (out.plotters.empty()) {
    err = "\"plotters\" array has no usable entries";
    return false;
  }
  return true;
}

bool loadPlotterRegistry(const std::string &path, PlotterRegistry &out,
                         std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  if (!parsePlotterRegistry(ss.str(), out, err)) {
    err = path + ": " + err;
    return false;
  }
  return true;
}

std::vector<std::string> defaultRegistryPaths() {
  std::vector<std::string> paths;

  if (const char *env = getenv("HPGL_VIEWER_PLOTTERS"); env && *env)
    paths.emplace_back(env);

  // Per-user copy wins over the installed one, so local edits survive an
  // upgrade.  Mirrors the layout config.cpp uses.
  const char *xdg  = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  if (xdg && *xdg)
    paths.push_back((fs::path(xdg) / "hpgl-viewer" / "plotters.json").string());
  else if (home && *home)
    paths.push_back(
        (fs::path(home) / ".config" / "hpgl-viewer" / "plotters.json").string());

  paths.push_back((fs::path(HPGL_DATADIR) / "hpgl-viewer" / "plotters.json")
                      .string());
  // Running straight out of a source tree.
  paths.emplace_back("data/plotters.json");

  return paths;
}

bool loadPlotterRegistryFromDefaults(PlotterRegistry &out,
                                     std::string &usedPath, std::string &err) {
  usedPath.clear();
  std::string tried;
  for (const std::string &p : defaultRegistryPaths()) {
    std::string one;
    if (loadPlotterRegistry(p, out, one)) {
      usedPath = p;
      err.clear();
      return true;
    }
    if (!tried.empty()) tried += "\n";
    tried += "  " + one;
  }
  err = "no plotter registry found. Tried:\n" + tried;
  return false;
}

// ── Geometry ──────────────────────────────────────────────────────────────────

Rect effectivePlotArea(const Plotter &p) {
  Rect r;
  r.x  = p.maxArea.x  + p.margin.left;
  r.y  = p.maxArea.y  + p.margin.bottom;
  r.x2 = p.maxArea.x2 - p.margin.right;
  r.y2 = p.maxArea.y2 - p.margin.top;
  // Margins that meet or cross collapse the rect instead of inverting it.
  if (r.x2 < r.x) r.x2 = r.x;
  if (r.y2 < r.y) r.y2 = r.y;
  return r;
}

bool paperRect(const Plotter &p, Rect &out) {
  if (!p.paper.present) return false;
  out.x  = p.maxArea.x - p.paper.offsetLeftMm   * p.unitsPerMmX;
  out.y  = p.maxArea.y - p.paper.offsetBottomMm * p.unitsPerMmY;
  out.x2 = out.x + p.paper.wMm * p.unitsPerMmX;
  out.y2 = out.y + p.paper.hMm * p.unitsPerMmY;
  return true;
}

float Clearance::worst() const {
  return std::min(std::min(left, right), std::min(bottom, top));
}

Clearance clearanceMm(const Rect &inner, const Rect &outer,
                      float unitsPerMmX, float unitsPerMmY) {
  if (unitsPerMmX <= 0.f) unitsPerMmX = 40.f;
  if (unitsPerMmY <= 0.f) unitsPerMmY = 40.f;
  Clearance c;
  c.left   = (inner.x  - outer.x)  / unitsPerMmX;
  c.right  = (outer.x2 - inner.x2) / unitsPerMmX;
  c.bottom = (inner.y  - outer.y)  / unitsPerMmY;
  c.top    = (outer.y2 - inner.y2) / unitsPerMmY;
  return c;
}

int matchPlotterByFilename(const PlotterRegistry &reg,
                           const std::string &filename) {
  const std::string hay = toLower(filename);
  int    best    = -1;
  size_t bestLen = 0;
  for (size_t i = 0; i < reg.plotters.size(); ++i) {
    const std::string needle = toLower(reg.plotters[i].name);
    if (needle.empty() || needle.size() <= bestLen) continue;
    if (hay.find(needle) == std::string::npos) continue;
    best    = static_cast<int>(i);
    bestLen = needle.size();
  }
  return best;
}
