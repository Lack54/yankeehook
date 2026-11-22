#pragma once

// On-screen crosshair, drawn at the centre of the screen every frame (also with the menu closed).
namespace crosshair {
struct Settings {
    bool  enabled = true;          // default on
    int   style = 0;               // 0 cross, 1 dot, 2 cross + dot, 3 circle
    float size = 9.f;              // arm length / radius in px
    float gap = 4.f;               // gap around the centre for the cross styles
    float thickness = 1.5f;
    bool  outline = true;
    float color[4] = { 0.424f, 0.710f, 1.0f, 1.0f };
};
extern Settings s;
extern const char* const kStyleNames[];
extern const int kStyleCount;
void draw();
}
