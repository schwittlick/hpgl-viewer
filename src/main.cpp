#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

// ─── HPGL parser ──────────────────────────────────────────────────────────────

struct Vec2 { float x, y; };

struct Stroke {
    std::vector<Vec2> points;
    int pen = 1; // SP pen number
};

struct HpglDoc {
    std::vector<Stroke> strokes;
    float minX =  1e30f, minY =  1e30f;
    float maxX = -1e30f, maxY = -1e30f;
    bool empty() const { return strokes.empty(); }
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

HpglDoc parseHpgl(const std::string& path) {
    HpglDoc doc;
    std::ifstream f(path);
    if (!f) return doc;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Tokenise by ';'
    int currentPen = 1;
    bool penDown    = false;
    float cx = 0, cy = 0;
    Stroke* cur = nullptr;

    auto ensureStroke = [&]() {
        if (!cur || cur->pen != currentPen) {
            doc.strokes.push_back({});
            cur = &doc.strokes.back();
            cur->pen = currentPen;
            if (penDown) cur->points.push_back({cx, cy});
        }
    };

    auto addPoint = [&](float x, float y) {
        doc.minX = std::min(doc.minX, x);
        doc.minY = std::min(doc.minY, y);
        doc.maxX = std::max(doc.maxX, x);
        doc.maxY = std::max(doc.maxY, y);
    };

    size_t pos = 0;
    while (pos < content.size()) {
        // skip whitespace / semicolons
        while (pos < content.size() && (content[pos] == ';' || content[pos] == ' ' || content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t'))
            ++pos;
        if (pos >= content.size()) break;

        // read command (2 uppercase chars)
        if (pos + 1 >= content.size()) break;
        char c0 = toupper(content[pos]);
        char c1 = toupper(content[pos+1]);
        pos += 2;

        // collect parameter string until ';' or next letter pair
        std::string params;
        while (pos < content.size() && content[pos] != ';') {
            params += content[pos++];
        }
        params = trim(params);

        // parse comma-separated floats
        auto getCoords = [&]() -> std::vector<float> {
            std::vector<float> v;
            std::stringstream ss(params);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                tok = trim(tok);
                if (!tok.empty()) {
                    try { v.push_back(std::stof(tok)); } catch (...) {}
                }
            }
            return v;
        };

        if (c0 == 'S' && c1 == 'P') {
            // Select Pen
            auto v = getCoords();
            if (!v.empty()) currentPen = (int)v[0];
            cur = nullptr;
        }
        else if (c0 == 'P' && c1 == 'U') {
            penDown = false;
            cur = nullptr;
            auto v = getCoords();
            if (v.size() >= 2) { cx = v[0]; cy = v[1]; }
        }
        else if (c0 == 'P' && c1 == 'D') {
            penDown = true;
            auto v = getCoords();
            if (v.size() >= 2) {
                ensureStroke();
                // first point = current position
                if (cur->points.empty()) cur->points.push_back({cx, cy});
                for (size_t i = 0; i + 1 < v.size(); i += 2) {
                    cx = v[i]; cy = v[i+1];
                    cur->points.push_back({cx, cy});
                    addPoint(cx, cy);
                }
            } else {
                ensureStroke();
                if (cur->points.empty()) cur->points.push_back({cx, cy});
            }
        }
        else if (c0 == 'P' && c1 == 'A') {
            auto v = getCoords();
            for (size_t i = 0; i + 1 < v.size(); i += 2) {
                cx = v[i]; cy = v[i+1];
                if (penDown) {
                    ensureStroke();
                    if (cur->points.empty()) cur->points.push_back({cx, cy});
                    else { cur->points.push_back({cx, cy}); addPoint(cx, cy); }
                }
            }
        }
    }

    // fallback bounds
    if (doc.minX > doc.maxX) { doc.minX = 0; doc.maxX = 1; doc.minY = 0; doc.maxY = 1; }

    return doc;
}

// ─── App state ────────────────────────────────────────────────────────────────

struct PenStyle {
    ImVec4 color = {0.1f, 0.1f, 0.1f, 1.0f};
    float  thickness = 1.5f;
};

static HpglDoc     g_doc;
static std::string g_filePath;
static char        g_filePathBuf[1024] = "";
static bool        g_fitNextFrame = false;

// pan/zoom state (in canvas coords)
static float g_panX = 0, g_panY = 0, g_scale = 1.0f;

// pen styles (up to 8 pens)
static PenStyle g_pens[8];

static void initPenColors() {
    // nice defaults
    ImVec4 defaults[] = {
        {0.05f,0.05f,0.05f,1}, // 1 black
        {0.8f, 0.1f,0.1f, 1}, // 2 red
        {0.1f, 0.5f,0.1f, 1}, // 3 green
        {0.1f, 0.1f,0.8f, 1}, // 4 blue
        {0.7f, 0.5f,0.0f, 1}, // 5 orange
        {0.5f, 0.0f,0.5f, 1}, // 6 purple
        {0.0f, 0.5f,0.5f, 1}, // 7 teal
        {0.4f, 0.4f,0.4f, 1}, // 8 grey
    };
    for (int i = 0; i < 8; ++i) g_pens[i].color = defaults[i];
}

static void fitView(float canvasW, float canvasH) {
    if (g_doc.empty()) return;
    float docW = g_doc.maxX - g_doc.minX;
    float docH = g_doc.maxY - g_doc.minY;
    if (docW < 1) docW = 1;
    if (docH < 1) docH = 1;
    float pad = 0.05f;
    g_scale = std::min(canvasW / docW, canvasH / docH) * (1.0f - 2*pad);
    g_panX = (canvasW - docW * g_scale) * 0.5f - g_doc.minX * g_scale;
    g_panY = (canvasH - docH * g_scale) * 0.5f - g_doc.minY * g_scale;
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

static void drawHpgl(ImDrawList* dl, ImVec2 origin, float canvasW, float canvasH) {
    // clip
    dl->PushClipRect(origin, {origin.x + canvasW, origin.y + canvasH}, true);

    for (auto& stroke : g_doc.strokes) {
        if (stroke.points.empty()) continue;
        int pi = std::max(0, std::min(stroke.pen - 1, 7));
        ImU32 col = ImGui::ColorConvertFloat4ToU32(g_pens[pi].color);
        float thick = g_pens[pi].thickness;
        std::cout << stroke.points.size() << std::endl;
        if (stroke.points.size() == 2) {
            float x = origin.x + stroke.points[0].x * g_scale + g_panX;
            float y = origin.y + stroke.points[0].y * g_scale + g_panY;
            dl->AddCircleFilled({x, y}, thick * 0.5f, col);
            continue;
        }

        for (size_t i = 0; i + 1 < stroke.points.size(); ++i) {
            float x0 = origin.x + stroke.points[i  ].x * g_scale + g_panX;
            float y0 = origin.y + stroke.points[i  ].y * g_scale + g_panY;
            float x1 = origin.x + stroke.points[i+1].x * g_scale + g_panX;
            float y1 = origin.y + stroke.points[i+1].y * g_scale + g_panY;
            dl->AddLine({x0, y0}, {x1, y1}, col, thick);
        }
    }
    dl->PopClipRect();
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "HPGL Viewer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    initPenColors();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        // ── Sidebar ──────────────────────────────────────────────────────────
        ImGui::SetNextWindowSize({320, 600}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls");

        ImGui::SeparatorText("File");
        ImGui::InputText("##path", g_filePathBuf, sizeof(g_filePathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Open")) {
            g_filePath = g_filePathBuf;
            g_doc = parseHpgl(g_filePath);
            g_fitNextFrame = true;
        }
        if (!g_filePath.empty()) {
            ImGui::TextDisabled("%s", g_filePath.c_str());
            ImGui::Text("Strokes: %zu", g_doc.strokes.size());
            ImGui::Text("Bounds: (%.0f,%.0f)-(%.0f,%.0f)",
                g_doc.minX, g_doc.minY, g_doc.maxX, g_doc.maxY);
        }

        ImGui::SeparatorText("View");
        ImGui::Text("Scale: %.3f", g_scale);
        ImGui::SameLine();
        if (ImGui::Button("Fit")) g_fitNextFrame = true;
        if (ImGui::Button("Reset zoom")) { g_scale = 1.0f; g_panX = 0; g_panY = 0; }

        ImGui::SeparatorText("Pen Styles");
        for (int i = 0; i < 8; ++i) {
            ImGui::PushID(i);
            char label[16];
            snprintf(label, sizeof(label), "Pen %d", i+1);
            ImGui::ColorEdit4(label, &g_pens[i].color.x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("##thick", &g_pens[i].thickness, 0.5f, 8.0f, "%.1f px");
            ImGui::PopID();
        }

        ImGui::End();

        // ── Canvas ───────────────────────────────────────────────────────────
        ImGui::SetNextWindowSize({1000, 700}, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        float  cW = canvasSize.x, cH = canvasSize.y;

        // Background
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(canvasPos, {canvasPos.x+cW, canvasPos.y+cH}, IM_COL32(245,245,240,255));

        if (g_fitNextFrame) {
            fitView(cW, cH);
            g_fitNextFrame = false;
        }

        // Invisible interaction rect
        ImGui::InvisibleButton("##canvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        bool hovered = ImGui::IsItemHovered();

        // Pan (left drag or middle drag)
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            g_panX += d.x; g_panY += d.y;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
            g_panX += d.x; g_panY += d.y;
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
