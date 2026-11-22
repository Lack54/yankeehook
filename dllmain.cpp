#include <windows.h>
#include "hook.h"
#include "menu.h"
#include "crashlog.h"

static HMODULE g_self;

static DWORD WINAPI MainThread(LPVOID) {
    crashlog::install();
    if (hook::install()) {
        while (!menu::wantUnload) Sleep(50);
        hook::uninstall();
    }
    crashlog::uninstall();
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) crashlog::markClean();    // the process is exiting normally
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
