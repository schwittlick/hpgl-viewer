#pragma once

// GL extension prototypes must be requested before any GL header is included.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#ifndef GLFW_INCLUDE_GLEXT
#define GLFW_INCLUDE_GLEXT
#endif
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "hpgl_parser.h"
#include "hpgl_fix.h"

// ── Pen styling ───────────────────────────────────────────────────────────────

struct PenStyle {
  ImVec4 color     = {0.1f, 0.1f, 0.1f, 1.0f};
  float  thickness = 0.3f; // mm
};

// Fill pens[0..9] with default colours.
void initPenColors(PenStyle pens[10]);

// ── Coordinate helpers ────────────────────────────────────────────────────────

// Undo canvas rotation around its centre: screen-relative → pre-rotation coords.
ImVec2 unrotateCanvas(float mx, float my, float cW, float cH,
                      float cosR, float sinR);

// Transform an HPGL point to screen space.
ImVec2 xfPoint(float hx, float hy, ImVec2 origin,
               float panX, float panY, float scale,
               float cW, float cH, float cosR, float sinR);

// ── GPU pen-up renderer ───────────────────────────────────────────────────────

struct PenUpRenderer {
  GLuint vao = 0, vbo = 0, program = 0;
  int    vertexCount = 0;
  bool   valid = false;

  // Per-frame context — set before the ImGui draw callback fires.
  ImVec2 origin{};
  float  cW = 0, cH = 0;
  float  thresholdSq = 0;
  float  dispX = 0, dispY = 0, dispW = 1, dispH = 1;
  float  scale = 1.0f, cosR = 1.0f, sinR = 0.0f;
  float  panX  = 0.0f, panY = 0.0f;
  int    fbH   = 0;

  GLint uScale=-1, uCosR=-1, uSinR=-1, uPanX=-1, uPanY=-1;
  GLint uOriginX=-1, uOriginY=-1, uCanvasW=-1, uCanvasH=-1;
  GLint uDispX=-1, uDispY=-1, uDispW=-1, uDispH=-1;
  GLint uThresholdSq=-1;

  void init();
  void upload(const HpglDoc &doc);
  void draw() const;
};

// ImGui draw-list callback that issues the GPU pen-up draw call.
void penUpRenderCallback(const ImDrawList*, const ImDrawCmd *cmd);

// ── GPU stroke renderer ───────────────────────────────────────────────────────

struct StrokeRenderer {
  GLuint vao = 0, vbo = 0, program = 0;
  bool   valid = false;

  struct PenRange { int offset = 0; int count = 0; };
  PenRange ranges[10];

  // Per-frame context — set before the ImGui draw callback fires.
  ImVec2 origin{};
  float  cW = 0, cH = 0;
  float  dispX = 0, dispY = 0, dispW = 1, dispH = 1;
  float  scale = 1.0f, cosR = 1.0f, sinR = 0.0f;
  float  panX  = 0.0f, panY = 0.0f;
  int    fbH   = 0;
  const PenStyle *pens = nullptr;

  GLint uScale=-1, uCosR=-1, uSinR=-1, uPanX=-1, uPanY=-1;
  GLint uOriginX=-1, uOriginY=-1, uCanvasW=-1, uCanvasH=-1;
  GLint uDispX=-1, uDispY=-1, uDispW=-1, uDispH=-1;
  GLint uColor=-1, uHalfWidth=-1;

  void init();
  void upload(const HpglDoc &doc);
  void draw() const;
};

// ImGui draw-list callback that issues the GPU stroke draw calls.
void strokeRenderCallback(const ImDrawList*, const ImDrawCmd *cmd);

// ── Off-screen canvas (FBO cache) ─────────────────────────────────────────────
//
// Caches the GPU-rendered scene (strokes + pen-up moves) into a texture so
// the same content can be sampled back via ImGui::Image when canvas state
// is unchanged.  The CPU-side overlays (grid, dot circles, FPS box) keep
// running through the regular ImGui draw list each frame.
struct CanvasFbo {
  GLuint fbo = 0;
  GLuint tex = 0;
  int    W   = 0;
  int    H   = 0;
  bool   valid = false;

  void init();
  void resize(int newW, int newH);
  void destroy();
};

// ── Scene drawing ─────────────────────────────────────────────────────────────

struct DrawParams {
  float panX, panY, scale, rotation;
  bool  showPenUp;
  float penUpThreshold; // cm
  const PenStyle *pens; // pointer to array of 10
  bool  showCoords = false;
  // When non-zero, drawHpgl skips the GPU stroke + pen-up callbacks and
  // instead samples this OpenGL texture between the grid and the CPU-drawn
  // dot circles, so z-order matches the immediate path.
  GLuint fboTex = 0;
};

void drawHpgl(ImDrawList *dl, ImVec2 origin, float canvasW, float canvasH,
              const HpglDoc &doc, const DrawParams &p,
              PenUpRenderer &penUpRenderer, StrokeRenderer &strokeRenderer);

// Render the cached scene (GPU strokes + pen-up moves) into target.
// Restores the previously-bound framebuffer and viewport on return.
//
// fbScale is the framebuffer-pixels-per-logical-pixel ratio (1.0 on a
// non-HiDPI display, typically 2.0 on a 2x display).  The caller is
// expected to size target.W/target.H in framebuffer pixels (i.e.
// canvas_logical_W * fbScale); this function then scales pan, zoom, and
// line-thickness uniforms by fbScale so the shader projects HPGL into
// FBO pixels 1:1.  This matters because ImGui::Image samples the
// texture at framebuffer-pixel resolution — a logical-sized FBO would
// otherwise be bilinearly upscaled, blurring strokes and dulling
// colours.  Anisotropic fbScale is not supported (passed value applies
// to both axes).
void renderSceneToFbo(CanvasFbo &target,
                      const HpglDoc &doc,
                      const DrawParams &p,
                      PenUpRenderer &penUpRenderer,
                      StrokeRenderer &strokeRenderer,
                      float fbScale = 1.0f);
