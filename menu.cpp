#include "menu.h"
#include "entities.h"
#include "camera.h"
#include "esp.h"
#include "fov.h"
#include "chams.h"
#include "weapon.h"
#include "tracers.h"
#include "aim.h"
#include "magic.h"
#include "volleyfx.h"
#include "camhook.h"
#include "visible.h"
#include "crosshair.h"
#include "config.h"
#include "buildflags.h"
#include "godmode.h"
#include "weaponscan.h"
#include "reloadspeed.h"

#include <windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/animation.hpp"
#include "ui/layout.hpp"
#include "ui/menu.hpp"
#include "ui/primitives.hpp"
#include "ui/scale.hpp"
#include "ui/slider.hpp"
#include "ui/theme.hpp"

using namespace yankeehook::gui;

namespace menu {
bool  wantUnload = false;
int   toggleKey = VK_DELETE;
bool  binding = false;
float uiScale = 1.0f;
int   cfgGeneration = 0;

const char* keyName(int vk) {
    static char buf[64];
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
        (vk >= VK_LEFT && vk <= VK_DOWN)) lp |= (1 << 24);   // extended key
    if (GetKeyNameTextA(lp, buf, sizeof(buf)) <= 0) snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

void setOpen(bool v) { yankeehook::gui::menu->m_draw = v; }
bool isOpen() { return yankeehook::gui::menu->m_draw; }
void toggle() { yankeehook::gui::menu->m_draw = !yankeehook::gui::menu->m_draw; }

namespace {
constexpr float kLeft  = layout::k_content_x;
constexpr float kRight = layout::k_content_x + layout::k_column_w + layout::k_gap;
constexpr float kTop   = layout::k_content_y;
constexpr float kFull  = layout::k_column_w * 2.0f + layout::k_gap;
ImVec2 column(float h) { return ImVec2(layout::k_column_w, h); }
// group height for n rows of widgets (28 px rows plus the title strip)
float rows(float n) { return 40.f + n * 29.f; }

// A colour swatch bound to a float[4] setting. The swatch keeps its own copy and re-reads the setting after a config load.
struct ColSync { ImVec4 c{}; int gen = -1; };
void colorRow(const char* label, const char* id, float* rgba, ColSync& cs) {
    if (cs.gen != cfgGeneration) { cs.c = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]); cs.gen = cfgGeneration; }
    ui::swatch_row(label, id, cs.c);
    rgba[0] = cs.c.x; rgba[1] = cs.c.y; rgba[2] = cs.c.z; rgba[3] = cs.c.w;
}
void sliderInt(const char* label, int* v, int lo, int hi, const char* fmt) {
    float f = (float)*v;
    widgets::slider_float_row(label, &f, (float)lo, (float)hi, fmt);
    *v = (int)(f + (f >= 0 ? 0.5f : -0.5f));
}

// ---------------- Visuals ----------------
void pageVisuals(int nav) {
    ImGui::PushID("visuals");
    auto& s = esp::s;
    static ColSync cEnemy, cTeam;
    if (nav == 0) {                                   // ---- ESP
        ui::group_begin("##esp", "ESP", ImVec2(kLeft, kTop), column(rows(6)));
        ui::checkbox("Enable ESP", s.enabled);
        ui::checkbox("Boxes", s.box);
        ui::checkbox("Names", s.name);
        ui::checkbox("Health bar", s.health);
        ui::checkbox("Distance", s.distance);
        ui::checkbox("Snaplines", s.snaplines);
        ui::group_end();

        ui::group_begin("##filters", "Filters", ImVec2(kRight, kTop), column(rows(5)));
        ui::checkbox("Team check (enemies only)", s.teamCheck);
        ui::checkbox("Show dead", s.showDead);
        colorRow("Enemy box colour", "##cenemy", s.colEnemy, cEnemy);
        colorRow("Team box colour", "##cteam", s.colTeam, cTeam);
        widgets::slider_float_row("Max distance", &s.maxDistance, 20.f, 2000.f, "%.0f m");
        ui::group_end();

        ui::group_begin("##proj", "Box size", ImVec2(kRight, kTop + rows(5) + 10.f), column(rows(4)));
        widgets::slider_float_row("ESP base FOV", &s.fov, 30.f, 120.f, "%.1f");
        widgets::slider_float_row("Height (standing)", &s.boxHeight, 1.0f, 2.5f, "%.2f m");
        widgets::slider_float_row("Height (crouch/prone)", &s.lowHeight, 0.3f, 1.8f, "%.2f m");
        widgets::slider_float_row("Line thickness", &s.thickness, 1.f, 4.f, "%.1f");
        ui::group_end();
    } else if (nav == 1) {                            // ---- Chams
        auto& c = chams::s;
        static ColSync cvis, chid;
        ui::group_begin("##chams", "Chams", ImVec2(kLeft, kTop), column(rows(5)));
        static const char* const modes[] = { "Off", "Visible only", "Through walls" };
        ui::combo_row("Mode", "##cmode", c.mode, modes, 3, 130.f);
        colorRow("Visible colour", "##cvis", c.visCol, cvis);
        colorRow("Hidden colour", "##chid", c.hidCol, chid);
        ui::checkbox("Auto-detect characters", c.autoDetect);
        if (ui::button("Reset learning", ImVec2(0.f, 24.f))) chams::resetLearning();
        ui::group_end();

        ui::group_begin("##cinfo", "Status", ImVec2(kRight, kTop), column(rows(5)));
        {
            auto st = chams::stats();
            char b[128];
            snprintf(b, sizeof(b), "Hooked: immediate %s  deferred %s", st.immediateHooked ? "yes" : "NO", st.deferredHooked ? "yes" : "no"); ui::text_label(b);
            snprintf(b, sizeof(b), "Draw calls/frame %d  colour pass %d", st.callsPerFrame, st.mainPassPerFrame); ui::text_label(b);
            snprintf(b, sizeof(b), "Mesh classes %d  recoloured/frame %d", st.classes, st.appliedPerFrame); ui::text_label(b);
            snprintf(b, sizeof(b), "Bots on screen %d", st.botsOnScreen); ui::text_label(b);
            ui::text_label("Walk so the bot count changes: classes that");
            ui::text_label("follow it are marked [CHAR].");
        }
        ui::group_end();

        ui::group_begin("##cclasses", "Draw classes (top 10)", ImVec2(kLeft, kTop + rows(5) + 10.f), ImVec2(kFull, 235.f));
        auto list = chams::snapshot(10);
        if (list.empty()) ui::text_label("Nothing seen yet (set Mode to Visible or Through walls).");
        static bool manualState[10];
        int n = 0;
        for (auto& v : list) {
            char label[128];
            snprintf(label, sizeof(label), "%s%llX  r %.2f  +%.2f/bot  avg %.1f  stride %u%s",
                     v.detected ? "[CHAR] " : "", (unsigned long long)v.id, v.corr, v.slope, v.avgDraws, v.stride, v.preview ? "  <preview>" : "");
            manualState[n] = v.manual;
            ImGui::PushID((int)(v.id & 0x7fffffff));
            if (ui::checkbox(label, manualState[n], 70.f)) chams::setManual(v.id, manualState[n]);
            ui::right_align(60.f);
            if (ui::button(v.preview ? "Stop" : "Preview", ImVec2(60.f, 18.f))) chams::setPreview(v.preview ? 0 : v.id);
            ImGui::PopID();
            ++n;
        }
        ui::group_end();
    } else if (nav == 2) {                            // ---- Tracers
        auto& t = tracers::s;
        static ColSync cSelf, cTeam2, cEnemy2;
        ui::group_begin("##trc", "Bullet tracers", ImVec2(kLeft, kTop), column(rows(7)));
        ui::checkbox("Enable tracers", t.enabled);
        ui::checkbox("Your bullets", t.showSelf);
        ui::checkbox("Teammate bullets", t.showTeam);
        ui::checkbox("Enemy bullets", t.showEnemy);
        ui::checkbox("Glow", t.glow);
        widgets::slider_float_row("Lifetime", &t.lifetime, 0.2f, 5.f, "%.1f s");
        widgets::slider_float_row("Thickness", &t.thickness, 1.f, 6.f, "%.1f");
        ui::group_end();

        ui::group_begin("##trcc", "Colours", ImVec2(kRight, kTop), column(rows(3)));
        colorRow("You", "##tself", t.colSelf, cSelf);
        colorRow("Teammates", "##tteam", t.colTeam, cTeam2);
        colorRow("Enemies", "##tenemy", t.colEnemy, cEnemy2);
        ui::group_end();

        ui::group_begin("##trcs", "Status", ImVec2(kRight, kTop + rows(3) + 10.f), column(rows(3)));
        {
            auto st = tracers::stats();
            char b[160];
            snprintf(b, sizeof(b), "Entities %d  bullets now %d", st.entitiesScanned, st.bulletsNow); ui::text_label(b);
            snprintf(b, sizeof(b), "Trails %d  moving %d  classes %d", st.tracks, st.moving, st.bulletClasses); ui::text_label(b);
            snprintf(b, sizeof(b), "Bullet class: %s", st.lastBulletClass.empty() ? "none yet" : st.lastBulletClass.c_str()); ui::text_label(b);
        }
        ui::group_end();
    } else {                                          // ---- View
        auto& c = crosshair::s;
        static ColSync cCross;
        ui::group_begin("##fovg", "Field of view", ImVec2(kLeft, kTop), column(rows(3)));
        ui::checkbox("FOV changer", fov::enabled);
        widgets::slider_float_row("FOV", &fov::value, 40.f, 140.f, "%.0f");
        ui::checkbox("Pause while aiming (keep ADS zoom)", fov::pauseWhileAiming);
        ui::group_end();

        ui::group_begin("##xh", "Crosshair", ImVec2(kLeft, kTop + rows(3) + 10.f), column(rows(7)));
        ui::checkbox("Enable crosshair", c.enabled);
        ui::combo_row("Style", "##xhstyle", c.style, crosshair::kStyleNames, crosshair::kStyleCount, 130.f);
        widgets::slider_float_row("Size", &c.size, 2.f, 40.f, "%.0f");
        widgets::slider_float_row("Gap", &c.gap, 0.f, 20.f, "%.0f");
        widgets::slider_float_row("Thickness", &c.thickness, 1.f, 5.f, "%.1f");
        ui::checkbox("Outline", c.outline);
        colorRow("Colour", "##xhcol", c.color, cCross);
        ui::group_end();

        ui::group_begin("##tp", "Third person", ImVec2(kRight, kTop), column(rows(4)));
        ui::checkbox("Third person", camhook::thirdPerson);
        ui::combo_row("Hotkey", "##tpkey", camhook::tpKey, camhook::kKeyNames, camhook::kKeyCount, 110.f);
        widgets::slider_float_row("Distance (m)", &camhook::tpDistance, 0.5f, 10.f, "%.1f");
        widgets::slider_float_row("Height (m)", &camhook::tpHeight, -1.f, 2.f, "%.2f");
        ui::group_end();

        ui::group_begin("##fc", "Freecam (WASD, Space up, Ctrl down, Shift fast)", ImVec2(kRight, kTop + rows(4) + 10.f), column(rows(3)));
        ui::checkbox("Freecam", camhook::freecam);
        ui::combo_row("Hotkey", "##fckey", camhook::fcKey, camhook::kKeyNames, camhook::kKeyCount, 110.f);
        widgets::slider_float_row("Speed", &camhook::fcSpeed, 1.f, 60.f, "%.0f m/s");
        ui::group_end();
    }
    ImGui::PopID();
}

// ---------------- Entities ----------------
void pageEntities(int) {
    static bool fLocal = true, fOthers = true, fDead = true, fUSA = true, fCSA = true, fNone = true;
    static int  botMode = 0;
    static bool sortByDist = true;
    static char nameFilter[64] = "";

    ImGui::PushID("entities");
    const entities::Snapshot& snap = entities::current();

    ui::group_begin("##ents", "Entities", ImVec2(kLeft, kTop), ImVec2(kFull, 455.f));

    ui::checkbox("Local", fLocal);  ImGui::SameLine(0, ui::S(8)); ui::checkbox("Others", fOthers); ImGui::SameLine(0, ui::S(8));
    ui::checkbox("Dead", fDead);    ImGui::SameLine(0, ui::S(8)); ui::checkbox("USA", fUSA);       ImGui::SameLine(0, ui::S(8));
    ui::checkbox("CSA", fCSA);      ImGui::SameLine(0, ui::S(8)); ui::checkbox("No team", fNone);  ImGui::SameLine(0, ui::S(8));
    ui::checkbox("Sort by distance", sortByDist);

    static const char* const botModes[] = { "All players", "Dummy-flagged only", "Non-dummy only" };
    ui::combo_row("Bots", "##botmode", botMode, botModes, 3, 160.f);
    ImGui::SetNextItemWidth(ui::S(200.f));
    ImGui::InputTextWithHint("##nf", "name filter", nameFilter, sizeof(nameFilter));
    ImGui::SameLine();
    if (ui::button("Rescan", ImVec2(70.f, 22.f))) entities::update(true);

    if (!snap.mapReadable) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Player map unreadable (count=%u).", snap.mapCount);
        ui::group_end();
        ImGui::PopID();
        return;
    }

    std::vector<const entities::Info*> rows;
    rows.reserve(snap.list.size());
    for (auto& e : snap.list) {
        if (e.isLocal && !fLocal) continue;
        if (!e.isLocal && !fOthers) continue;
        if (e.state == 2 && !fDead) continue;
        if (e.faction == 1 && !fUSA) continue;
        if (e.faction == 2 && !fCSA) continue;
        if (e.faction != 1 && e.faction != 2 && !fNone) continue;
        if (botMode == 1 && !e.isDummyFlag) continue;
        if (botMode == 2 && e.isDummyFlag) continue;
        if (nameFilter[0]) {
            std::string a = e.name, b = nameFilter;
            for (auto& c : a) c = (char)tolower((unsigned char)c);
            for (auto& c : b) c = (char)tolower((unsigned char)c);
            if (a.find(b) == std::string::npos) continue;
        }
        rows.push_back(&e);
    }
    if (sortByDist) std::sort(rows.begin(), rows.end(), [](auto* a, auto* b) { return a->dist < b->dist; });

    ImGui::Text("Showing %zu / %zu", rows.size(), snap.list.size());
    ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("tbl", 9, tf, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Team");
        ImGui::TableSetupColumn("HP");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Stance");
        ImGui::TableSetupColumn("Dist");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Id / ptr");
        ImGui::TableHeadersRow();
        for (auto* e : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->name.empty() ? "<unnamed>" : e->name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->isLocal ? "LOCAL" : e->isDummyFlag ? "Bot?" : "Other");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->faction == 1 ? "USA" : e->faction == 2 ? "CSA" : "?");
            ImGui::TableNextColumn(); ImGui::Text("%.0f/%.0f", e->health, e->maxHealth);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e->state == 1 ? "alive" : e->state == 2 ? "dead" : "?");
            ImGui::TableNextColumn(); ImGui::Text("%08X", e->stance);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", e->dist);
            ImGui::TableNextColumn(); ImGui::Text("%.0f %.0f %.0f", e->x, e->y, e->z);
            ImGui::TableNextColumn(); ImGui::Text("%X  %llX", e->id, (unsigned long long)e->player);
        }
        ImGui::EndTable();
    }
    ui::group_end();
    ImGui::PopID();
}


// ---------------- Player ----------------
void pagePlayer(int) {
    ImGui::PushID("player");
    camera::LocalPos lp = camera::readLocal();
    char buf[96];
    ui::group_begin("##gm", "Player", ImVec2(kLeft, kTop), column(rows(kRespawnTimer ? 3 : 1)));
    ui::checkbox("God mode", godmode::enabled);
    if (kRespawnTimer) ui::checkbox("No respawn timer", weapon::noRespawnTimer);
    ui::group_end();

    ui::group_begin("##lp", "Position", ImVec2(kRight, kTop), column(rows(3)));
    if (lp.valid) {
        snprintf(buf, sizeof(buf), "X  %.2f", lp.pos.x); ui::text_label(buf);
        snprintf(buf, sizeof(buf), "Y  %.2f", lp.pos.y); ui::text_label(buf);
        snprintf(buf, sizeof(buf), "Z  %.2f", lp.pos.z); ui::text_label(buf);
    } else ui::text_label("n/a (no local player)");
    ui::group_end();
    ImGui::PopID();
}

// ---------------- Weapon ----------------
void pageWeapon(int nav) {
    ImGui::PushID("weapon");
    if (nav == 0) {                                   // ---- Gun mods
        ui::group_begin("##wpn", "Gun mods", ImVec2(kLeft, kTop), column(rows(5)));
        ui::checkbox("Enable gun mods", weaponscan::gunMods);
        ui::text_label("Turns on, together:");
        ui::text_label("No sway / shake, no bullet drop, fast ADS,");
        ui::text_label("instant reload, infinite ammo and clip,");
        ui::text_label("no suppression, no head bob, value sliders.");
        ui::group_end();

        ui::group_begin("##wpnv", "Mod values", ImVec2(kRight, kTop), column(rows(6)));
        widgets::slider_float_row("Damage x", &weaponscan::damageMul, 0.1f, 5.f, "%.2f");
        widgets::slider_float_row("Bullet speed x", &weaponscan::speedMul, 0.5f, 5.f, "%.2f");
        widgets::slider_float_row("Reload time x", &weaponscan::reloadMul, 0.1f, 1.f, "%.2f");
        widgets::slider_float_row("Suppression x", &weaponscan::suppressionMul, 0.f, 10.f, "%.2f");
        widgets::slider_float_row("Reload animation x", &reloadspeed::speed, 2.f, 60.f, "%.0f");
        widgets::slider_float_row("Reload cancel at", &weapon::reloadCancelValue, 0.f, 0.75f, "%.2f");
        ui::group_end();
    } else {                                          // ---- Volley
        ui::group_begin("##volley", "Volley", ImVec2(kLeft, kTop), column(rows(9)));
        sliderInt("Bullets", &reloadspeed::volleyCount, 2, 60, "%.0f");
        sliderInt("Per tick", &reloadspeed::volleyPerTick, 1, 60, "%.0f");
        sliderInt("Delay (ms)", &reloadspeed::volleyDelayMs, 0, 1000, "%.0f");
        sliderInt("Random extra (ms)", &reloadspeed::volleyJitterMs, 0, 500, "%.0f");
        ui::checkbox("Ripple order (off = random)", reloadspeed::volleyRipple);
        ui::checkbox("Lock aim at start", reloadspeed::volleyLockAim);
        ui::checkbox("Spread along a line", magic::lineOn);
        widgets::slider_float_row("Spacing (m)", &magic::lineSpacing, 0.1f, 3.f, "%.2f");
        magic::lineCount = reloadspeed::volleyCount < 2 ? 2 : reloadspeed::volleyCount;
        if (ui::button("Fire volley", ImVec2(0.f, 24.f)) && reloadspeed::volleyLeft <= 0) reloadspeed::volleyStart(reloadspeed::volleyCount);
        ui::group_end();

        ui::group_begin("##trig", "Triggers and effects", ImVec2(kRight, kTop), column(rows(7)));
        ui::checkbox("Volley hotkey", reloadspeed::volleyHot);
        ui::combo_row("Volley key", "##volleykey", reloadspeed::volleyKey, aim::kKeyNames, aim::kKeyCount, 130.f);
        ui::checkbox("Hold-to-fire gun (hipfire)", reloadspeed::fireHot);
        ui::combo_row("Fire key", "##firekey", reloadspeed::fireKey, aim::kKeyNames, aim::kKeyCount, 130.f);
        sliderInt("Fire interval (ms)", &reloadspeed::fireMs, 40, 1500, "%.0f");
        ui::checkbox("Volley bullets deal damage", magic::volleyDamage);
        ui::checkbox("Muzzle smoke at each position", volleyfx::enabled);
        ui::group_end();
    }
    ImGui::PopID();
}

// ---------------- Aim ----------------
void pageAim(int) {
    ImGui::PushID("aim");
    auto& a = aim::s;
    ui::group_begin("##aim", "Aim assist (bots)", ImVec2(kLeft, kTop), column(rows(9)));
    ui::checkbox("Enable aim assist", a.enabled);
    ui::combo_row("Hold key", "##aimkey", a.key, aim::kKeyNames, aim::kKeyCount, 130.f);
    ui::checkbox("Lead moving targets", a.predict);
    ui::checkbox("Compensate bullet drop", a.drop);
    ui::checkbox("Use measured gravity", a.useMeasured);
    ui::checkbox("Visible only (no walls)", a.visibleOnly);
    ui::checkbox("Draw FOV circle", a.drawFov);
    widgets::slider_float_row("FOV (pixels)", &a.fovPx, 40.f, 600.f, "%.0f");
    widgets::slider_float_row("Strength", &a.strength, 0.05f, 1.5f, "%.2f");
    ui::group_end();

    ui::group_begin("##magic", "Magic bullet", ImVec2(kRight, kTop), column(rows(3)));
    ui::checkbox("Enable magic bullet (spawn in front of target)", a.magic);
    widgets::slider_float_row("Magic FOV (px)", &a.magicFovPx, 40.f, 1200.f, "%.0f");
    widgets::slider_float_row("Spawn distance", &a.magicSpawnDist, 0.3f, 6.f, "%.1f m");
    ui::group_end();

    ui::group_begin("##aim2", "Targeting", ImVec2(kRight, kTop + rows(3) + 10.f), column(rows(4)));
    widgets::slider_float_row("Aim height", &a.aimHeight, 0.4f, 1.0f, "%.2f");
    widgets::slider_float_row("Max distance", &a.maxDistance, 30.f, 800.f, "%.0f m");
    widgets::slider_float_row("Manual gravity", &a.gravity, 2.f, 40.f, "%.2f");
    widgets::slider_float_row("Max step", &a.maxStep, 5.f, 100.f, "%.0f");
    ui::group_end();
    ImGui::PopID();
}

// ---------------- Settings ----------------
void pageSettings(int nav) {
    static const char* const scales[]  = { "75%", "100%", "125%", "150%" };
    static const float       factors[] = { 0.75f, 1.0f, 1.25f, 1.5f };

    ImGui::PushID("settings");
    if (nav == 0) {                                   // ---- General
        int scaleIdx = 1; float best = 99.f;
        for (int i = 0; i < 4; ++i) if (fabsf(factors[i] - uiScale) < best) { best = fabsf(factors[i] - uiScale); scaleIdx = i; }
        ui::group_begin("##menu", "Menu", ImVec2(kLeft, kTop), column(rows(4)));
        ui::row("Menu key", 28.f, 110.f, [](ImVec2 pos, float h) {
            ImGui::SetCursorScreenPos(pos);
            std::string label = binding ? "press a key..." : keyName(toggleKey);
            if (ui::button(label.c_str(), ImVec2(110.f, h / ui::g_scale))) binding = true;
        });
        if (binding) ui::text_label("ESC cancels");
        ui::swatch_row("Accent colour", "##accent", theme::accent);
        if (ui::combo_row("UI scale", "##scale", scaleIdx, scales, 4)) uiScale = factors[scaleIdx];
        ui::group_end();

        ui::group_begin("##unload", "Session", ImVec2(kRight, kTop), column(rows(2)));
        if (ui::button("Unload yankeehook", ImVec2(0.f, 26.f))) wantUnload = true;
        ui::text_label("Crash log: %LOCALAPPDATA%\\yankeehook\\crash.log");
        ui::group_end();
    } else if (nav == 1) {                            // ---- Config
        static std::vector<std::string> names; static double nextRefresh = 0; static int sel = 0; static char nameBuf[48] = "";
        double now = ImGui::GetTime();
        if (now >= nextRefresh) { names = config::list(); nextRefresh = now + 2.0; if (sel >= (int)names.size()) sel = 0; }
        std::vector<const char*> items; for (auto& n : names) items.push_back(n.c_str());

        ui::group_begin("##cfg", "Configs", ImVec2(kLeft, kTop), column(rows(6)));
        if (items.empty()) ui::text_label("No configs saved yet");
        else ui::combo_row("Config", "##cfglist", sel, items.data(), (int)items.size(), 150.f);
        if (!items.empty() && ui::button("Load selected", ImVec2(0.f, 24.f))) { config::load(names[sel]); nextRefresh = 0; }
        if (!items.empty() && ui::button("Delete selected", ImVec2(0.f, 24.f))) { config::remove(names[sel]); nextRefresh = 0; }
        ImGui::SetNextItemWidth(ui::S(200.f));
        ImGui::InputTextWithHint("##cfgname", "new config name", nameBuf, sizeof(nameBuf));
        if (ui::button("Save as new name", ImVec2(0.f, 24.f)) && nameBuf[0]) { config::save(nameBuf); nextRefresh = 0; }
        if (!items.empty() && ui::button("Overwrite selected", ImVec2(0.f, 24.f))) { config::save(names[sel]); nextRefresh = 0; }
        ui::group_end();

        ui::group_begin("##cfgd", "Startup config", ImVec2(kRight, kTop), column(rows(5)));
        ui::text_label("\"default\" is loaded when yankeehook starts.");
        if (ui::button("Save current settings as default", ImVec2(0.f, 24.f))) { config::saveDefault(); nextRefresh = 0; }
        if (ui::button("Load default now", ImVec2(0.f, 24.f))) config::load("default");
        ui::text_label(config::status.empty() ? "" : config::status.c_str());
        ui::text_label(("Folder: " + config::directory()).c_str());
        ui::group_end();
    } else if (nav == 2) {                            // ---- Debug
        camera::Cam c = camera::read();
        char b[160];
        ui::group_begin("##dcam", "Camera", ImVec2(kLeft, kTop), column(rows(7)));
        if (c.valid) {
            snprintf(b, sizeof(b), "Pos  %.2f  %.2f  %.2f", c.pos.x, c.pos.y, c.pos.z); ui::text_label(b);
            snprintf(b, sizeof(b), "Fwd  %.3f  %.3f  %.3f", c.fwd.x, c.fwd.y, c.fwd.z); ui::text_label(b);
            snprintf(b, sizeof(b), "Right  %.3f  %.3f  %.3f", c.right.x, c.right.y, c.right.z); ui::text_label(b);
            snprintf(b, sizeof(b), "Up  %.3f  %.3f  %.3f", c.up.x, c.up.y, c.up.z); ui::text_label(b);
            float yaw = atan2f(-c.fwd.x, c.fwd.y) * 57.29578f;
            float pitch = asinf(std::clamp(c.fwd.z, -1.f, 1.f)) * 57.29578f;
            snprintf(b, sizeof(b), "Yaw %.1f   Pitch %.1f", yaw, pitch); ui::text_label(b);
            float cur;
            if (fov::read(cur)) { snprintf(b, sizeof(b), "cl_fov  %.1f", cur); ui::text_label(b); }
        } else ui::text_label("n/a (matrix unreadable)");
        ui::group_end();

        ui::group_begin("##dhook", "Hooks", ImVec2(kRight, kTop), column(rows(8)));
        snprintf(b, sizeof(b), "Reload/update: %s", reloadspeed::diag.installResult); ui::text_label(b);
        snprintf(b, sizeof(b), "Camera: %s  calls %d applied %d", camhook::diag.installResult, (int)camhook::diag.calls, (int)camhook::diag.applied); ui::text_label(b);
        snprintf(b, sizeof(b), "Muzzle fx: %s  moved %d / %d", volleyfx::diag.installResult, (int)volleyfx::diag.moved, (int)volleyfx::diag.spawns); ui::text_label(b);
        snprintf(b, sizeof(b), "Magic: queued %d  fired %d  refused %d  | shoot hook %s calls %d redirected %d", (int)magic::diag.queued, (int)magic::diag.magicFired, (int)magic::diag.magicFailed, magic::diag.shootInstall, (int)magic::diag.shootCalls, (int)magic::diag.shootRedirected); ui::text_label(b);
        snprintf(b, sizeof(b), "Visibility rays %d  blocked %d  faults %d%s", (int)visible::diag.rays, (int)visible::diag.blocked, (int)visible::diag.faults, visible::diag.disabled ? "  DISABLED" : ""); ui::text_label(b);
        auto st = aim::stats(); auto m = tracers::measured();
        snprintf(b, sizeof(b), "Bullet speed %.0f m/s  gravity used %.2f", st.bulletSpeed, st.gravityUsed); ui::text_label(b);
        snprintf(b, sizeof(b), "Measured: g %.2f  v %.0f  (%d trails)", m.gravity, m.speed, m.samples); ui::text_label(b);
        snprintf(b, sizeof(b), "Target %s  dist %.0f m  flight %.2f s", st.haveTarget ? "yes" : "no", st.targetDist, st.flightTime); ui::text_label(b);
        ui::group_end();

        ui::group_begin("##dvol", "Volley", ImVec2(kRight, kTop + rows(8) + 10.f), column(rows(3)));
        snprintf(b, sizeof(b), "ok %d fail %d (ret %d canShoot %d)  left %d  slot %X", (int)reloadspeed::volley.fired, (int)reloadspeed::volley.failed, (int)reloadspeed::volley.lastRet, (int)reloadspeed::volley.canShoot, (int)reloadspeed::volleyLeft, reloadspeed::volley.shootRva); ui::text_label(b);
        snprintf(b, sizeof(b), "rejected %d  not held %d  not mine %d", (int)reloadspeed::volley.rejected, (int)reloadspeed::volley.noMatch, (int)reloadspeed::volley.notMine); ui::text_label(b);
        snprintf(b, sizeof(b), "exception %X @%X x%d", reloadspeed::volley.exCode, reloadspeed::volley.exRva, (int)reloadspeed::volley.exCount); ui::text_label(b);
        ui::group_end();

        ui::group_begin("##dscan", "Weapon data", ImVec2(kLeft, kTop + rows(7) + 10.f), column(rows(5)));
        snprintf(b, sizeof(b), "Held item %X: %d fire, %d reload, %d sway, %d drop", weaponscan::heldItemId.load(), weaponscan::heldFire.load(), weaponscan::heldReload.load(), weaponscan::heldSway.load(), weaponscan::heldDrop.load());
        ui::text_label(b);
        ui::checkbox("Held weapon only", weaponscan::heldOnly);
        if (!weaponscan::heldOnly) {
            if (weaponscan::scanning) snprintf(b, sizeof(b), "Scanning... %d%%", weaponscan::scanPercent.load());
            else snprintf(b, sizeof(b), "Global scan: %d fire modes, %d reload tables", weaponscan::fireModes.load(), weaponscan::reloadTables.load());
            ui::text_label(b);
            if (ui::button(weaponscan::scanning ? "Scanning..." : "Scan all weapons", ImVec2(0.f, 22.f)) && !weaponscan::scanning) weaponscan::startScan();
        }
        ui::group_end();
    } else {                                          // ---- Advanced
        ui::group_begin("##stance", "Stance box heights", ImVec2(kLeft, kTop), column(rows(5)));
        auto& tbl = esp::stanceTable();
        if (tbl.empty()) ui::text_label("No stances seen yet");
        static char labels[16][48];
        int n = 0;
        for (auto& kv : tbl) {
            if (n >= 16) break;
            snprintf(labels[n], sizeof(labels[n]), "%08X  (%d now)", kv.first, kv.second.liveCount);
            widgets::slider_float_row(labels[n], &kv.second.height, 0.2f, 2.2f, "%.2f m");
            ++n;
        }
        ui::group_end();

        ui::group_begin("##rdbg", "Reload debug", ImVec2(kRight, kTop), column(rows(14)));
        {
            auto& d = reloadspeed::diag;
            char b[160];
            snprintf(b, sizeof(b), "Reloads %d  yours %d  table %d  anim %d", d.calls, d.localHits, d.tablesScaled, d.boosted); ui::text_label(b);
            snprintf(b, sizeof(b), "Owner %llX  local %llX", d.ownerId, d.localId); ui::text_label(b);
            snprintf(b, sizeof(b), "was %.2f %.2f %.2f %.2f  %.2f %.2f %.2f %.2f", d.tableOrig[0], d.tableOrig[1], d.tableOrig[2], d.tableOrig[3], d.tableOrig[4], d.tableOrig[5], d.tableOrig[6], d.tableOrig[7]); ui::text_label(b);
            snprintf(b, sizeof(b), "now %.2f %.2f %.2f %.2f  %.2f %.2f %.2f %.2f", d.tableNow[0], d.tableNow[1], d.tableNow[2], d.tableNow[3], d.tableNow[4], d.tableNow[5], d.tableNow[6], d.tableNow[7]); ui::text_label(b);
            if (d.busyMs >= 0) snprintf(b, sizeof(b), "Weapon busy for %d ms   forced %d", d.busyMs, d.forced);
            else snprintf(b, sizeof(b), "Weapon busy: waiting...   forced %d", d.forced);
            ui::text_label(b);
            snprintf(b, sizeof(b), "Clip before %d  after %d  size %d", d.clipBefore, d.clipAfter, d.clipCap); ui::text_label(b);
            snprintf(b, sizeof(b), "Reserve before %d  after %d", d.entGet928, d.entGet928After); ui::text_label(b);
            snprintf(b, sizeof(b), "Weapon timer +88: now %.2f  max %.2f s", d.timerNow, d.timerMax); ui::text_label(b);
            auto& g = d.gate;
            snprintf(b, sizeof(b), "CanShoot %d  busy127 %d  f418 %d  timer %.2f", g.canShoot, g.b127, g.b418, g.timer); ui::text_label(b);
            snprintf(b, sizeof(b), "ent121 %d ent120 %d  noAmmo(v72) %d  cap %d", g.e121, g.e120, g.v72, g.cap); ui::text_label(b);
            snprintf(b, sizeof(b), "2nd R: sent %d  busy %d  key %d   auto reloads %d", d.secondRCount, d.secondRBusy, d.secondRKey, d.autoReloads); ui::text_label(b);
            ui::text_label(d.probeStatus);
        }
        ui::group_end();
    }
    ImGui::PopID();
}
} // namespace

void init() {
    using ui::icon_t;
    auto& m = *yankeehook::gui::menu;
    m.m_name = "yankeehook";
    m.add_page({ "Visuals", icon_t::eye, { { "ESP", "Boxes, filters" }, { "Chams", "Player colours" }, { "Tracers", "Bullet trails" }, { "View", "FOV, crosshair, camera" } }, pageVisuals });
    m.add_page({ "Weapon", icon_t::code, { { "Gun mods", "One switch, sliders" }, { "Volley", "Volley and fire gun" } }, pageWeapon });
    m.add_page({ "Aim", icon_t::crosshair, { }, pageAim });
    m.add_page({ "Entities", icon_t::bars, { }, pageEntities });
    m.add_page({ "Player", icon_t::crosshair, { }, pagePlayer });
    m.add_page({ "Settings", icon_t::gear, { { "General", "Menu, session" }, { "Config", "Save and load" }, { "Debug", "Hooks, camera" }, { "Advanced", "Stances, reload" } }, pageSettings });
}

void render() {
    ui::g_scale = uiScale;
    ImGui::GetIO().FontGlobalScale = uiScale;
    yankeehook::gui::menu->on_render();
}
}
