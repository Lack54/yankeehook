#pragma once

#include <cmath>
#include <unordered_map>

#include <imgui.h>

namespace yankeehook::gui::anim
{
	// eases a value toward target, frame rate independent. state is keyed by ImGui ID,
	// so the same ID must be used every frame for the same widget
	inline float approach( ImGuiID id, float target, float speed = 12.0f )
	{
		static std::unordered_map<ImGuiID, float> states;

		auto it = states.find( id );

		if ( it == states.end( ) )
			it = states.emplace( id, target ).first;

		const float blend = 1.0f - std::exp( -speed * ImGui::GetIO( ).DeltaTime );
		it->second += ( target - it->second ) * blend;

		return it->second;
	}

	// jumps a value without easing, used to restart an animation
	inline void set( ImGuiID id, float value )
	{
		static std::unordered_map<ImGuiID, float> states;
		states[ id ] = value;
	}
} // namespace yankeehook::gui::anim
