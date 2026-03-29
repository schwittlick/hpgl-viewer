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

void initPenColors(PenStyle pens[8]) {
  ImVec4 defaults[] = {
      {0.05f, 0.05f, 0.05f, 1}, // 1 black
      {0.8f,  0.1f,  0.1f,  1}, // 2 red
      {0.1f,  0.5f,  0.1f,  1}, // 3 green
      {0.1f,  0.1f,  0.8f,  1}, // 4 blue
      {0.7f,  0.5f,  0.0f,  1}, // 5 orange
      {0.5f,  0.0f,  0.5f,  1}, // 6 purple
      {0.0f,  0.5f,  0.5f,  1}, // 7 teal
      {0.4f,  0.4f,  0.4f,  1}, // 8 grey
  };
  for (int i = 0; i < 8; ++i)
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
              const HpglDoc &doc, const DrawParams &p, PenUpRenderer &renderer) {
  dl->PushClipRect(origin, {origin.x + canvasW, origin.y + canvasH}, true);

  float cosR = cosf(p.rotation);
  float sinR = sinf(p.rotation);

  if (p.showCoords)
    drawCoordinateSystem(dl, origin, canvasW, canvasH, doc, p);

  // Compute visible HPGL AABB by inverse-transforming the 4 canvas corners.
  // This gives a conservative bounding box used to cull off-screen strokes.
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

  // Pen-down strokes (CPU path)
  for (const auto &stroke : doc.strokes) {
    if (stroke.points.empty()) continue;
    // Cull strokes entirely outside the visible HPGL area
    if (stroke.bboxMax.x < visMinX || stroke.bboxMin.x > visMaxX ||
        stroke.bboxMax.y < visMinY || stroke.bboxMin.y > visMaxY) continue;
    int pi = std::max(0, std::min(stroke.pen - 1, 7));
    ImU32 col = ImGui::ColorConvertFloat4ToU32(p.pens[pi].color);
    float screen_thick =
        std::max(1.0f, p.pens[pi].thickness * kHpglUnitsPerMm * p.scale);

    if (stroke.points.size() == 2 && stroke.points[0] == stroke.points[1]) {
      ImVec2 pt = xfPoint(stroke.points[0].x, stroke.points[0].y, origin,
                          p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
      dl->AddCircleFilled(pt, screen_thick * 0.5f, col);
      continue;
    }

    for (size_t i = 0; i + 1 < stroke.points.size(); ++i) {
      ImVec2 p0 = xfPoint(stroke.points[i].x,     stroke.points[i].y,     origin,
                          p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
      ImVec2 p1 = xfPoint(stroke.points[i + 1].x, stroke.points[i + 1].y, origin,
                          p.panX, p.panY, p.scale, canvasW, canvasH, cosR, sinR);
      dl->AddLine(p0, p1, col, screen_thick);
    }
  }

  // Pen-up moves — drawn via GPU VBO (one draw call for all segments)
  if (p.showPenUp && renderer.valid && renderer.vertexCount > 0) {
    float t  = p.penUpThreshold * kHpglUnitsPerCm;
    float cx = doc.minX + (p.fixLeftPct / 100.0f) * (doc.maxX - doc.minX);
    ImVec2 dp = ImGui::GetMainViewport()->Pos;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    renderer.origin      = origin;
    renderer.cW          = canvasW;
    renderer.cH          = canvasH;
    renderer.thresholdSq = t * t;
    renderer.cutoffX     = cx;
    renderer.dispX       = dp.x;
    renderer.dispY       = dp.y;
    renderer.dispW       = ds.x;
    renderer.dispH       = ds.y;
    renderer.scale       = p.scale;
    renderer.cosR        = cosR;
    renderer.sinR        = sinR;
    renderer.panX        = p.panX;
    renderer.panY        = p.panY;
    dl->AddCallback(penUpRenderCallback, &renderer);
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
