#pragma once

// No teamkill punishment (offline). Sets the two punishment CVars in memory (RVAs from re_notes/18_auth_and_punishment.md):
//   Punishment.Bans.Enabled (int, RVA 0x15CFD80) -> 0        Punishment.Threshold (float, RVA 0x15CFD84) -> 999999
// Both are not cheat-protected (flags 0xD0). The original values are put back when the option is turned off or the DLL unloads.
// Untested: the game may copy the CVars into its own struct at start-up, in which case the memory write alone has no effect.
namespace nopunish {
extern bool enabled;
void tick();      // once per frame
void restore();
}
