#include "esp.h"
#include "camera.h"
#include "entities.h"
#include "fov.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>

namespace esp {
Settings s;

std::map<uint32_t, StanceEntry>& stanceTable() { static std::map<uint32_t, StanceEntry> t; return t; }

// colour by relation to you: same faction = teammate, otherwise enemy (also when your own faction is unknown)
static ImU32 relColor(int faction, int localFaction) {
    const float* c = (localFaction != 0 && faction == localFaction) ? s.colTeam : s.colEnemy;
    return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(c[3] * 255));
}

void draw() {
    if (!s.enabled) return;
    camera::Cam cam = camera::read();
    if (!cam.valid) return;

    const entities::Snapshot& snap = entities::current();
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float fovDeg = fov::effective(s.fov);
    // Count living players per stance tag; first-seen tags get a default height.
    auto& table = stanceTable();
    for (auto& kv : table) kv.second.liveCount = 0;
    uint32_t common = 0; int best = 0;
    for (const auto& e : snap.list)
        if (e.state == 1 && e.stance) {
            auto it = table.find(e.stance);
            if (it == table.end()) it = table.emplace(e.stance, StanceEntry{ 0.f, 0 }).first;
            int c = ++it->second.liveCount;
            if (c > best) { best = c; common = e.stance; }
        }
    for (auto& kv : table)
        if (kv.second.height <= 0.f) kv.second.height = (kv.first == common) ? s.boxHeight : s.lowHeight;
    const bool useTeam = s.teamCheck && snap.localFaction != 0;

    for (const auto& e : snap.list) {
        if (e.isLocal) continue;
        if (e.state == 2 && !s.showDead) continue;
        if (useTeam && e.faction == snap.localFaction) continue;
        if (e.dist > s.maxDistance) continue;

        float h = s.boxHeight;
        if (e.state == 2) h = 0.5f;
        else if (e.stance) { auto it = table.find(e.stance); if (it != table.end()) h = it->second.height; }
        camera::V3 feet{ e.x, e.y, e.z }, head{ e.x, e.y, e.z + h };
        float fx, fy, hx, hy;
        if (!camera::worldToScreen(cam, feet, disp.x, disp.y, fovDeg, false, fx, fy)) continue;
        if (!camera::worldToScreen(cam, head, disp.x, disp.y, fovDeg, false, hx, hy)) continue;

        float bh = fy - hy;
        if (bh < 2.f) bh = 2.f;
        float bw = bh * 0.45f;
        float cx = (fx + hx) * 0.5f;
        float top = std::min(fy, hy), bot = top + bh;
        ImU32 col = relColor(e.faction, snap.localFaction);

        if (s.box) {
            dl->AddRect(ImVec2(cx - bw / 2 - 1, top - 1), ImVec2(cx + bw / 2 + 1, bot + 1), IM_COL32(0, 0, 0, 200), 0, 0, s.thickness + 1.f);
            dl->AddRect(ImVec2(cx - bw / 2, top), ImVec2(cx + bw / 2, bot), col, 0, 0, s.thickness);
        }
        if (s.health && e.maxHealth > 0) {
            float f = std::clamp(e.health / e.maxHealth, 0.f, 1.f);
            float x = cx - bw / 2 - 5;
            dl->AddRectFilled(ImVec2(x - 1, top - 1), ImVec2(x + 3, bot + 1), IM_COL32(0, 0, 0, 200));
            dl->AddRectFilled(ImVec2(x, bot - bh * f), ImVec2(x + 2, bot),
                              IM_COL32((int)(255 * (1 - f)), (int)(255 * f), 40, 255));
        }
        float ty = top - 14;
        if (s.name) {
            const char* n = e.name.empty() ? "<unnamed>" : e.name.c_str();
            ImVec2 ts = ImGui::CalcTextSize(n);
            dl->AddText(ImVec2(cx - ts.x / 2 + 1, ty + 1), IM_COL32(0, 0, 0, 255), n);
            dl->AddText(ImVec2(cx - ts.x / 2, ty), IM_COL32(255, 255, 255, 255), n);
        }
        if (s.distance) {
            char buf[32]; snprintf(buf, sizeof(buf), "%.0fm", e.dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(cx - ts.x / 2 + 1, bot + 3), IM_COL32(0, 0, 0, 255), buf);
            dl->AddText(ImVec2(cx - ts.x / 2, bot + 2), IM_COL32(255, 255, 255, 255), buf);
        }
        if (s.snaplines)
            dl->AddLine(ImVec2(disp.x / 2, disp.y), ImVec2(cx, bot), col, 1.f);
    }
}
}

namespace esp {
int botsOnScreenCount() {
    camera::Cam cam = camera::read();
    if (!cam.valid) return 0;
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float fovDeg = fov::effective(s.fov);
    int n = 0;
    for (const auto& e : entities::current().list) {
        if (e.isLocal || e.state != 1 || e.dist > 200.f) continue;
        float x, y;
        camera::V3 mid{ e.x, e.y, e.z + 0.9f };
        if (camera::worldToScreen(cam, mid, disp.x, disp.y, fovDeg, false, x, y) &&
            x > 0 && y > 0 && x < disp.x && y < disp.y) ++n;
    }
    return n;
}
bool anyBotOnScreen() { return botsOnScreenCount() > 0; }
}

namespace esp {
float boxHeightFor(uint32_t stance, unsigned char state) {
    if (state == 2) return 0.5f;
    if (stance) {
        auto& t = stanceTable();
        auto it = t.find(stance);
        if (it != t.end() && it->second.height > 0.f) return it->second.height;
    }
    return s.boxHeight;
}
}
