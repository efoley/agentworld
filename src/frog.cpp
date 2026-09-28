#include "frog.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

ImU32 shade(ImU32 c, float f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x = std::clamp(v.x * f, 0.f, 1.f);
    v.y = std::clamp(v.y * f, 0.f, 1.f);
    v.z = std::clamp(v.z * f, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        {x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t});
}

ImU32 withAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = std::clamp(a, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(v);
}

// Maps frog-local coordinates (x in -80..80, y in 0..160) to the screen.
struct Pen {
    ImDrawList* dl;
    ImVec2 o;
    float s;

    ImVec2 p(float x, float y) const { return {o.x + x * s, o.y + y * s}; }
    void ellipse(float x, float y, float rx, float ry, ImU32 c) const {
        dl->AddEllipseFilled(p(x, y), {rx * s, ry * s}, c);
    }
    void circle(float x, float y, float r, ImU32 c) const { dl->AddCircleFilled(p(x, y), r * s, c); }
    void ring(float x, float y, float r, ImU32 c, float th) const {
        dl->AddCircle(p(x, y), r * s, c, 0, th * s);
    }
    void line(float x0, float y0, float x1, float y1, ImU32 c, float th) const {
        dl->AddLine(p(x0, y0), p(x1, y1), c, th * s);
    }
    void curve(float x0, float y0, float cx, float cy, float x1, float y1, ImU32 c, float th) const {
        dl->AddBezierQuadratic(p(x0, y0), p(cx, cy), p(x1, y1), c, th * s);
    }
    void arc(float x, float y, float r, float a0, float a1, ImU32 c, float th) const {
        dl->PathArcTo(p(x, y), r * s, a0, a1);
        dl->PathStroke(c, 0, th * s);
    }
    void text(ImFont* f, float size, float x, float y, ImU32 c, const char* str) const {
        ImVec2 sz = f->CalcTextSizeA(size * s, FLT_MAX, 0, str);
        dl->AddText(f, size * s, {o.x + x * s - sz.x / 2, o.y + y * s - sz.y / 2}, c, str);
    }
};

const ImU32 kInk = IM_COL32(28, 36, 30, 255);
const ImU32 kWhite = IM_COL32(250, 252, 245, 255);
const ImU32 kCheek = IM_COL32(255, 120, 140, 90);

void drawEye(const Pen& f, float x, float y, ImU32 skin, bool closed, ImVec2 look) {
    f.circle(x, y, 15, skin);
    if (closed) {
        f.arc(x, y + 1, 8, 0.15f * kPi, 0.85f * kPi, kInk, 2.6f);
        return;
    }
    f.circle(x, y, 11, kWhite);
    f.circle(x + look.x, y + look.y, 5.5f, kInk);
    f.circle(x + look.x - 1.8f, y + look.y - 2.0f, 1.7f, kWhite);
}

void drawLaptop(const Pen& f, ImU32 skin, float t, float seed, const FrogFonts& fonts) {
    const ImU32 lid = IM_COL32(184, 194, 204, 255);
    f.dl->AddRectFilled(f.p(-38, 106), f.p(38, 150), shade(lid, 0.7f), 7 * f.s);
    f.dl->AddRectFilled(f.p(-37, 106), f.p(37, 148), lid, 6 * f.s);
    // A fly for a logo.
    f.ellipse(-3.6f, 124.5f, 3.8f, 2.3f, IM_COL32(255, 255, 255, 150));
    f.ellipse(3.6f, 124.5f, 3.8f, 2.3f, IM_COL32(255, 255, 255, 150));
    f.circle(0, 127.5f, 3.2f, shade(lid, 0.45f));

    // Hands tap the keys in turn.
    float phase = std::sin(t * 15 + seed);
    float tapL = std::max(0.f, phase) * 3.5f, tapR = std::max(0.f, -phase) * 3.5f;
    for (auto [x, tap] : {std::pair{-24.f, tapL}, std::pair{24.f, tapR}}) {
        f.ellipse(x, 105 + tap, 10, 7, shade(skin, 0.72f));
        f.ellipse(x, 104 + tap, 9, 6, skin);
    }

    // Bits of code drift up from the keyboard.
    static const char* glyphs[] = {"{", "}", ";", "</>", "()", "=>"};
    for (int i = 0; i < 2; ++i) {
        float cycle = t / 1.7f + i * 0.5f + seed;
        float u = cycle - std::floor(cycle);
        int g = static_cast<int>(std::floor(cycle) * 7 + i * 3) % 6;
        float side = i == 0 ? -1.f : 1.f;
        float x = side * (46 + u * 14), y = 104 - u * 46;
        f.text(fonts.label, 13, x, y, IM_COL32(150, 225, 255, static_cast<int>(220 * std::sin(u * kPi))),
               glyphs[g]);
    }
}

void drawZzz(const Pen& f, float t, const FrogFonts& fonts) {
    for (int i = 0; i < 3; ++i) {
        float cycle = t / 3.0f + i / 3.0f;
        float u = cycle - std::floor(cycle);
        float x = 32 + u * 28 + std::sin(u * 6) * 3, y = 50 - u * 42;
        f.text(fonts.big, 11 + u * 13, x, y, IM_COL32(215, 232, 255, static_cast<int>(230 * std::sin(u * kPi))),
               "z");
    }
}

void drawRaisedHand(const Pen& f, ImU32 skin, float t) {
    float a = -1.1f + std::sin(t * 6) * 0.22f;
    float sx = 40, sy = 116, len = 36;
    float hx = sx + std::cos(a) * len, hy = sy + std::sin(a) * len;
    f.line(sx, sy, hx, hy, shade(skin, 0.72f), 11);
    f.line(sx, sy, hx, hy, skin, 8);
    // Three round toe pads fanned around the palm.
    for (int k = -1; k <= 1; ++k) {
        float ta = a + k * 0.55f;
        f.circle(hx + std::cos(ta) * 8, hy + std::sin(ta) * 8, 3.2f, shade(skin, 0.85f));
    }
    f.circle(hx, hy, 7, skin);
}

void drawBubble(const Pen& f, float t, const FrogFonts& fonts) {
    float bob = std::sin(t * 3.2f) * 2;
    const ImU32 fill = IM_COL32(247, 200, 70, 255), edge = IM_COL32(170, 120, 20, 255);
    float x0 = 34, y0 = -2 + bob, x1 = 72, y1 = 32 + bob;
    f.dl->AddTriangleFilled(f.p(40, y1 - 2), f.p(52, y1 - 2), f.p(34, y1 + 12), edge);
    f.dl->AddRectFilled(f.p(x0 - 1.5f, y0 - 1.5f), f.p(x1 + 1.5f, y1 + 1.5f), edge, 11 * f.s);
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), fill, 10 * f.s);
    f.dl->AddTriangleFilled(f.p(41.5f, y1 - 1), f.p(50, y1 - 1), f.p(36, y1 + 9), fill);
    f.text(fonts.big, 27, (x0 + x1) / 2, (y0 + y1) / 2 + 1, IM_COL32(90, 55, 5, 255), "?");
}

}  // namespace

void drawFrog(ImDrawList* dl, ImVec2 origin, float scale, ImU32 skin, AgentState state, float t,
              float seed, const FrogFonts& fonts) {
    const bool working = state == AgentState::Working;
    const bool idle = state == AgentState::Idle;
    const bool asking = state == AgentState::Question;

    float hop = 0, breathe = 1;
    if (working) hop = std::sin(t * 7 + seed) * 1.0f;
    if (asking) hop = -std::fabs(std::sin(t * 3.2f)) * 7;
    breathe = idle ? 1 + 0.035f * std::sin(t * 1.6f + seed) : 1 + 0.015f * std::sin(t * 2.5f + seed);

    // The lily pad stays put while the frog hops.
    Pen pad{dl, origin, scale};
    pad.ellipse(0, 150, 68, 17, IM_COL32(38, 96, 52, 255));
    pad.ellipse(0, 148, 63, 14, IM_COL32(58, 130, 70, 255));
    pad.ellipse(0, 146, 44 + hop, 7, IM_COL32(0, 0, 0, 55));

    Pen f{dl, {origin.x, origin.y + hop * scale}, scale};
    const ImU32 dark = shade(skin, 0.72f);
    const ImU32 belly = mix(skin, IM_COL32(255, 250, 215, 255), 0.55f);

    // Legs and body.
    f.ellipse(-40, 138, 22, 11, dark);
    f.ellipse(40, 138, 22, 11, dark);
    f.ellipse(0, 118, 44, 33 * breathe, skin);
    f.ellipse(0, 126, 29, 21 * breathe, belly);
    if (!working) {
        f.ellipse(-22, 150, 11, 5.5f, dark);
        if (!asking) f.ellipse(22, 150, 11, 5.5f, dark);
    }

    // Head.
    f.ellipse(0, 84, 46, 29, skin);
    float blinkCycle = std::fmod(t + seed * 3.7f, 4.3f);
    bool closed = idle || blinkCycle < 0.13f;
    ImVec2 look = working ? ImVec2{0, 3.5f} : ImVec2{2.5f, -3.5f};
    drawEye(f, -24, 60, skin, closed, look);
    drawEye(f, 24, 60, skin, closed, look);
    f.circle(-6, 77, 1.6f, dark);
    f.circle(6, 77, 1.6f, dark);
    f.circle(-31, 92, 5, kCheek);
    f.circle(31, 92, 5, kCheek);

    if (working) {
        f.curve(-11, 95, 0, 98, 11, 95, kInk, 2.4f);
        f.ellipse(0, 101, 34, 9, IM_COL32(150, 215, 255, 40));  // screen glow
        drawLaptop(f, skin, t, seed, fonts);
    } else if (idle) {
        f.curve(-15, 93, 0, 101, 15, 93, kInk, 2.4f);
        drawZzz(f, t, fonts);
    } else {
        f.circle(0, 96, 4.2f, IM_COL32(120, 40, 50, 255));
        f.ring(0, 96, 4.2f, kInk, 2);
        drawRaisedHand(f, skin, t);
        drawBubble(f, t, fonts);
    }
}
