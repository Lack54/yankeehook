#include "crashlog.h"
#include "esp.h"
#include "chams.h"
#include "tracers.h"
#include "godmode.h"
#include "weapon.h"
#include "weaponscan.h"
#include "reloadspeed.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace crashlog {
namespace {
struct Shared {
    char magic[8];                 // "YHKSESS1"
    volatile LONG clean;           // 0 while running, 1 after a normal exit / unload
    volatile LONG idx;             // breadcrumb ring index
    char crumbs[16][72];
    volatile LONG vehSeq;          // number of recorded first-chance faults
    char veh[640];                 // the last one, as text
};

LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;
PVOID g_veh = nullptr;
bool g_installed = false;
volatile LONG g_inFilter = 0;
Shared g_local{};                  // used if the mapping could not be created
Shared* g_sh = &g_local;
HANDLE g_file = INVALID_HANDLE_VALUE, g_map = nullptr;
HMODULE g_self = nullptr;

std::wstring folder() {
    wchar_t base[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) n = GetTempPathW(MAX_PATH, base);
    std::wstring dir = base;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    dir += L"\\yankeehook";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

const char* moduleOf(const void* addr, uintptr_t& base, char* buf, size_t cap) {
    HMODULE m = nullptr;
    base = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m) && m) {
        base = (uintptr_t)m;
        char path[MAX_PATH] = {};
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        snprintf(buf, cap, "%s", slash ? slash + 1 : path);
        return buf;
    }
    snprintf(buf, cap, "?");
    return buf;
}

const char* codeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN / fast fail";
    default: return "other";
    }
}

bool fatalKind(DWORD c) {
    return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_ILLEGAL_INSTRUCTION || c == EXCEPTION_STACK_OVERFLOW ||
           c == EXCEPTION_INT_DIVIDE_BY_ZERO || c == EXCEPTION_PRIV_INSTRUCTION || c == EXCEPTION_IN_PAGE_ERROR;
}

FILE* openLog() {
    std::wstring p = folder() + L"\\crash.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"a") != 0) return nullptr;
    return f;
}

void stamp(FILE* f, const char* title) {
    time_t t = time(nullptr); tm lt{};
    localtime_s(&lt, &t);
    char ts[64]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &lt);
    fprintf(f, "==================== %s %s (thread %lu) ====================\n", title, ts, GetCurrentThreadId());
}

void writeCrumbs(FILE* f, const Shared& s) {
    fprintf(f, "breadcrumbs (oldest to newest):\n");
    LONG idx = s.idx;
    for (int i = 1; i <= 16; ++i) {
        const char* c = s.crumbs[(idx + i) & 15];
        if (c[0]) fprintf(f, "  %s\n", c);
    }
}

void writeFeatures(FILE* f) {
    fprintf(f, "features: esp=%d chams=%d tracers=%d god=%d noHeadBob=%d noSupp=%d instantReload=%d apply=%d noSway=%d noRecoil=%d "
               "forceFinish=%d autoReload=%d secondR=%d infAmmo=%d noRespawnTimer=%d\n",
            esp::s.enabled, chams::s.mode, tracers::s.enabled, godmode::enabled, weapon::noHeadBob, weapon::noSuppression,
            weaponscan::instantReload, weaponscan::apply, weaponscan::noSway, weaponscan::noRecoil,
            reloadspeed::forceFinish, reloadspeed::autoReload, reloadspeed::secondR, reloadspeed::infiniteAmmo, weapon::noRespawnTimer);
    fprintf(f, "reload hook: %s   reloads %d  forced %d  auto %d\n", reloadspeed::diag.installResult, (int)reloadspeed::diag.calls,
            (int)reloadspeed::diag.forced, (int)reloadspeed::diag.autoReloads);
}

void writeLog(EXCEPTION_POINTERS* ep) {
    FILE* f = openLog();
    if (!f) return;
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    CONTEXT* c = ep->ContextRecord;
    char mod[MAX_PATH]; uintptr_t base = 0;
    moduleOf(er->ExceptionAddress, base, mod, sizeof(mod));

    stamp(f, "crash");
    fprintf(f, "code     %08lX  %s\n", er->ExceptionCode, codeName(er->ExceptionCode));
    fprintf(f, "address  %p  = %s + 0x%llX\n", er->ExceptionAddress, mod, (unsigned long long)((uintptr_t)er->ExceptionAddress - base));
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(f, "access   %s of %p\n", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute", (void*)er->ExceptionInformation[1]);
    fprintf(f, "regs     RAX %016llX RBX %016llX RCX %016llX RDX %016llX\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
    fprintf(f, "         RSI %016llX RDI %016llX RBP %016llX RSP %016llX\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    fprintf(f, "         R8  %016llX R9  %016llX R10 %016llX R11 %016llX\n", c->R8, c->R9, c->R10, c->R11);
    fprintf(f, "         R12 %016llX R13 %016llX R14 %016llX R15 %016llX\n", c->R12, c->R13, c->R14, c->R15);
    fprintf(f, "         RIP %016llX\n", c->Rip);

    fprintf(f, "stack (return-address candidates, newest first):\n");
    int shown = 0;
    __try {
        const uintptr_t* sp = (const uintptr_t*)c->Rsp;
        for (int i = 0; i < 2048 && shown < 40; ++i) {
            uintptr_t v = sp[i];
            if (v < 0x10000 || v > 0x00007FFFFFFF0000ull) continue;
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery((void*)v, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) continue;
            if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
            uintptr_t b = 0; char m[MAX_PATH];
            moduleOf((void*)v, b, m, sizeof(m));
            fprintf(f, "  %p  %s + 0x%llX\n", (void*)v, m, (unsigned long long)(v - b));
            ++shown;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { fprintf(f, "  (stack unreadable)\n"); }

    writeCrumbs(f, *g_sh);
    if (g_sh->vehSeq) fprintf(f, "last first-chance fault outside this DLL (#%ld): %s\n", g_sh->vehSeq, g_sh->veh);
    writeFeatures(f);
    fprintf(f, "\n");
    fclose(f);
}

LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    if (InterlockedCompareExchange(&g_inFilter, 1, 0) == 0) {      // log once, never recurse
        __try { writeLog(ep); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_sh->clean = 2;                                           // 2 = crash already logged
    }
    return g_prev ? g_prev(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// First-chance faults that happen in code that is not this DLL (the game, the driver, system DLLs) and are of a crash-like kind.
// Our own guarded memory reads fault inside this DLL on purpose and are ignored. The last record is kept in the mapped file, so
// if the process then dies without reaching the unhandled filter, the next session can still report it.
LONG CALLBACK veh(EXCEPTION_POINTERS* ep) {
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    if (!fatalKind(er->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)er->ExceptionAddress, &m) && m == g_self)
        return EXCEPTION_CONTINUE_SEARCH;
    char mod[MAX_PATH]; uintptr_t base = 0;
    moduleOf(er->ExceptionAddress, base, mod, sizeof(mod));
    const CONTEXT* c = ep->ContextRecord;
    char text[sizeof(g_sh->veh)];
    int n = snprintf(text, sizeof(text), "%s at %p (%s+0x%llX)", codeName(er->ExceptionCode), er->ExceptionAddress, mod,
                     (unsigned long long)((uintptr_t)er->ExceptionAddress - base));
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 && n > 0 && n < (int)sizeof(text))
        n += snprintf(text + n, sizeof(text) - n, " %s %p", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "exec", (void*)er->ExceptionInformation[1]);
    if (n > 0 && n < (int)sizeof(text))
        snprintf(text + n, sizeof(text) - n, " RAX=%llX RCX=%llX RDX=%llX RSP=%llX", c->Rax, c->Rcx, c->Rdx, c->Rsp);
    memcpy(g_sh->veh, text, sizeof(text));
    InterlockedIncrement(&g_sh->vehSeq);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Map session.bin; report a previous session that never marked itself clean.
void openSession() {
    std::wstring p = folder() + L"\\session.bin";
    Shared prev{};
    bool havePrev = false;
    HANDLE rf = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (rf != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (ReadFile(rf, &prev, sizeof(prev), &got, nullptr) && got == sizeof(prev) && memcmp(prev.magic, "YHKSESS1", 8) == 0) havePrev = true;
        CloseHandle(rf);
    }
    if (havePrev && prev.clean == 0) {
        if (FILE* f = openLog()) {
            stamp(f, "previous session ended without a clean shutdown");
            fprintf(f, "(the game process died without the unhandled-exception filter running: fast-fail, terminate, killed, or\n a crash before this DLL was injected)\n");
            writeCrumbs(f, prev);
            if (prev.vehSeq) fprintf(f, "last first-chance fault outside this DLL (#%ld): %s\n", prev.vehSeq, prev.veh);
            else fprintf(f, "no first-chance fault outside this DLL was recorded\n");
            fprintf(f, "\n");
            fclose(f);
        }
    }
    g_file = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return;
    g_map = CreateFileMappingW(g_file, nullptr, PAGE_READWRITE, 0, sizeof(Shared), nullptr);
    if (!g_map) return;
    void* v = MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!v) return;
    g_sh = (Shared*)v;
    memset(g_sh, 0, sizeof(Shared));
    memcpy(g_sh->magic, "YHKSESS1", 8);
    g_sh->clean = 0;
}
} // namespace

std::wstring logFolder() { return folder(); }

void note(const char* what) {
    LONG i = InterlockedIncrement(&g_sh->idx);
    strncpy_s(g_sh->crumbs[i & 15], sizeof(g_sh->crumbs[0]), what, _TRUNCATE);
}

void markClean() { if (g_sh) g_sh->clean = 1; }

void install() {
    if (g_installed) return;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&install, &g_self);
    openSession();
    g_prev = SetUnhandledExceptionFilter(filter);
    g_veh = AddVectoredExceptionHandler(1, veh);
    g_installed = true;
    note("crashlog installed");
}

void uninstall() {
    if (!g_installed) return;
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
    LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(g_prev);
    if (cur != filter) SetUnhandledExceptionFilter(cur);       // someone replaced ours: put theirs back
    markClean();
    g_installed = false;
}
}
