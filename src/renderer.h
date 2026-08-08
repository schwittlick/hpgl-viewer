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
#include "plotters.h"

#include <string>

// ── Pen styling ───────────────────────────────────────────────────────────────

struct PenStyle {
  ImVec4 color     = {0.1f, 0.1f, 0.1f, 1.0f};
  float  thickness = 0.3f; // mm
};

// Fill pens[0..9] with default colours.
void initPenColors(PenStyle pens[10]);

// ── Layer styling ─────────────────────────────────────────────────────────────
//
// A layer may override the per-pen palette with a single solid colour.  When
// `solid` is true, every stroke originating from that layer is drawn with
// `style` regardless of its HPGL pen index; otherwise the global pen palette
// applies as usual.
struct LayerStyle {
  bool     solid = false;
  PenStyle style;
};

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

  // One contiguous VBO range per (layer, pen) pair that has geometry.  draw()
  // resolves each bucket's colour from the layer override or the pen palette.
  struct StrokeBucket { int layer = 0; int pen = 0; int offset = 0; int count = 0; };
  std::vector<StrokeBucket> buckets;

  // Per-frame context — set before the ImGui draw callback fires.
  ImVec2 origin{};
  float  cW = 0, cH = 0;
  float  dispX = 0, dispY = 0, dispW = 1, dispH = 1;
  float  scale = 1.0f, cosR = 1.0f, sinR = 0.0f;
  float  panX  = 0.0f, panY = 0.0f;
  int    fbH   = 0;
  const PenStyle   *pens        = nullptr;
  const LayerStyle *layerStyles = nullptr; // indexed by Stroke::layer
  int               layerCount  = 0;

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

// ── Plotter bounds overlay ────────────────────────────────────────────────────
//
// Rectangles in HPGL units, drawn over the canvas so a drawing can be checked
// against the machine it will be plotted on.  All three are in the document's
// own coordinate space, so no transform is applied beyond the usual view one.
struct PlotterOverlay {
  bool show      = false; // master switch — nothing is drawn when false
  bool showPaper = false; // draw the sheet outline as well as the plot area
  bool hasPaper  = false; // the selected plotter carries paper data at all
  bool paperVerified = false; // false → sheet is drawn dashed as a guess

  Rect maxArea;  // furthest the pen can reach
  Rect effArea;  // maxArea inset by the plotter's margin
  Rect paper;    // the physical sheet (only when hasPaper)

  std::string label; // plotter name, captioned at the top-left of the sheet
};

// Draw the plotter/paper rectangles plus per-edge clearance readouts.  Edges
// the document overruns are drawn and labelled in red.
void drawPlotterOverlay(ImDrawList *dl, ImVec2 origin, float canvasW,
                        float canvasH, const HpglDoc &doc,
                        const PlotterOverlay &ov, float panX, float panY,
                        float scale, float rotation);

// ── Scene drawing ─────────────────────────────────────────────────────────────

struct DrawParams {
  float panX, panY, scale, rotation;
  bool  showPenUp;
  float penUpThreshold; // cm
  const PenStyle *pens; // pointer to array of 10
  const LayerStyle *layerStyles = nullptr; // per-layer overrides, indexed by layer
  int   layerCount = 0;
  bool  showCoords = false;
  // Plotter/paper bounds overlay, or nullptr for none.  Drawn on the CPU
  // alongside the grid, so it costs nothing on FBO-cached frames.
  const PlotterOverlay *plotter = nullptr;
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
