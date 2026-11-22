#pragma once

#include <imgui.h>

namespace yankeehook::gui::ui
{
	// the menu is designed at 100%. every custom size and position goes through S(), so the whole menu scales as one
	inline float g_scale = 1.0f;

	// font size of the custom text, at 100%
	inline constexpr float k_font_px = 14.0f;

	inline float S( float v )
	{
		return v * g_scale;
	}

	inline ImVec2 S( const ImVec2& v )
	{
		return ImVec2( v.x * g_scale, v.y * g_scale );
	}
} // namespace yankeehook::gui::ui
