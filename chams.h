#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

// D3D11 "chams": hooks DrawIndexed / DrawIndexedInstanced on the game's contexts and redraws character meshes with a
// flat-colour pixel shader.
//   mode 1 = Visible only   (flat colour where the model is visible)
//   mode 2 = Through walls  (hidden colour everywhere with depth test off, then visible colour where visible)
//
// Which draws are characters is learned by correlation: every input-layout "class" has a per-frame draw count; a class whose
// count rises and falls with the number of bots on screen is a character class. (A fixed first-person arm mesh using the same
// skinned format adds a constant, which correlation ignores.) Classes can also be forced on by hand, and one previewed in magenta.
namespace chams {
struct Settings {
    int   mode = 0;                                  // 0 off, 1 visible, 2 through walls
    float visCol[4] = { 0.42f, 0.71f, 1.0f, 1.0f };
    float hidCol[4] = { 1.0f, 0.30f, 0.30f, 1.0f };
    bool  autoDetect = true;
};
extern Settings s;

struct ClassView {
    uintptr_t id = 0;        // input layout pointer
    float avgDraws = 0;      // draws per frame (smoothed)
    float corr = 0;          // correlation of draws/frame with bots on screen
    float slope = 0;         // extra draws per frame for each bot on screen
    int   samples = 0;
    bool  detected = false;  // auto-detected as a character class
    bool  manual = false;
    bool  preview = false;
    unsigned stride = 0, indexCount = 0;
};

struct Stats {
    bool  immediateHooked = false, deferredHooked = false;
    int   callsPerFrame = 0;     // draw calls seen by the hook last frame (all contexts, all passes)
    int   mainPassPerFrame = 0;  // of those: colour-pass draws (render target + pixel shader bound)
    int   classes = 0;           // distinct input layouts seen
    int   appliedPerFrame = 0;   // draws that were recoloured
    int   botsOnScreen = 0;
};

bool install(ID3D11Device* dev, ID3D11DeviceContext* ctx);
void uninstall();
void endFrame(int botsOnScreen);             // once per frame, from the Present hook
void setBypass(bool on);                     // true while our own UI is being drawn
std::vector<ClassView> snapshot(int maxCount);
Stats stats();
void setManual(uintptr_t id, bool on);
void setPreview(uintptr_t id);               // 0 clears
void resetLearning();
}
