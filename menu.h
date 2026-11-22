#pragma once

namespace menu {
extern bool wantUnload;    // set by the Unload button
extern int  toggleKey;     // virtual-key code, default VK_DELETE
extern int  cfgGeneration;  // bumped whenever a config is loaded (menu widgets re-sync from the settings)
extern bool binding;       // true while waiting for a new key
extern float uiScale;      // menu scale (1.0 = 100%)

void init();               // registers pages with the yankeehook UI kit
void render();             // call between NewFrame and Render
void setOpen(bool v);
bool isOpen();
void toggle();
const char* keyName(int vk);
}
