#include "config.h"
#include "esp.h"
#include "tracers.h"
#include "chams.h"
#include "aim.h"
#include "camhook.h"
#include "fov.h"
#include "magic.h"
#include "volleyfx.h"
#include "crosshair.h"
#include "reloadspeed.h"
#include "weaponscan.h"
#include "weapon.h"
#include "godmode.h"
#include "menu.h"
#include "ui/theme.hpp"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace config {
std::string status = "";

namespace {
enum class T { Bool, Int, Float, Color };
struct Entry { const char* key; T t; void* p; };

std::vector<Entry>& reg() {
    static std::vector<Entry> r;
    if (!r.empty()) return r;
    auto B = [&](const char* k, bool& v) { r.push_back({ k, T::Bool, &v }); };
    auto I = [&](const char* k, int& v) { r.push_back({ k, T::Int, &v }); };
    auto F = [&](const char* k, float& v) { r.push_back({ k, T::Float, &v }); };
    auto C = [&](const char* k, float* v) { r.push_back({ k, T::Color, v }); };

    // ESP
    B("esp.enabled", esp::s.enabled); B("esp.box", esp::s.box); B("esp.name", esp::s.name); B("esp.health", esp::s.health);
    B("esp.distance", esp::s.distance); B("esp.snaplines", esp::s.snaplines); B("esp.showDead", esp::s.showDead); B("esp.teamCheck", esp::s.teamCheck);
    C("esp.colEnemy", esp::s.colEnemy); C("esp.colTeam", esp::s.colTeam);
    F("esp.maxDistance", esp::s.maxDistance); F("esp.boxHeight", esp::s.boxHeight); F("esp.lowHeight", esp::s.lowHeight);
    F("esp.fov", esp::s.fov); F("esp.thickness", esp::s.thickness);
    // chams
    I("chams.mode", chams::s.mode); C("chams.visCol", chams::s.visCol); C("chams.hidCol", chams::s.hidCol); B("chams.autoDetect", chams::s.autoDetect);
    // tracers
    B("tracers.enabled", tracers::s.enabled); B("tracers.showSelf", tracers::s.showSelf); B("tracers.showTeam", tracers::s.showTeam);
    B("tracers.showEnemy", tracers::s.showEnemy); B("tracers.glow", tracers::s.glow);
    C("tracers.colSelf", tracers::s.colSelf); C("tracers.colTeam", tracers::s.colTeam); C("tracers.colEnemy", tracers::s.colEnemy);
    F("tracers.lifetime", tracers::s.lifetime); F("tracers.thickness", tracers::s.thickness); F("tracers.maxDistance", tracers::s.maxDistance);
    // view
    B("fov.enabled", fov::enabled); F("fov.value", fov::value); B("fov.pauseWhileAiming", fov::pauseWhileAiming);
    B("crosshair.enabled", crosshair::s.enabled); I("crosshair.style", crosshair::s.style); F("crosshair.size", crosshair::s.size);
    F("crosshair.gap", crosshair::s.gap); F("crosshair.thickness", crosshair::s.thickness); B("crosshair.outline", crosshair::s.outline);
    C("crosshair.color", crosshair::s.color);
    I("cam.tpKey", camhook::tpKey); I("cam.fcKey", camhook::fcKey); F("cam.tpDistance", camhook::tpDistance); F("cam.tpHeight", camhook::tpHeight);
    F("cam.fcSpeed", camhook::fcSpeed);
    // aim
    B("aim.enabled", aim::s.enabled); I("aim.key", aim::s.key); F("aim.fovPx", aim::s.fovPx); F("aim.strength", aim::s.strength);
    F("aim.maxStep", aim::s.maxStep); F("aim.aimHeight", aim::s.aimHeight); F("aim.maxDistance", aim::s.maxDistance);
    B("aim.predict", aim::s.predict); B("aim.drop", aim::s.drop); B("aim.useMeasured", aim::s.useMeasured); F("aim.gravity", aim::s.gravity);
    B("aim.drawFov", aim::s.drawFov); B("aim.visibleOnly", aim::s.visibleOnly); B("aim.magic", aim::s.magic); F("aim.magicFovPx", aim::s.magicFovPx); F("aim.magicSpawnDist", aim::s.magicSpawnDist);
    // gun mods
    B("gun.mods", weaponscan::gunMods); B("gun.heldOnly", weaponscan::heldOnly);
    F("gun.damageMul", weaponscan::damageMul); F("gun.speedMul", weaponscan::speedMul); F("gun.reloadMul", weaponscan::reloadMul);
    F("gun.suppressionMul", weaponscan::suppressionMul); F("gun.reloadAnimSpeed", reloadspeed::speed); F("gun.cancelAfter", weapon::reloadCancelValue);
    // volley
    I("volley.count", reloadspeed::volleyCount); I("volley.perTick", reloadspeed::volleyPerTick); I("volley.delayMs", reloadspeed::volleyDelayMs);
    I("volley.jitterMs", reloadspeed::volleyJitterMs); B("volley.ripple", reloadspeed::volleyRipple); B("volley.lockAim", reloadspeed::volleyLockAim);
    B("volley.hotkey", reloadspeed::volleyHot); I("volley.key", reloadspeed::volleyKey);
    B("volley.fireHot", reloadspeed::fireHot); I("volley.fireKey", reloadspeed::fireKey); I("volley.fireMs", reloadspeed::fireMs);
    B("volley.line", magic::lineOn); F("volley.spacing", magic::lineSpacing); B("volley.damage", magic::volleyDamage); B("volley.smoke", volleyfx::enabled);
    // player / menu
    B("player.god", godmode::enabled);
    I("menu.key", menu::toggleKey); F("menu.scale", menu::uiScale);
    C("menu.accent", &yankeehook::gui::theme::accent.x);
    return r;
}

std::string dir() {
    char p[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p))) {
        std::string d = std::string(p) + "\\yankeehook";
        CreateDirectoryA(d.c_str(), nullptr);
        d += "\\configs";
        CreateDirectoryA(d.c_str(), nullptr);
        return d;
    }
    return ".";
}

std::string pathOf(const std::string& name) { return dir() + "\\" + name + ".cfg"; }

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 40) return false;
    for (char c : n) if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) return false;
    return true;
}

bool writeFile(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "# yankeehook config\n";
    for (auto& e : reg()) {
        f << e.key << "=";
        switch (e.t) {
        case T::Bool: f << (*(bool*)e.p ? 1 : 0); break;
        case T::Int: f << *(int*)e.p; break;
        case T::Float: f << *(float*)e.p; break;
        case T::Color: { float* c = (float*)e.p; f << c[0] << "," << c[1] << "," << c[2] << "," << c[3]; break; }
        }
        f << "\n";
    }
    return (bool)f;
}

bool readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        for (auto& e : reg()) {
            if (k != e.key) continue;
            switch (e.t) {
            case T::Bool: *(bool*)e.p = atoi(v.c_str()) != 0; break;
            case T::Int: *(int*)e.p = atoi(v.c_str()); break;
            case T::Float: { float x = (float)atof(v.c_str()); if (x == x) *(float*)e.p = x; break; }
            case T::Color: {
                float c[4]; int n = sscanf(v.c_str(), "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]);
                if (n == 4) memcpy(e.p, c, sizeof(c));
                break;
            }
            }
            break;
        }
    }
    return true;
}

void sanitize() {
    auto clampI = [](int& v, int lo, int hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    auto clampF = [](float& v, float lo, float hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    clampI(aim::s.key, 0, aim::kKeyCount - 1); clampI(reloadspeed::volleyKey, 0, aim::kKeyCount - 1); clampI(reloadspeed::fireKey, 0, aim::kKeyCount - 1);
    clampI(camhook::tpKey, 0, camhook::kKeyCount - 1); clampI(camhook::fcKey, 0, camhook::kKeyCount - 1);
    clampI(crosshair::s.style, 0, crosshair::kStyleCount - 1); clampI(chams::s.mode, 0, 2);
    clampI(reloadspeed::volleyCount, 1, 64); clampI(reloadspeed::volleyPerTick, 1, 60);
    clampI(reloadspeed::volleyDelayMs, 0, 1000); clampI(reloadspeed::volleyJitterMs, 0, 500); clampI(reloadspeed::fireMs, 30, 1500);
    clampF(menu::uiScale, 0.5f, 2.f); clampF(fov::value, 30.f, 150.f); clampF(tracers::s.lifetime, 0.1f, 10.f);
    clampF(crosshair::s.size, 1.f, 60.f); clampF(crosshair::s.gap, 0.f, 40.f); clampF(crosshair::s.thickness, 0.5f, 8.f);
    if (menu::toggleKey < 8 || menu::toggleKey > 254) menu::toggleKey = VK_DELETE;
    yankeehook::gui::theme::accent.w = 1.f;
}
}

std::string directory() { return dir(); }

std::vector<std::string> list() {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir() + "\\*.cfg").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n.size() > 4) out.push_back(n.substr(0, n.size() - 4));
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool save(const std::string& name) {
    if (!validName(name)) { status = "invalid name (letters, digits, space, - and _)"; return false; }
    bool ok = writeFile(pathOf(name));
    status = ok ? "saved " + name : "save failed";
    return ok;
}

bool load(const std::string& name) {
    if (!validName(name)) { status = "invalid name"; return false; }
    if (!readFile(pathOf(name))) { status = "cannot read " + name; return false; }
    sanitize();
    ++menu::cfgGeneration;                         // menu widgets re-sync from the loaded values
    status = "loaded " + name;
    return true;
}

bool remove(const std::string& name) {
    if (name == "default") { status = "the default config cannot be deleted"; return false; }
    if (!validName(name)) return false;
    bool ok = DeleteFileA(pathOf(name).c_str()) != 0;
    status = ok ? "deleted " + name : "delete failed";
    return ok;
}

bool saveDefault() {
    bool ok = writeFile(pathOf("default"));
    status = ok ? "saved as startup config (default)" : "save failed";
    return ok;
}

void init() {
    reg();
    std::string p = pathOf("default");
    DWORD a = GetFileAttributesA(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { writeFile(p); status = "created default config"; }
    else if (readFile(p)) { sanitize(); ++menu::cfgGeneration; status = "loaded default config"; }
}
}
