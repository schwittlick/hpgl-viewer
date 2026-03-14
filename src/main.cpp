#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <cmath>

#include "hpgl_parser.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

// ─── Config
// ──────────────────────────────────────────────────────────────────

namespace fs = std::filesystem;

static fs::path configPath() {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  fs::path base = xdg ? fs::path(xdg) : fs::path(getenv("HOME")) / ".config";
  return base / "hpgl-viewer" / "config";
}

static std::string configLoad(const std::string &key) {
  std::ifstream f(configPath());
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind(key + "=", 0) == 0)
      return line.substr(key.size() + 1);
  }
  return {};
}

static void configSave(const std::string &key, const std::string &value) {
  fs::path path = configPath();
  fs::create_directories(path.parent_path());

  // read existing lines, replace or append
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  bool found = false;
  while (std::getline(in, line)) {
    if (line.rfind(key + "=", 0) == 0) {
      lines.push_back(key + "=" + value);
      found = true;
    } else {
      lines.push_back(line);
    }
  }
  in.close();
  if (!found)
    lines.push_back(key + "=" + value);

  std::ofstream out(path);
  for (auto &l : lines)
    out << l << "\n";
}

// ─── File dialog
// ────────────────────────────────────────────────────────────

static std::string g_lastOpenDir;

static std::string openFileDialog() {
  // build the start-path argument (must end with / for zenity to treat as dir)
  std::string startDir = g_lastOpenDir;
  if (!startDir.empty() && startDir.back() != '/')
    startDir += '/';

  std::string zenity =
      "zenity --file-selection"
      " --file-filter='HPGL files (*.hpgl *.plt *.hgl) | *.hpgl *.plt *.hgl'"
      " --file-filter='All files | *'";
  if (!startDir.empty())
    zenity += " --filename='" + startDir + "'";
  zenity += " 2>/dev/null";

  std::string kdialog = "kdialog --getopenfilename '";
  kdialog += startDir.empty() ? "." : startDir;
  kdialog += "' '*.hpgl *.plt *.hgl' 2>/dev/null";

  for (const std::string &cmd : {kdialog}) {
    FILE *f = popen(cmd.c_str(), "r");
    if (!f)
      continue;
    std::array<char, 4096> buf{};
    fgets(buf.data(), buf.size(), f);
    int rc = pclose(f);
    if (rc != 0)
      continue;
    std::string path(buf.data());
    if (!path.empty() && path.back() == '\n')
      path.pop_back();
    if (!path.empty()) {
      g_lastOpenDir = fs::path(path).parent_path().string();
      configSave("last_open_dir", g_lastOpenDir);
      return path;
    }
  }
  return {};
}

// ─── App state
// ────────────────────────────────────────────────────────────────

static constexpr float kHpglUnitsPerMm = 40.0f;

struct PenStyle {
  ImVec4 color = {0.1f, 0.1f, 0.1f, 1.0f};
  float thickness = 0.3f; // mm
};

static HpglDoc g_doc;
static std::string g_filePath;
static char g_filePathBuf[1024] = "";
static bool g_fitRequested = false;

// pan/zoom/rotation state
static float g_panX = 0, g_panY = 0, g_scale = 1.0f;
static float g_rotation = 0.0f; // radians, multiples of π/2
static bool  g_showPenUp = false;
static float g_penUpThreshold = 30.0f; // cm

// Cached doc stats (recomputed on load)
static int   g_numPaths    = 0;
static float g_penDownDist = 0.0f; // mm
static float g_penUpDist   = 0.0f; // mm
static void computeDocStats() {
  g_numPaths    = (int)g_doc.strokes.size();
  g_penDownDist = 0.0f;
  g_penUpDist   = 0.0f;
  for (auto &s : g_doc.strokes) {
    for (size_t i = 0; i + 1 < s.points.size(); ++i) {
      float dx = s.points[i+1].x - s.points[i].x;
      float dy = s.points[i+1].y - s.points[i].y;
      g_penDownDist += sqrtf(dx*dx + dy*dy);
    }
  }
  for (size_t i = 0; i + 1 < g_doc.strokes.size(); ++i) {
    const auto &a = g_doc.strokes[i];
    const auto &b = g_doc.strokes[i+1];
    if (a.points.empty() || b.points.empty()) continue;
    float dx = b.points.front().x - a.points.back().x;
    float dy = b.points.front().y - a.points.back().y;
    g_penUpDist += sqrtf(dx*dx + dy*dy);
  }
  g_penDownDist /= kHpglUnitsPerMm;
  g_penUpDist   /= kHpglUnitsPerMm;
}

// fullscreen state
static bool g_isFullscreen = false;
static int g_windowedX = 100, g_windowedY = 100;
static int g_windowedW = 1400, g_windowedH = 900;

// pen styles (up to 8 pens)
static PenStyle g_pens[8];

static void initPenColors() {
  // nice defaults
  ImVec4 defaults[] = {
      {0.05f, 0.05f, 0.05f, 1}, // 1 black
      {0.8f, 0.1f, 0.1f, 1},    // 2 red
      {0.1f, 0.5f, 0.1f, 1},    // 3 green
      {0.1f, 0.1f, 0.8f, 1},    // 4 blue
      {0.7f, 0.5f, 0.0f, 1},    // 5 orange
      {0.5f, 0.0f, 0.5f, 1},    // 6 purple
      {0.0f, 0.5f, 0.5f, 1},    // 7 teal
      {0.4f, 0.4f, 0.4f, 1},    // 8 grey
  };
  for (int i = 0; i < 8; ++i)
    g_pens[i].color = defaults[i];
}

static void fitView(float canvasW, float canvasH) {
  if (g_doc.empty())
    return;
  float docW = g_doc.maxX - g_doc.minX;
  float docH = g_doc.maxY - g_doc.minY;
  if (docW < 1) docW = 1;
  if (docH < 1) docH = 1;

  // Effective screen-space bounding box of the (possibly rotated) document
  float absC = fabsf(cosf(g_rotation));
  float absS = fabsf(sinf(g_rotation));
  float effW = docW * absC + docH * absS;
  float effH = docW * absS + docH * absC;

  float pad = 0.05f;
  g_scale = std::min(canvasW / effW, canvasH / effH) * (1.0f - 2.0f * pad);

  // Center the document midpoint on the canvas
  g_panX = canvasW * 0.5f - (g_doc.minX + docW * 0.5f) * g_scale;
  g_panY = canvasH * 0.5f - (g_doc.minY + docH * 0.5f) * g_scale;
}

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

// ─── Drawing
// ──────────────────────────────────────────────────────────────────

// Transform one HPGL point to screen space, applying pan/zoom/rotation.
static ImVec2 xfPoint(float hx, float hy, ImVec2 origin, float cW, float cH,
                      float cosR, float sinR) {
  float sx = hx * g_scale + g_panX - cW * 0.5f;
  float sy = hy * g_scale + g_panY - cH * 0.5f;
  return {origin.x + sx * cosR - sy * sinR + cW * 0.5f,
          origin.y + sx * sinR + sy * cosR + cH * 0.5f};
}

static void drawHpgl(ImDrawList *dl, ImVec2 origin, float canvasW,
                     float canvasH) {
  dl->PushClipRect(origin, {origin.x + canvasW, origin.y + canvasH}, true);

  float cosR = cosf(g_rotation);
  float sinR = sinf(g_rotation);

  for (auto &stroke : g_doc.strokes) {
    if (stroke.points.empty())
      continue;
    int pi = std::max(0, std::min(stroke.pen - 1, 7));
    ImU32 col = ImGui::ColorConvertFloat4ToU32(g_pens[pi].color);
    float screen_thick =
        std::max(1.0f, g_pens[pi].thickness * kHpglUnitsPerMm * g_scale);

    if (stroke.points.size() == 2 && stroke.points[0] == stroke.points[1]) {
      ImVec2 p = xfPoint(stroke.points[0].x, stroke.points[0].y, origin,
                         canvasW, canvasH, cosR, sinR);
      dl->AddCircleFilled(p, screen_thick * 0.5f, col);
      continue;
    }

    for (size_t i = 0; i + 1 < stroke.points.size(); ++i) {
      ImVec2 p0 = xfPoint(stroke.points[i].x, stroke.points[i].y, origin,
                          canvasW, canvasH, cosR, sinR);
      ImVec2 p1 = xfPoint(stroke.points[i + 1].x, stroke.points[i + 1].y,
                          origin, canvasW, canvasH, cosR, sinR);
      dl->AddLine(p0, p1, col, screen_thick);
    }
  }

  if (g_showPenUp) {
    const ImU32 colNormal = IM_COL32(60, 220, 100, 160);
    const ImU32 colLong   = IM_COL32(220, 50, 50, 200);
    float t = g_penUpThreshold * 400.0f; // cm → HPGL units
    float thresholdSq = t * t;
    for (size_t i = 0; i + 1 < g_doc.strokes.size(); ++i) {
      const auto &a = g_doc.strokes[i];
      const auto &b = g_doc.strokes[i + 1];
      if (a.points.empty() || b.points.empty()) continue;
      float dx = b.points.front().x - a.points.back().x;
      float dy = b.points.front().y - a.points.back().y;
      ImU32 col = (dx*dx + dy*dy) > thresholdSq ? colLong : colNormal;
      ImVec2 p0 = xfPoint(a.points.back().x,  a.points.back().y,  origin, canvasW, canvasH, cosR, sinR);
      ImVec2 p1 = xfPoint(b.points.front().x, b.points.front().y, origin, canvasW, canvasH, cosR, sinR);
      dl->AddLine(p0, p1, col, 1.0f);
    }
  }

  dl->PopClipRect();
}

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

  initPenColors();
  g_lastOpenDir = configLoad("last_open_dir");

  if (argc > 1) {
      g_filePath = argv[1];
      strncpy(g_filePathBuf, g_filePath.c_str(), sizeof(g_filePathBuf) - 1);
      g_doc = HpglParser{}.parseFile(g_filePath);
      computeDocStats();
      g_fitRequested = true;
  }

  while (!glfwWindowShouldClose(window)) {
    glfwWaitEvents();
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
      if (ImGui::IsKeyPressed(ImGuiKey_O)) {
        std::string path = openFileDialog();
        if (!path.empty()) {
          g_filePath = path;
          strncpy(g_filePathBuf, path.c_str(), sizeof(g_filePathBuf) - 1);
          g_doc = HpglParser{}.parseFile(g_filePath);
          computeDocStats();
          g_fitRequested = true;
        }
      }
    }

    // Full-window dockspace — apply default layout on first run (no ini yet)
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
    if (ImGui::Button("Open")) {
      g_filePath = g_filePathBuf;
      g_doc = HpglParser{}.parseFile(g_filePath);
      computeDocStats();
      g_fitRequested = true;
    }
    if (!g_filePath.empty()) {
      ImGui::TextDisabled("%s", g_filePath.c_str());
      ImGui::Text("Strokes: %zu", g_doc.strokes.size());
      ImGui::Text("Bounds: (%.0f,%.0f)-(%.0f,%.0f)", g_doc.minX, g_doc.minY,
                  g_doc.maxX, g_doc.maxY);
    }

    ImGui::SeparatorText("View");
    ImGui::Checkbox("Show pen-up moves", &g_showPenUp);
    if (g_showPenUp) {
      ImGui::SetNextItemWidth(150);
      ImGui::SliderFloat("Long move threshold", &g_penUpThreshold, 0.0f, 200.0f, "%.0f cm");
    }
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
    if (g_fitRequested || sizeChanged)
      fitView(cW, cH);
    g_fitRequested = false;
    lastCanvasSize = {cW, cH};

    // Invisible interaction rect
    ImGui::InvisibleButton("##canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();

    // Pan (left drag or middle drag)
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
      g_panX += d.x;
      g_panY += d.y;
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
      g_panX += d.x;
      g_panY += d.y;
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

    // Zoom (scroll wheel, zoom toward cursor)
    if (hovered && io.MouseWheel != 0.0f) {
      float factor = (io.MouseWheel > 0) ? 1.1f : 0.9f;
      ImVec2 mp = io.MousePos;
      float mx = mp.x - canvasPos.x;
      float my = mp.y - canvasPos.y;
      g_panX = mx - (mx - g_panX) * factor;
      g_panY = my - (my - g_panY) * factor;
      g_scale *= factor;
    }

    drawHpgl(dl, canvasPos, cW, cH);

    // Stats overlay — top-right corner of canvas
    {
      char lines[4][48];
      snprintf(lines[0], sizeof(lines[0]), "%.0f FPS", io.Framerate);
      snprintf(lines[1], sizeof(lines[1]), "%d paths", g_numPaths);
      snprintf(lines[2], sizeof(lines[2]), "pd %.2f m", g_penDownDist / 1000.0f);
      snprintf(lines[3], sizeof(lines[3]), "pu %.2f m", g_penUpDist   / 1000.0f);
      float pad   = 6.0f;
      float lineH = ImGui::GetTextLineHeight();
      float step  = lineH + 2.0f;

      // Find widest line for background rect
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

    // Tooltip with plotter coords
    if (hovered) {
      ImVec2 mp = io.MousePos;
      float px = (mp.x - canvasPos.x - g_panX) / g_scale;
      float py = (mp.y - canvasPos.y - g_panY) / g_scale;
      ImGui::SetTooltip("%.0f, %.0f", px, py);
    }

    ImGui::End();

    // Render
    ImGui::Render();
    int fw, fh;
    glfwGetFramebufferSize(window, &fw, &fh);
    glViewport(0, 0, fw, fh);
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
