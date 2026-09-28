#include "frog.h"
#include "status.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr float kCellW = 170, kCellH = 234, kHeaderH = 34, kFrogTop = 30;

// Poison-dart-frog colours, one per agent.
const ImU32 kSkins[] = {
    IM_COL32(94, 187, 99, 255),   IM_COL32(63, 167, 214, 255), IM_COL32(242, 150, 70, 255),
    IM_COL32(155, 123, 212, 255), IM_COL32(232, 197, 71, 255), IM_COL32(224, 100, 90, 255),
    IM_COL32(64, 190, 170, 255),  IM_COL32(236, 128, 170, 255),
};
constexpr int kSkinCount = sizeof(kSkins) / sizeof(kSkins[0]);

uint32_t fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

// Each name hashes to a colour; clashes walk to the next free one, so a
// frog's colour stays put as long as the set of agents does.
std::map<std::string, ImU32> assignSkins(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    std::map<std::string, ImU32> out;
    bool used[kSkinCount] = {};
    for (auto& n : names) {
        int i = fnv(n) % kSkinCount;
        for (int k = 0; k < kSkinCount && used[i]; ++k) i = (i + 1) % kSkinCount;
        used[i] = true;
        out[n] = kSkins[i];
    }
    return out;
}

std::string shortName(const std::string& n) {
    const std::string prefix = "mudline-";
    return n.rfind(prefix, 0) == 0 && n.size() > prefix.size() ? n.substr(prefix.size()) : n;
}

ImU32 stateColor(AgentState s) {
    switch (s) {
        case AgentState::Working: return IM_COL32(127, 214, 255, 255);
        case AgentState::Question: return IM_COL32(247, 200, 70, 255);
        default: return IM_COL32(160, 180, 184, 255);
    }
}

std::vector<Agent> demoAgents() {
    return {
        {"mudline-pm", "busy", AgentState::Working, ""},
        {"mudline-lead-dev", "idle", AgentState::Question, "Should the Export A story ship behind a flag?"},
        {"mudline-design", "idle", AgentState::Idle, "Mockups are up on the preview."},
        {"mudline-cos", "busy", AgentState::Working, ""},
        {"mudline-dev", "idle", AgentState::Idle, "MUD-168 is merged."},
        {"mudline-platform-dev", "shell", AgentState::Working, ""},
    };
}

void centeredText(ImDrawList* dl, ImFont* f, float size, ImVec2 c, ImU32 col, const std::string& s) {
    ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    dl->AddText(f, size, {c.x - sz.x / 2, c.y - sz.y / 2}, col, s.c_str());
}

bool writePpm(const char* path, int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* out = std::fopen(path, "wb");
    if (!out) return false;
    std::fprintf(out, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(&px[static_cast<size_t>(y) * w * 3], 1, w * 3, out);
    std::fclose(out);
    return true;
}

void usage() {
    std::puts(
        "agentworld — frogs for your Claude Code lead sessions\n"
        "  --root <dir>       workspace whose named sessions to show\n"
        "                     (default $AGENTWORLD_ROOT or ~/development/mudline-workspace)\n"
        "  --status           print each agent's state and exit\n"
        "  --demo             show made-up agents in every state\n"
        "  --snapshot <file>  render one frame to a PPM file and exit");
}

}  // namespace

int main(int argc, char** argv) {
    const char* envRoot = std::getenv("AGENTWORLD_ROOT");
    std::string root = envRoot ? envRoot : std::string(std::getenv("HOME")) + "/development/mudline-workspace";
    bool statusOnly = false, demo = false;
    const char* snapshot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!std::strcmp(argv[i], "--status")) statusOnly = true;
        else if (!std::strcmp(argv[i], "--demo")) demo = true;
        else if (!std::strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot = argv[++i];
        else { usage(); return std::strcmp(argv[i], "--help") ? 1 : 0; }
    }

    auto poll = [&] { return demo ? demoAgents() : readAgents(root); };

    if (statusOnly) {
        for (auto& a : poll())
            std::printf("%-22s %-15s %-6s %s\n", a.name.c_str(), stateName(a.state), a.status.c_str(),
                        a.detail.substr(0, 70).c_str());
        return 0;
    }

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(3 * static_cast<int>(kCellW) + 24,
                                       2 * static_cast<int>(kCellH) + static_cast<int>(kHeaderH) + 16,
                                       "agentworld", nullptr, nullptr);
    if (!win) return 1;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Fonts are rasterised at the display's pixel density and drawn at logical size.
    float dpi = 1;
    glfwGetWindowContentScale(win, &dpi, nullptr);
    const char* rounded = "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf";
    FrogFonts fonts{};
    if (std::filesystem::exists(rounded)) {
        fonts.label = io.Fonts->AddFontFromFileTTF(rounded, 15 * dpi);
        fonts.big = io.Fonts->AddFontFromFileTTF(rounded, 32 * dpi);
    } else {
        ImFontConfig cfg;
        cfg.SizePixels = 15 * dpi;
        fonts.label = io.Fonts->AddFontDefault(&cfg);
        cfg.SizePixels = 32 * dpi;
        fonts.big = io.Fonts->AddFontDefault(&cfg);
    }
    io.FontGlobalScale = 1 / dpi;

    std::vector<Agent> agents = poll();
    std::map<std::string, AgentState> lastState;
    for (auto& a : agents) lastState[a.name] = a.state;
    double lastPoll = glfwGetTime();
    bool onTop = false;
    int frame = 0;

    while (!glfwWindowShouldClose(win)) {
        // ~30 fps is plenty for the animations; back off while minimised.
        glfwWaitEventsTimeout(glfwGetWindowAttrib(win, GLFW_ICONIFIED) ? 0.5 : 1.0 / 30.0);
        double now = glfwGetTime();
        if (now - lastPoll > 1.5) {
            agents = poll();
            lastPoll = now;
            bool newQuestion = false;
            for (auto& a : agents) {
                auto it = lastState.find(a.name);
                if (a.state == AgentState::Question && (it == lastState.end() || it->second != a.state))
                    newQuestion = true;
                lastState[a.name] = a.state;
            }
            // Bounces the dock icon when a frog starts waiting on you.
            if (newQuestion && !glfwGetWindowAttrib(win, GLFW_FOCUSED)) glfwRequestWindowAttention(win);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImVec2 size = io.DisplaySize;
        const float t = static_cast<float>(snapshot ? 1.1 : now);
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilledMultiColor({0, 0}, size, IM_COL32(28, 60, 68, 255), IM_COL32(28, 60, 68, 255),
                                    IM_COL32(15, 36, 42, 255), IM_COL32(15, 36, 42, 255));
        // Slow ripples on the pond.
        for (int i = 0; i < 3; ++i) {
            float u = std::fmod(t / 6.f + i / 3.f, 1.f);
            ImVec2 c{size.x * (0.2f + 0.3f * i), size.y * (0.35f + 0.25f * (i % 2))};
            bg->AddEllipse(c, {20 + u * 90, (20 + u * 90) * 0.3f}, IM_COL32(170, 220, 230, (int)(40 * (1 - u))), 0,
                           0, 1.5f);
        }

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("pond", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        int counts[3] = {};
        for (auto& a : agents) counts[static_cast<int>(a.state)]++;
        dl->AddText(fonts.label, 15, {14, 10}, IM_COL32(220, 235, 230, 255), "mudline");
        char summary[96];
        std::snprintf(summary, sizeof summary, "%d working, %d question%s, %d idle", counts[0], counts[2],
                      counts[2] == 1 ? "" : "s", counts[1]);
        ImVec2 ss = fonts.label->CalcTextSizeA(13, FLT_MAX, 0, summary);
        dl->AddText(fonts.label, 13, {size.x - ss.x - 14, 11}, IM_COL32(160, 190, 190, 255), summary);

        if (agents.empty()) {
            centeredText(dl, fonts.label, 15, {size.x / 2, size.y / 2}, IM_COL32(160, 190, 190, 255),
                         "No lead sessions running under " + shortName(root));
        } else {
            std::vector<std::string> names;
            for (auto& a : agents) names.push_back(a.name);
            auto skins = assignSkins(names);

            int n = static_cast<int>(agents.size());
            int cols = std::min(n, 3), rows = (n + cols - 1) / cols;
            float availH = size.y - kHeaderH;
            float scale = std::clamp(std::min(size.x / (cols * kCellW), availH / (rows * kCellH)), 0.4f, 2.5f);
            float gridW = cols * kCellW * scale, gridH = rows * kCellH * scale;
            ImVec2 start{(size.x - gridW) / 2, kHeaderH + (availH - gridH) / 2};

            for (int i = 0; i < n; ++i) {
                const Agent& a = agents[i];
                ImVec2 cell{start.x + (i % cols) * kCellW * scale, start.y + (i / cols) * kCellH * scale};
                ImVec2 cellEnd{cell.x + kCellW * scale, cell.y + kCellH * scale};

                if (a.state == AgentState::Question) {
                    float pulse = 0.5f + 0.5f * std::sin(t * 4);
                    dl->AddRectFilled({cell.x + 4, cell.y + 4}, {cellEnd.x - 4, cellEnd.y - 4},
                                      IM_COL32(247, 200, 70, (int)(18 + 22 * pulse)), 14 * scale);
                    dl->AddRect({cell.x + 4, cell.y + 4}, {cellEnd.x - 4, cellEnd.y - 4},
                                IM_COL32(247, 200, 70, (int)(60 + 90 * pulse)), 14 * scale, 0, 1.5f);
                }

                float seed = (fnv(a.name) % 1000) / 100.f;
                drawFrog(dl, {cell.x + kCellW * scale / 2, cell.y + kFrogTop * scale}, scale, skins[a.name],
                         a.state, t, seed, fonts);

                float cx = cell.x + kCellW * scale / 2;
                centeredText(dl, fonts.label, 16 * scale, {cx, cell.y + 203 * scale}, IM_COL32(240, 245, 240, 255),
                             shortName(a.name));
                centeredText(dl, fonts.label, 12.5f * scale, {cx, cell.y + 221 * scale}, stateColor(a.state),
                             stateName(a.state));

                ImGui::SetCursorScreenPos(cell);
                ImGui::InvisibleButton(a.name.c_str(), {kCellW * scale, kCellH * scale});
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
                    ImGui::TextUnformatted(a.name.c_str());
                    ImGui::TextDisabled("%s (%s)", stateName(a.state), a.status.c_str());
                    if (!a.detail.empty()) {
                        ImGui::Separator();
                        ImGui::TextUnformatted(a.detail.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("menu");
        if (ImGui::BeginPopup("menu")) {
            if (ImGui::MenuItem("Always on top", nullptr, &onTop)) glfwSetWindowAttrib(win, GLFW_FLOATING, onTop);
            if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(win, GLFW_TRUE);
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Render();
        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.06f, 0.14f, 0.16f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (snapshot && ++frame == 3) {
            writePpm(snapshot, fbw, fbh);
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
