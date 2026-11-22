#pragma once

// Compile-time switches for the features added after the "pre-recoil" version. false = no menu entry and nothing scans for their data.
constexpr bool kInfiniteAmmo  = true;    // Infinite ammo (pocket reserve) and Infinite clip
constexpr bool kRecoil        = true;    // No recoil (switched on: it was asked for again)
constexpr bool kRespawnTimer  = false;   // No respawn timer
