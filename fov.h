#pragma once

// FOV changer: writes the game's cl_fov CVar value (holder 0x1415CF820 + 0x38, registered as a float, no flags).
namespace fov {
extern bool  enabled;
extern bool  pauseWhileAiming;   // don't force the FOV while the game is zooming (aiming down sights)
extern float value;        // degrees
bool  read(float& out);    // current cl_fov; false if unreadable
void  tick();              // call once per frame
void  restore();           // put back the original value (on disable / unload)
float effective(float base);   // FOV for projection math: the changer value when active, else base
}
