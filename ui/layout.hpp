#pragma once

#include <imgui.h>

namespace yankeehook::gui::layout
{
	// window geometry, px at 100% DPI, estimated from the reference screenshots
	inline constexpr ImVec2 k_window     = { 830.0f, 600.0f };
	inline constexpr float  k_title_h    = 52.0f;
	inline constexpr float  k_nav_w      = 165.0f;
	inline constexpr float  k_nav_top    = 66.0f;
	inline constexpr float  k_nav_item_h = 48.0f;

	// bottom tab bar: rule, then the tabs with room for the icon and the label under it
	inline constexpr float  k_bottom_rule = 532.0f;
	inline constexpr float  k_bottom_top  = 540.0f;
	inline constexpr float  k_bottom_h    = 52.0f;
	inline constexpr float  k_tab_w       = 100.0f;
	inline constexpr float  k_tab_step    = 110.0f;
	inline constexpr float  k_tab_center  = 415.0f;

	// content area, to the right of the nav
	inline constexpr float  k_content_x   = 180.0f;
	inline constexpr float  k_content_y   = 67.0f;
	inline constexpr float  k_column_w    = 310.0f;
	inline constexpr float  k_gap         = 10.0f;
} // namespace yankeehook::gui::layout
