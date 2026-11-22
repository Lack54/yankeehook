#pragma once

namespace hook {
bool install();     // hooks IDXGISwapChain::Present / ResizeBuffers (DX11)
void uninstall();   // restores hooks, shuts ImGui down
}
