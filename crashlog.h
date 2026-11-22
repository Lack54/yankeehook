#pragma once
#include <string>

// Crash logging, in a folder the game can always write to: %LOCALAPPDATA%\yankeehook\
//   crash.log     appended on every crash the unhandled-exception filter sees (registers, module + offset, stack candidates,
//                 breadcrumbs, features on)
//   session.bin   memory-mapped record of the last breadcrumbs and the last first-chance fault that happened outside this DLL.
//                 It survives a hard crash (fast-fail, terminate, killed process) that never reaches the filter; the next time
//                 the DLL loads it notices the previous session did not end cleanly and writes that record into crash.log.
namespace crashlog {
void install();
void uninstall();                 // clean unload (Unload button)
void markClean();                 // process is exiting normally (DLL_PROCESS_DETACH)
void note(const char* what);      // breadcrumb: the last 16 are kept
std::wstring logFolder();         // where the files are
}
