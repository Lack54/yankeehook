#pragma once
#include <string>
#include <vector>

// Bullet tracers. Bullets are entities whose class is named after the ammo (RoundBall, MinieBall, BuckAndBall, ...).
// Each frame the entity array (g_CEntitySystem + 0x80, highest index u16 at +0x4012E) is scanned for them, their path is
// recorded and drawn as a fading trail. The shooter is taken to be the nearest player to where the bullet first appears;
// the colour depends on whether that is you, a teammate (same faction as you) or an enemy.
namespace tracers {
struct Settings {
    bool  enabled = false;       // off by default (boxes are the default look)
    bool  showSelf = true, showTeam = true, showEnemy = true;
    float colSelf[4]  = { 0.40f, 1.00f, 0.45f, 1.0f };
    float colTeam[4]  = { 0.40f, 0.70f, 1.00f, 1.0f };
    float colEnemy[4] = { 1.00f, 0.30f, 0.30f, 1.0f };
    float lifetime = 0.8f;      // seconds a trail stays visible
    float thickness = 2.0f;
    bool  glow = true;
    float maxDistance = 600.f;  // don't draw trails starting farther than this from you
};
extern Settings s;

struct Stats {
    int entitiesScanned = 0;
    int bulletsNow = 0;         // bullet entities alive this frame
    int tracks = 0;             // trails being drawn
    int moving = 0;             // trails with more than one distinct point
    int bulletClasses = 0;      // distinct class names recognised as bullets
    std::string lastBulletClass;
    std::string sampleClasses;  // a few recent non-bullet class names, to help diagnose recognition
};

struct Measured { float gravity = 0; float speed = 0; int samples = 0; };
extern bool measureRequested;   // set by the aim assist: scan bullets even when tracers are not drawn
Measured measured();            // bullet speed (m/s) and gravity (m/s^2) fitted from finished bullet trails

void update();   // scan + record (render thread, once per frame, after entities::update)
void draw();     // draw trails into the background draw list
Stats stats();
}
