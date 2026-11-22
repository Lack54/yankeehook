#pragma once
#include <string>
#include <vector>

// Settings profiles. Files live in %LOCALAPPDATA%\yankeehook\configs\<name>.cfg (plain "key=value" lines).
// "default" is the startup config: it is loaded when the DLL starts. If it does not exist yet it is created from the built-in defaults.
namespace config {
void init();                                  // create the folder, load (or create) default.cfg
std::vector<std::string> list();              // config names without extension, sorted
bool save(const std::string& name);
bool load(const std::string& name);
bool remove(const std::string& name);         // "default" cannot be deleted
bool saveDefault();                           // write the current settings as the startup config
extern std::string status;                    // last action result, shown in the menu
std::string directory();
}
