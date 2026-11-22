#pragma once

#include <imgui.h>

namespace yankeehook::utility::color
{
	// linear blend of two colours, t = 0 gives a, t = 1 gives b
	inline ImVec4 mix( const ImVec4& a, const ImVec4& b, float t )
	{
		return ImVec4( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t, a.w + ( b.w - a.w ) * t );
	}

	// colour with its alpha multiplied by a
	inline ImU32 with_alpha( const ImVec4& c, float a )
	{
		return ImGui::ColorConvertFloat4ToU32( ImVec4( c.x, c.y, c.z, c.w * a ) );
	}
} // namespace yankeehook::utility::color
