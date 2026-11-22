#include "crosshair.h"
#include "imgui.h"

namespace crosshair {
Settings s;
const char* const kStyleNames[] = { "Cross", "Dot", "Cross + dot", "Circle" };
const int kStyleCount = 4;

void draw() {
    if (!s.enabled) return;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    if (d.x < 2.f || d.y < 2.f) return;
    const float cx = (float)(int)(d.x * 0.5f) + 0.5f, cy = (float)(int)(d.y * 0.5f) + 0.5f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 col = IM_COL32((int)(s.color[0] * 255), (int)(s.color[1] * 255), (int)(s.color[2] * 255), (int)(s.color[3] * 255));
    const ImU32 out = IM_COL32(0, 0, 0, (int)(200 * s.color[3]));
    const float t = s.thickness, g = s.gap, L = s.size;

    auto line = [&](float x0, float y0, float x1, float y1) {
        if (s.outline) dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), out, t + 2.f);
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, t);
    };
    if (s.style == 0 || s.style == 2) {
        line(cx - g - L, cy, cx - g, cy);
        line(cx + g, cy, cx + g + L, cy);
        line(cx, cy - g - L, cx, cy - g);
        line(cx, cy + g, cx, cy + g + L);
    }
    if (s.style == 1 || s.style == 2) {
        float r = t + 0.5f;
        if (s.outline) dl->AddCircleFilled(ImVec2(cx, cy), r + 1.f, out, 16);
        dl->AddCircleFilled(ImVec2(cx, cy), r, col, 16);
    }
    if (s.style == 3) {
        if (s.outline) dl->AddCircle(ImVec2(cx, cy), L, out, 32, t + 2.f);
        dl->AddCircle(ImVec2(cx, cy), L, col, 32, t);
    }
}
}
