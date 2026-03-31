#include "renderer.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ── Shaders ───────────────────────────────────────────────────────────────────

// Vertex shader: transforms HPGL coords to NDC using pan/zoom/rotation uniforms.
// NDC conversion mirrors ImGui's own orthographic projection exactly:
//   ndcX = (sx - DisplayPos.x) / DisplaySize.x * 2 - 1
//   ndcY = 1 - (sy - DisplayPos.y) / DisplaySize.y * 2
static const char *kPenUpVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2  aPos;    // HPGL coordinates
layout(location = 1) in float aLenSq;  // squared segment length (HPGL units²)
layout(location = 2) in float aStartX; // HPGL X of segment start (same for both verts)

uniform float uScale, uCosR, uSinR, uPanX, uPanY;
uniform float uOriginX, uOriginY, uCanvasW, uCanvasH;
uniform float uDispX, uDispY, uDispW, uDispH;  // ImGui DisplayPos / DisplaySize

out float vLenSq;
out float vStartX;

void main() {
    vLenSq  = aLenSq;
    vStartX = aStartX;
    // HPGL → rotated screen space (matches xfPoint() in CPU code)
    float lx = aPos.x * uScale + uPanX - uCanvasW * 0.5;
    float ly = aPos.y * uScale + uPanY - uCanvasH * 0.5;
    float sx = uOriginX + lx * uCosR - ly * uSinR + uCanvasW * 0.5;
    float sy = uOriginY + lx * uSinR + ly * uCosR + uCanvasH * 0.5;
    // screen → NDC using ImGui's projection (DisplayPos / DisplaySize)
    gl_Position = vec4(
        (sx - uDispX) / uDispW * 2.0 - 1.0,
        1.0 - (sy - uDispY) / uDispH * 2.0,
        0.0, 1.0);
}
)glsl";

// Vertex shader for pen-down strokes.
// Each segment is a quad extended by uHalfWidth beyond both endpoints (for caps).
// aPos  = HPGL position of this corner (p0 or p1).
// aP0/aP1 = canonical segment start/end (same for all 6 vertices of a segment).
// aSide = +1 or -1: perpendicular side.
// Outputs local capsule coordinates (vU along axis, vV perpendicular) for the SDF.
static const char *kStrokeVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2  aPos;
layout(location = 1) in vec2  aP0;
layout(location = 2) in vec2  aP1;
layout(location = 3) in float aSide;

uniform float uScale, uCosR, uSinR, uPanX, uPanY;
uniform float uOriginX, uOriginY, uCanvasW, uCanvasH;
uniform float uDispX, uDispY, uDispW, uDispH;
uniform float uHalfWidth;

flat out float vL;
out float vU;
out float vV;

vec2 toScreen(vec2 p) {
    float lx = p.x * uScale + uPanX - uCanvasW * 0.5;
    float ly = p.y * uScale + uPanY - uCanvasH * 0.5;
    return vec2(uOriginX + lx * uCosR - ly * uSinR + uCanvasW * 0.5,
                uOriginY + lx * uSinR + ly * uCosR + uCanvasH * 0.5);
}

void main() {
    vec2 sp  = toScreen(aPos);
    vec2 sp0 = toScreen(aP0);
    vec2 sp1 = toScreen(aP1);
    vec2 d   = sp1 - sp0;
    float L  = length(d);
    vL = L;
    vec2 dir  = (L > 0.0001) ? d / L : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Extend the quad outward along the axis at both ends for the rounded cap region.
    float u_base   = dot(sp - sp0, dir);
    float end_sign = (u_base > L * 0.5) ? 1.0 : -1.0;
    vec2  s        = sp + perp * aSide * uHalfWidth + dir * end_sign * uHalfWidth;

    vU = u_base + end_sign * uHalfWidth;
    vV = aSide * uHalfWidth;

    gl_Position = vec4(
        (s.x - uDispX) / uDispW * 2.0 - 1.0,
        1.0 - (s.y - uDispY) / uDispH * 2.0,
        0.0, 1.0);
}
)glsl";

static const char *kStrokeFragSrc = R"glsl(
#version 330 core
flat in float vL;
in float vU;
in float vV;
uniform float uHalfWidth;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    // Capsule SDF: distance from this fragment to the nearest point on the segment.
    float nearest_u = clamp(vU, 0.0, vL);
    float dist = length(vec2(nearest_u - vU, vV));
    if (dist > uHalfWidth) discard;
    fragColor = uColor;
}
)glsl";

static const char *kPenUpFragSrc = R"glsl(
#version 330 core
in float vLenSq;
in float vStartX;
uniform float uThresholdSq;
uniform float uCutoffX;   // HPGL X cutoff (left-zone boundary)
out vec4 fragColor;

void main() {
    bool isLong = vLenSq  > uThresholdSq;
    bool inZone = vStartX <= uCutoffX;
    if (isLong && inZone)
        fragColor = vec4(220.0/255.0,  50.0/255.0,  50.0/255.0, 200.0/255.0); // red: will fix
    else if (isLong)
        fragColor = vec4(220.0/255.0, 150.0/255.0,  50.0/255.0, 180.0/255.0); // orange: long, skipped
    else
        fragColor = vec4( 60.0/255.0, 220.0/255.0, 100.0/255.0, 160.0/255.0); // green: short
}
)glsl";

// ── Pen styling ───────────────────────────────────────────────────────────────

void initPenColors(PenStyle pens[10]) {
  ImVec4 defaults[] = {
      {1.00f, 0.00f, 0.00f, 1}, // 1  red
      {0.00f, 0.60f, 0.00f, 1}, // 2  green
      {0.00f, 0.00f, 0.70f, 1}, // 3  blue
      {1.00f, 0.65f, 0.00f, 1}, // 4  orange
      {0.35f, 0.15f, 0.03f, 1}, // 5  brown
      {0.00f, 0.00f, 0.00f, 1}, // 6  black
      {1.00f, 1.00f, 0.00f, 1}, // 7  yellow
      {0.40f, 0.00f, 0.60f, 1}, // 8  purple
      {0.45f, 0.65f, 1.00f, 1}, // 9  light blue
      {1.00f, 0.00f, 1.00f, 1}, // 10 magenta
  };
  for (int i = 0; i < 10; ++i)
    pens[i].color = defaults[i];
}

// ── Coordinate helpers ────────────────────────────────────────────────────────

ImVec2 unrotateCanvas(float mx, float my, float cW, float cH,
                      float cosR, float sinR) {
  float u = mx - cW * 0.5f;
  float v = my - cH * 0.5f;
  return {u * cosR + v * sinR + cW * 0.5f,
         -u * sinR + v * cosR + cH * 0.5f};
}

ImVec2 xfPoint(float hx, float hy, ImVec2 origin,
               float panX, float panY, float scale,
               float cW, float cH, float cosR, float sinR) {
  float sx = hx * scale + panX - cW * 0.5f;
  float sy = hy * scale + panY - cH * 0.5f;
  return {origin.x + sx * cosR - sy * sinR + cW * 0.5f,
          origin.y + sx * sinR + sy * cosR + cH * 0.5f};
}

// ── GPU pen-up renderer ───────────────────────────────────────────────────────

void PenUpRenderer::init() {
  auto compile = [](GLenum type, const char *src) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[512];
      glGetShaderInfoLog(s, sizeof(log), nullptr, log);
      fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
  };
  GLuint vert = compile(GL_VERTEX_SHADER,   kPenUpVertSrc);
  GLuint frag = compile(GL_FRAGMENT_SHADER, kPenUpFragSrc);
  program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);
  {
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[512];
      glGetProgramInfoLog(program, sizeof(log), nullptr, log);
      fprintf(stderr, "Shader link error: %s\n", log);
    }
  }
  glDeleteShader(vert);
  glDeleteShader(frag);

  uScale       = glGetUniformLocation(program, "uScale");
  uCosR        = glGetUniformLocation(program, "uCosR");
  uSinR        = glGetUniformLocation(program, "uSinR");
  uPanX        = glGetUniformLocation(program, "uPanX");
  uPanY        = glGetUniformLocation(program, "uPanY");
  uOriginX     = glGetUniformLocation(program, "uOriginX");
  uOriginY     = glGetUniformLocation(program, "uOriginY");
  uCanvasW     = glGetUniformLocation(program, "uCanvasW");
  uCanvasH     = glGetUniformLocation(program, "uCanvasH");
  uDispX       = glGetUniformLocation(program, "uDispX");
  uDispY       = glGetUniformLocation(program, "uDispY");
  uDispW       = glGetUniformLocation(program, "uDispW");
  uDispH       = glGetUniformLocation(program, "uDispH");
  uThresholdSq = glGetUniformLocation(program, "uThresholdSq");
  uCutoffX     = glGetUniformLocation(program, "uCutoffX");

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  valid = true;
}

void PenUpRenderer::upload(const HpglDoc &doc) {
  if (!valid) return;
  // 4 floats per vertex: x, y, lenSq, startX — 2 vertices per pen-up segment
  std::vector<float> data;
  data.reserve(doc.strokes.size() * 8);
  for (size_t i = 0; i + 1 < doc.strokes.size(); ++i) {
    const auto &a = doc.strokes[i];
    const auto &b = doc.strokes[i + 1];
    if (a.points.empty() || b.points.empty()) continue;
    float x0 = a.points.back().x,  y0 = a.points.back().y;
    float x1 = b.points.front().x, y1 = b.points.front().y;
    float dx = x1 - x0, dy = y1 - y0;
    float lenSq = dx*dx + dy*dy;
    // startX is identical for both vertices of the segment
    data.insert(data.end(), {x0, y0, lenSq, x0,
                              x1, y1, lenSq, x0});
  }
  vertexCount = (int)(data.size() / 4);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(data.size() * sizeof(float)),
               data.data(), GL_STATIC_DRAW);
  constexpr int stride = 4 * sizeof(float);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PenUpRenderer::draw() const {
  if (!valid || vertexCount == 0) return;
  glUseProgram(program);
  glUniform1f(uScale,       scale);
  glUniform1f(uCosR,        cosR);
  glUniform1f(uSinR,        sinR);
  glUniform1f(uPanX,        panX);
  glUniform1f(uPanY,        panY);
  glUniform1f(uOriginX,     origin.x);
  glUniform1f(uOriginY,     origin.y);
  glUniform1f(uCanvasW,     cW);
  glUniform1f(uCanvasH,     cH);
  glUniform1f(uDispX,       dispX);
  glUniform1f(uDispY,       dispY);
  glUniform1f(uDispW,       dispW);
  glUniform1f(uDispH,       dispH);
  glUniform1f(uThresholdSq, thresholdSq);
  glUniform1f(uCutoffX,     cutoffX);
  glBindVertexArray(vao);
  glDrawArrays(GL_LINES, 0, vertexCount);
  glBindVertexArray(0);
}

void penUpRenderCallback(const ImDrawList*, const ImDrawCmd *cmd) {
  auto *r = static_cast<PenUpRenderer*>(cmd->UserCallbackData);

  // Convert ImGui clip rect to GL scissor (framebuffer coords, y-flipped).
  ImDrawData *dd = ImGui::GetDrawData();
  float scaleX = dd->FramebufferScale.x, scaleY = dd->FramebufferScale.y;
  float ox = dd->DisplayPos.x,           oy     = dd->DisplayPos.y;
  int x1 = (int)((cmd->ClipRect.x - ox) * scaleX);
  int y1 = (int)((cmd->ClipRect.y - oy) * scaleY);
  int x2 = (int)((cmd->ClipRect.z - ox) * scaleX);
  int y2 = (int)((cmd->ClipRect.w - oy) * scaleY);
  glScissor(x1, r->fbH - y2, x2 - x1, y2 - y1);

  r->draw();
}

// ── GPU stroke renderer ───────────────────────────────────────────────────────

void StrokeRenderer::init() {
  auto compile = [](GLenum type, const char *src) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[512];
      glGetShaderInfoLog(s, sizeof(log), nullptr, log);
      fprintf(stderr, "StrokeRenderer shader error: %s\n", log);
    }
    return s;
  };
  GLuint vert = compile(GL_VERTEX_SHADER,   kStrokeVertSrc);
  GLuint frag = compile(GL_FRAGMENT_SHADER, kStrokeFragSrc);
  program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);
  {
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[512];
      glGetProgramInfoLog(program, sizeof(log), nullptr, log);
      fprintf(stderr, "StrokeRenderer link error: %s\n", log);
    }
  }
  glDeleteShader(vert);
  glDeleteShader(frag);

  uScale   = glGetUniformLocation(program, "uScale");
  uCosR    = glGetUniformLocation(program, "uCosR");
  uSinR    = glGetUniformLocation(program, "uSinR");
  uPanX    = glGetUniformLocation(program, "uPanX");
  uPanY    = glGetUniformLocation(program, "uPanY");
  uOriginX = glGetUniformLocation(program, "uOriginX");
  uOriginY = glGetUniformLocation(program, "uOriginY");
  uCanvasW = glGetUniformLocation(program, "uCanvasW");
  uCanvasH = glGetUniformLocation(program, "uCanvasH");
  uDispX   = glGetUniformLocation(program, "uDispX");
  uDispY   = glGetUniformLocation(program, "uDispY");
  uDispW   = glGetUniformLocation(program, "uDispW");
  uDispH   = glGetUniformLocation(program, "uDispH");
  uColor     = glGetUniformLocation(program, "uColor");
  uHalfWidth = glGetUniformLocation(program, "uHalfWidth");

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  valid = true;
}

void StrokeRenderer::upload(const HpglDoc &doc) {
  if (!valid) return;

  // Each segment is rendered as a quad extended beyond both endpoints for rounded caps.
  // Vertex layout: (pos.x, pos.y, p0.x, p0.y, p1.x, p1.y, side) — 7 floats per vertex.
  // p0/p1 are the canonical segment endpoints (same for all 6 vertices of a segment).
  // Skip single-point dot strokes (handled by CPU dot renderer).
  std::vector<float> penData[10];
  for (const auto &stroke : doc.strokes) {
    if (stroke.points.size() < 2) continue;
    if (stroke.points.size() == 2 && stroke.points[0] == stroke.points[1]) continue;
    int pi = std::max(0, std::min(stroke.pen - 1, 9));
    for (size_t i = 0; i + 1 < stroke.points.size(); ++i) {
      float x0 = stroke.points[i].x,   y0 = stroke.points[i].y;
      float x1 = stroke.points[i+1].x, y1 = stroke.points[i+1].y;
      // Two triangles (A,B,D) and (A,D,C) where:
      //   A = p0 top, B = p0 bottom, C = p1 top, D = p1 bottom
      // Layout: (pos, p0, p1, side)
      float verts[6][7] = {
        {x0, y0,  x0, y0, x1, y1,  +1.f}, // A
        {x0, y0,  x0, y0, x1, y1,  -1.f}, // B
        {x1, y1,  x0, y0, x1, y1,  -1.f}, // D
        {x0, y0,  x0, y0, x1, y1,  +1.f}, // A
        {x1, y1,  x0, y0, x1, y1,  -1.f}, // D
        {x1, y1,  x0, y0, x1, y1,  +1.f}, // C
      };
      for (auto &v : verts)
        for (float f : v)
          penData[pi].push_back(f);
    }
  }

  // Concatenate into one VBO, record per-pen vertex offset/count.
  std::vector<float> vboData;
  int vertOffset = 0;
  for (int i = 0; i < 10; ++i) {
    ranges[i].offset = vertOffset;
    ranges[i].count  = (int)(penData[i].size() / 7); // 7 floats per vertex
    vboData.insert(vboData.end(), penData[i].begin(), penData[i].end());
    vertOffset += ranges[i].count;
  }

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(vboData.size() * sizeof(float)),
               vboData.data(), GL_STATIC_DRAW);
  constexpr int stride = 7 * sizeof(float);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);                    // aPos
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));  // aP0
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));  // aP1
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));  // aSide
  glEnableVertexAttribArray(3);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StrokeRenderer::draw() const {
  if (!valid || !pens) return;
  glUseProgram(program);
  glUniform1f(uScale,   scale);
  glUniform1f(uCosR,    cosR);
  glUniform1f(uSinR,    sinR);
  glUniform1f(uPanX,    panX);
  glUniform1f(uPanY,    panY);
  glUniform1f(uOriginX, origin.x);
  glUniform1f(uOriginY, origin.y);
  glUniform1f(uCanvasW, cW);
  glUniform1f(uCanvasH, cH);
  glUniform1f(uDispX,   dispX);
  glUniform1f(uDispY,   dispY);
  glUniform1f(uDispW,   dispW);
  glUniform1f(uDispH,   dispH);

  glBindVertexArray(vao);
  for (int i = 0; i < 10; ++i) {
    if (ranges[i].count == 0) continue;
    const auto &c = pens[i].color;
    glUniform4f(uColor, c.x, c.y, c.z, c.w);
    glUniform1f(uHalfWidth, pens[i].thickness * kHpglUnitsPerMm * scale * 0.5f);
    glDrawArrays(GL_TRIANGLES, ranges[i].offset, ranges[i].count);
  }
  glBindVertexArray(0);
}

void strokeRenderCallback(const ImDrawList*, const ImDrawCmd *cmd) {
  auto *r = static_cast<StrokeRenderer*>(cmd->UserCallbackData);

  ImDrawData *dd = ImGui::GetDrawData();
  float scaleX = dd->FramebufferScale.x, scaleY = dd->FramebufferScale.y;
  float ox = dd->DisplayPos.x,           oy     = dd->DisplayPos.y;
  int x1 = (int)((cmd->ClipRect.x - ox) * scaleX);
  int y1 = (int)((cmd->ClipRect.y - oy) * scaleY);
  int x2 = (int)((cmd->ClipRect.z - ox) * scaleX);
  int y2 = (int)((cmd->ClipRect.w - oy) * scaleY);
  glScissor(x1, r->fbH - y2, x2 - x1, y2 - y1);

  r->draw();
}

// ── Coordinate system overlay ────────────────────────────────────────────────

// Choose a grid step (in HPGL units) that yields 5–20 visible grid lines.
static float pickGridStep(float hpglSpan) {
  // steps in HPGL units: 1 mm, 5 mm, 1 cm, 2 cm, 5 cm, 10 cm, 20 cm, 50 cm
  static const float kSteps[] = {40, 200, 400, 800, 2000, 4000, 8000, 20000};
  for (float s : kSteps)
    if (hpglSpan / s < 20.f) return s;
  return kSteps[std::size(kSteps) - 1];
}

static void drawCoordinateSystem(ImDrawList *dl, ImVec2 origin,
                                  float cW, float cH,
                                  const HpglDoc &doc, const DrawParams &p) {
  const float cosR = cosf(p.rotation);
  const float sinR = sinf(p.rotation);

  // HPGL viewport (what's currently visible, with a small margin)
  float invScale = 1.f / p.scale;
  float visW = cW * invScale;
  float visH = cH * invScale;
  float hMinX = (-p.panX) * invScale;
  float hMinY = (-p.panY) * invScale;
  float hMaxX = hMinX + visW;
  float hMaxY = hMinY + visH;

  float stepX = pickGridStep(visW);
  float stepY = pickGridStep(visH);

  ImU32 gridCol  = IM_COL32( 80, 120, 200,  70);
  ImU32 axisCol  = IM_COL32( 80, 120, 200, 160);
  ImU32 labelCol = IM_COL32( 60,  90, 170, 220);

  auto snap = [](float v, float step) { return floorf(v / step) * step; };

  // Vertical grid lines (constant HPGL-X)
  for (float x = snap(hMinX, stepX); x <= hMaxX; x += stepX) {
    ImVec2 a = xfPoint(x, hMinY, origin, p.panX, p.panY, p.scale,
                       cW, cH, cosR, sinR);
    ImVec2 b = xfPoint(x, hMaxY, origin, p.panX, p.panY, p.scale,
                       cW, cH, cosR, sinR);
    ImU32 col = (fabsf(x) < 0.5f) ? axisCol : gridCol;
    dl->AddLine(a, b, col, 1.f);

    // Label: show value in mm or cm
    char buf[24];
    float mm = x / kHpglUnitsPerMm;
    if (fabsf(mm) < 0.01f) snprintf(buf, sizeof(buf), "0");
    else if (fabsf(mm) >= 10.f) snprintf(buf, sizeof(buf), "%.0fcm", mm / 10.f);
    else snprintf(buf, sizeof(buf), "%.0fmm", mm);
    // Place label near the bottom of the canvas (unrotated: just above bottom edge)
    ImVec2 lp = xfPoint(x, hMinY + visH * 0.97f, origin, p.panX, p.panY,
                        p.scale, cW, cH, cosR, sinR);
    dl->AddText(lp, labelCol, buf);
  }

  // Horizontal grid lines (constant HPGL-Y)
  for (float y = snap(hMinY, stepY); y <= hMaxY; y += stepY) {
    ImVec2 a = xfPoint(hMinX, y, origin, p.panX, p.panY, p.scale,
                       cW, cH, cosR, sinR);
    ImVec2 b = xfPoint(hMaxX, y, origin, p.panX, p.panY, p.scale,
                       cW, cH, cosR, sinR);
    ImU32 col = (fabsf(y) < 0.5f) ? axisCol : gridCol;
    dl->AddLine(a, b, col, 1.f);

    char buf[24];
    float mm = y / kHpglUnitsPerMm;
    if (fabsf(mm) < 0.01f) snprintf(buf, sizeof(buf), "0");
    else if (fabsf(mm) >= 10.f) snprintf(buf, sizeof(buf), "%.0fcm", mm / 10.f);
    else snprintf(buf, sizeof(buf), "%.0fmm", mm);
    ImVec2 lp = xfPoint(hMinX + visW * 0.01f, y, origin, p.panX, p.panY,
                        p.scale, cW, cH, cosR, sinR);
    dl->AddText(lp, labelCol, buf);
  }

  // Document bounding-box outline
  if (!doc.empty()) {
    ImVec2 corners[4] = {
      xfPoint(doc.minX, doc.minY, origin, p.panX, p.panY, p.scale, cW, cH, cosR, sinR),
      xfPoint(doc.maxX, doc.minY, origin, p.panX, p.panY, p.scale, cW, cH, cosR, sinR),
      xfPoint(doc.maxX, doc.maxY, origin, p.panX, p.panY, p.scale, cW, cH, cosR, sinR),
      xfPoint(doc.minX, doc.maxY, origin, p.panX, p.panY, p.scale, cW, cH, cosR, sinR),
    };
    ImU32 boxCol = IM_COL32(80, 120, 200, 120);
    dl->AddPolyline(corners, 4, boxCol, ImDrawFlags_Closed, 1.f);
  }
}

// ── Scene drawing ─────────────────────────────────────────────────────────────

void drawHpgl(ImDrawList *dl, ImVec2 origin, float canvasW, float canvasH,
              const HpglDoc &doc, const DrawParams &p,
              PenUpRenderer &penUpRenderer, StrokeRenderer &strokeRenderer) {
  dl->PushClipRect(origin, {origin.x + canvasW, origin.y + canvasH}, true);

  float cosR = cosf(p.rotation);
  float sinR = sinf(p.rotation);

  if (p.showCoords)
    drawCoordinateSystem(dl, origin, canvasW, canvasH, doc, p);

  // GPU pen-down strokes — one draw call per pen (up to 8 total).
  if (strokeRenderer.valid) {
    ImVec2 dp = ImGui::GetMainViewport()->Pos;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    strokeRenderer.origin = origin;
    strokeRenderer.cW     = canvasW;
    strokeRenderer.cH     = canvasH;
    strokeRenderer.dispX  = dp.x;
    strokeRenderer.dispY  = dp.y;
    strokeRenderer.dispW  = ds.x;
    strokeRenderer.dispH  = ds.y;
    strokeRenderer.scale  = p.scale;
    strokeRenderer.cosR   = cosR;
    strokeRenderer.sinR   = sinR;
    strokeRenderer.panX   = p.panX;
    strokeRenderer.panY   = p.panY;
    strokeRenderer.pens   = p.pens;
    dl->AddCallback(strokeRenderCallback, &strokeRenderer);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
  }

  // Dot strokes (single-point pen-8 waypoints) stay on CPU — there are very few.
  {
    float visMinX =  1e30f, visMinY =  1e30f;
    float visMaxX = -1e30f, visMaxY = -1e30f;
    for (int ci = 0; ci < 4; ++ci) {
      float cx = (ci & 1) ? canvasW : 0.f;
      float cy = (ci & 2) ? canvasH : 0.f;
      float lx = (cx - canvasW * 0.5f) * cosR + (cy - canvasH * 0.5f) * sinR;
      float ly = -(cx - canvasW * 0.5f) * sinR + (cy - canvasH * 0.5f) * cosR;
      float hx = (lx - p.panX + canvasW * 0.5f) / p.scale;
      float hy = (ly - p.panY + canvasH * 0.5f) / p.scale;
      visMinX = std::min(visMinX, hx); visMinY = std::min(visMinY, hy);
      visMaxX = std::max(visMaxX, hx); visMaxY = std::max(visMaxY, hy);
    }
    for (const auto &stroke : doc.strokes) {
      if (stroke.points.size() != 2) continue;
      if (!(stroke.points[0] == stroke.points[1])) continue;
      if (stroke.bboxMax.x < visMinX || stroke.bboxMin.x > visMaxX ||
          stroke.bboxMax.y < visMinY || stroke.bboxMin.y > visMaxY) continue;
      int pi = std::max(0, std::min(stroke.pen - 1, 9));
      ImU32 col = ImGui::ColorConvertFloat4ToU32(p.pens[pi].color);
      float screen_thick = std::max(1.0f, p.pens[pi].thickness * kHpglUnitsPerMm * p.scale);
      ImVec2 pt = xfPoint(stroke.points[0].x, stroke.points[0].y, origin,
                          p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
      dl->AddCircleFilled(pt, screen_thick * 0.5f, col);
    }
  }

  // Pen-up moves — drawn via GPU VBO (one draw call for all segments)
  if (p.showPenUp && penUpRenderer.valid && penUpRenderer.vertexCount > 0) {
    float t  = p.penUpThreshold * kHpglUnitsPerCm;
    float cx = doc.minX + (p.fixLeftPct / 100.0f) * (doc.maxX - doc.minX);
    ImVec2 dp = ImGui::GetMainViewport()->Pos;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    penUpRenderer.origin      = origin;
    penUpRenderer.cW          = canvasW;
    penUpRenderer.cH          = canvasH;
    penUpRenderer.thresholdSq = t * t;
    penUpRenderer.cutoffX     = cx;
    penUpRenderer.dispX       = dp.x;
    penUpRenderer.dispY       = dp.y;
    penUpRenderer.dispW       = ds.x;
    penUpRenderer.dispH       = ds.y;
    penUpRenderer.scale       = p.scale;
    penUpRenderer.cosR        = cosR;
    penUpRenderer.sinR        = sinR;
    penUpRenderer.panX        = p.panX;
    penUpRenderer.panY        = p.panY;
    dl->AddCallback(penUpRenderCallback, &penUpRenderer);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    // Draw vertical cutoff line in HPGL space
    ImVec2 lineTop = xfPoint(cx, doc.minY, origin,
                             p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
    ImVec2 lineBot = xfPoint(cx, doc.maxY, origin,
                             p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
    dl->AddLine(lineTop, lineBot, IM_COL32(100, 200, 255, 200), 1.5f);
  }

  dl->PopClipRect();
}
