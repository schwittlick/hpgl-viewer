#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "config.h"
#include "hpgl_fix.h"
#include "hpgl_parser.h"
#include "renderer.h"
#include "view_state.h"

#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// ─── File dialog
// ────────────────────────────────────────────────────────────

namespace fs = std::filesystem;

static std::string g_lastOpenDir;

static std::string openFileDialog() {
  std::string startDir = g_lastOpenDir;
  if (!startDir.empty() && startDir.back() != '/')
    startDir += '/';

  std::string cmd = "kdialog --getopenfilename '";
  cmd += shellEscapeSingleQuoted(startDir.empty() ? "." : startDir);
  cmd += "' '*.hpgl *.plt *.hgl' 2>/dev/null";

  FILE *f = popen(cmd.c_str(), "r");
  if (!f) return {};
  std::array<char, 4096> buf{};
  fgets(buf.data(), buf.size(), f);
  int rc = pclose(f);
  if (rc != 0) return {};

  std::string path(buf.data());
  if (!path.empty() && path.back() == '\n')
    path.pop_back();
  if (!path.empty()) {
    g_lastOpenDir = fs::path(path).parent_path().string();
    configSave("last_open_dir", g_lastOpenDir);
  }
  return path;
}

// ─── App state
// ────────────────────────────────────────────────────────────────

static HpglDoc g_doc;
static std::string g_filePath;
static char g_filePathBuf[4096] = ""; // PATH_MAX on Linux
static bool g_fitRequested = false;

// pan/zoom/rotation state
static float g_panX = 0, g_panY = 0, g_scale = 1.0f;
static float g_rotation = 0.0f;
static bool  g_showPenUp = false;
static float g_penUpThreshold =  10.0f; // cm
static float g_fixStepCm      =   2.0f; // cm between inserted waypoints
static float g_fixLeftPct      =  15.0f; // % of doc width from left that is eligible

// Framebuffer size (updated each frame)
static int g_fbW = 0, g_fbH = 0;

// Cached doc stats (recomputed on load)
static DocStats g_stats;
static void refreshDocStats() { g_stats = computeDocStats(g_doc); }

// fullscreen state
static bool g_isFullscreen = false;
static int g_windowedX = 100, g_windowedY = 100;
static int g_windowedW = 1400, g_windowedH = 900;

// pen styles (up to 8 pens)
static PenStyle g_pens[8];

static void toggleFullscreen(GLFWwindow *window) {
  if (!g_isFullscreen) {
    glfwGetWindowPos(window, &g_windowedX, &g_windowedY);
    glfwGetWindowSize(window, &g_windowedW, &g_windowedH);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    g_isFullscreen = true;
  } else {
    glfwSetWindowMonitor(window, nullptr, g_windowedX, g_windowedY,
                         g_windowedW, g_windowedH, 0);
    g_isFullscreen = false;
  }
}

static PenUpRenderer g_penUpRenderer;

// ─── Pen-up fix + HPGL export
// ────────────────────────────────────────────────────────────

static std::string g_fixStatus;  // shown in sidebar after fix/export
static bool        g_hasFixed = false; // true when g_doc holds a fixed (unsaved) result

// ─── Layout
// ────────────────────────────────────────────────────────────────────

static void applyDefaultLayout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

  ImGuiID left, right;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, &left, &right);
  ImGui::DockBuilderDockWindow("Controls", left);
  ImGui::DockBuilderDockWindow("Canvas", right);
  ImGui::DockBuilderFinish(dockspace_id);
}

// ─── Actions
// ─────────────────────────────────────────────────────────────────────

static void loadFile(const std::string &path) {
  g_filePath = path;
  strncpy(g_filePathBuf, path.c_str(), sizeof(g_filePathBuf) - 1);
  g_filePathBuf[sizeof(g_filePathBuf) - 1] = '\0';
  g_doc = HpglParser{}.parseFile(path);
  refreshDocStats();
  g_penUpRenderer.upload(g_doc);
  g_fitRequested = true;
  g_hasFixed = false;
  g_fixStatus.clear();
}

static void applyFix() {
  float cutoff = g_doc.minX + (g_fixLeftPct / 100.0f) * (g_doc.maxX - g_doc.minX);
  g_doc = fixLongPenUps(g_doc,
                        g_penUpThreshold * kHpglUnitsPerCm,
                        g_fixStepCm      * kHpglUnitsPerCm,
                        cutoff);
  refreshDocStats();
  g_penUpRenderer.upload(g_doc);
  g_fitRequested = true;
  g_hasFixed = true;
  g_fixStatus = "Fixed (not yet saved)";
}

// ─── Main
// ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  if (!glfwInit())
    return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window =
      glfwCreateWindow(1400, 900, "HPGL Viewer", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  g_penUpRenderer.init();

  initPenColors(g_pens);
  g_lastOpenDir = configLoad("last_open_dir");

  if (argc > 1)
    loadFile(argv[1]);

  while (!glfwWindowShouldClose(window)) {
    glfwWaitEvents();

    glfwGetFramebufferSize(window, &g_fbW, &g_fbH);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Key shortcuts (only when not typing in a text field)
    if (!io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_Q))
        glfwSetWindowShouldClose(window, 1);
      if (ImGui::IsKeyPressed(ImGuiKey_F))
        toggleFullscreen(window);
      if (ImGui::IsKeyPressed(ImGuiKey_C))
        g_fitRequested = true;
      if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        g_rotation += static_cast<float>(M_PI_2);
        g_fitRequested = true;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_E) && !g_filePath.empty())
        applyFix();
      if (ImGui::IsKeyPressed(ImGuiKey_O)) {
        std::string path = openFileDialog();
        if (!path.empty()) loadFile(path);
      }
    }

    // ── Top status bar ───────────────────────────────────────────────────
    if (ImGui::BeginMainMenuBar()) {
      if (g_filePath.empty()) {
        ImGui::TextDisabled("No file loaded");
      } else {
        std::string name = fs::path(g_filePath).filename().string();
        if (g_hasFixed) name += "  [unsaved fix]";
        ImGui::TextUnformatted(name.c_str());
      }
      ImGui::EndMainMenuBar();
    }

    // Full-window dockspace
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    static bool layout_initialized = false;
    if (!layout_initialized) {
      layout_initialized = true;
      ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspace_id);
      if (!node || node->IsLeafNode())
        applyDefaultLayout(dockspace_id);
    }

    // ── Sidebar ──────────────────────────────────────────────────────────
    ImGui::SetNextWindowSize({320, 600}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls");

    ImGui::SeparatorText("File");
    ImGui::InputText("##path", g_filePathBuf, sizeof(g_filePathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Open"))
      loadFile(g_filePathBuf);
    if (!g_filePath.empty()) {
      ImGui::TextDisabled("%s", g_filePath.c_str());
      ImGui::Text("Strokes: %zu", g_doc.strokes.size());
      ImGui::Text("Bounds: (%.0f,%.0f)-(%.0f,%.0f)", g_doc.minX, g_doc.minY,
                  g_doc.maxX, g_doc.maxY);
    }

    ImGui::SeparatorText("Fix");
    ImGui::BeginDisabled(g_filePath.empty());
    if (ImGui::Button("Fix long pen-up jumps  [E]"))
      applyFix();
    ImGui::BeginDisabled(!g_hasFixed);
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
      std::string out = fixedPath(g_filePath);
      if (exportHpgl(g_doc, out)) {
        g_filePath = out;
        strncpy(g_filePathBuf, out.c_str(), sizeof(g_filePathBuf) - 1);
        g_hasFixed = false;
        g_fixStatus = "Saved: " + out;
      } else {
        g_fixStatus = "Export failed: " + out;
      }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Threshold", &g_penUpThreshold, 1.0f, 200.0f, "%.0f cm");
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Waypoint spacing", &g_fixStepCm, 0.5f, 20.0f, "%.1f cm");
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Left zone", &g_fixLeftPct, 0.0f, 100.0f, "%.0f%%");
    if (!g_fixStatus.empty())
      ImGui::TextWrapped("%s", g_fixStatus.c_str());

    ImGui::SeparatorText("View");
    ImGui::Checkbox("Show pen-up moves", &g_showPenUp);
    ImGui::Text("Scale: %.3f", g_scale);
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
      g_fitRequested = true;
    if (ImGui::Button("Reset zoom")) {
      g_scale = 1.0f;
      g_panX = 0;
      g_panY = 0;
    }

    ImGui::SeparatorText("Pen Styles");
    for (int i = 0; i < 8; ++i) {
      ImGui::PushID(i);
      char label[16];
      snprintf(label, sizeof(label), "Pen %d", i + 1);
      ImGui::ColorEdit4(label, &g_pens[i].color.x,
                        ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_AlphaBar);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100);
      ImGui::SliderFloat("##thick", &g_pens[i].thickness, 0.05f, 2.0f,
                         "%.2f mm");
      ImGui::PopID();
    }

    ImGui::End();

    // ── Canvas ───────────────────────────────────────────────────────────
    ImGui::SetNextWindowSize({1000, 700}, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Canvas", nullptr,
                 ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    float cW = canvasSize.x, cH = canvasSize.y;

    // Background
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, {canvasPos.x + cW, canvasPos.y + cH},
                      IM_COL32(245, 245, 240, 255));

    static ImVec2 lastCanvasSize = {0, 0};
    bool sizeChanged = (cW != lastCanvasSize.x || cH != lastCanvasSize.y);
    if (g_fitRequested || sizeChanged) {
      auto vs = fitToCanvas(cW, cH, g_doc, g_rotation);
      g_scale = vs.scale; g_panX = vs.panX; g_panY = vs.panY;
    }
    g_fitRequested = false;
    lastCanvasSize = {cW, cH};

    // Invisible interaction rect
    ImGui::InvisibleButton("##canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();

    // Pan — unrotate the screen-space delta into pre-rotation pan space
    float cosR = cosf(g_rotation);
    float sinR = sinf(g_rotation);
    auto applyPanDelta = [&](ImVec2 d) {
      g_panX += d.x * cosR + d.y * sinR;
      g_panY += -d.x * sinR + d.y * cosR;
    };
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      applyPanDelta(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f));
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      applyPanDelta(ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f));
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    // Zoom toward cursor — convert mouse canvas position to pre-rotation space first
    if (hovered && io.MouseWheel != 0.0f) {
      float factor = (io.MouseWheel > 0) ? 1.1f : 0.9f;
      float mx = io.MousePos.x - canvasPos.x;
      float my = io.MousePos.y - canvasPos.y;
      auto [px, py] = unrotateCanvas(mx, my, cW, cH, cosR, sinR);
      g_panX = px - (px - g_panX) * factor;
      g_panY = py - (py - g_panY) * factor;
      g_scale *= factor;
    }

    g_penUpRenderer.fbH = g_fbH;
    DrawParams dp{g_panX, g_panY, g_scale, g_rotation,
                  g_showPenUp, g_penUpThreshold, g_fixLeftPct, g_pens};
    drawHpgl(dl, canvasPos, cW, cH, g_doc, dp, g_penUpRenderer);

    // Stats overlay — top-right corner of canvas
    {
      char lines[4][48];
      snprintf(lines[0], sizeof(lines[0]), "%.0f FPS", io.Framerate);
      snprintf(lines[1], sizeof(lines[1]), "%d paths", g_stats.numPaths);
      snprintf(lines[2], sizeof(lines[2]), "pd %.2f m", g_stats.penDownMm / 1000.0f);
      snprintf(lines[3], sizeof(lines[3]), "pu %.2f m", g_stats.penUpMm   / 1000.0f);
      float pad   = 6.0f;
      float lineH = ImGui::GetTextLineHeight();
      float step  = lineH + 2.0f;

      float maxW = 0;
      for (auto &l : lines) maxW = std::max(maxW, ImGui::CalcTextSize(l).x);
      float boxH = 4 * step + pad;
      ImVec2 boxMax = {canvasPos.x + cW - pad, canvasPos.y + pad + boxH};
      ImVec2 boxMin = {boxMax.x - maxW - pad,  canvasPos.y + pad};
      dl->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 140), 4.0f);

      for (int li = 0; li < 4; ++li) {
        ImVec2 sz  = ImGui::CalcTextSize(lines[li]);
        ImVec2 pos = {canvasPos.x + cW - sz.x - pad * 1.5f,
                      canvasPos.y + pad * 1.5f + li * step};
        dl->AddText(pos, IM_COL32(255, 255, 255, 230), lines[li]);
      }
    }

    // Tooltip with plotter coords — invert the full xfPoint() transform:
    // screen → unrotated canvas → HPGL
    if (hovered) {
      float mx = io.MousePos.x - canvasPos.x;
      float my = io.MousePos.y - canvasPos.y;
      auto [rx, ry] = unrotateCanvas(mx, my, cW, cH, cosR, sinR);
      ImGui::SetTooltip("%.0f, %.0f",
                        (rx - g_panX) / g_scale,
                        (ry - g_panY) / g_scale);
    }

    ImGui::End();

    // Render
    ImGui::Render();
    glViewport(0, 0, g_fbW, g_fbH);
    glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
