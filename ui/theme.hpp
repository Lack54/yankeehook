#pragma once
#include <imgui.h>

#include "fonts.hpp"

namespace yankeehook::gui::theme
{
	// accent is live: the Personalization swatch writes to it every frame
	inline ImVec4 accent = { 0.424f, 0.710f, 1.000f, 1.00f }; // #6CB5FF light blue

	// all sampled from the reference. the page behind the groups is the darkest, the groups sit lighter on it, and
	// each group's title strip is lighter again
	inline constexpr ImVec4 window_bg      = { 0.090f, 0.090f, 0.090f, 1.00f }; // #171717
	inline constexpr ImVec4 title_bg       = { 0.141f, 0.141f, 0.141f, 1.00f }; // #242424
	inline constexpr ImVec4 nav_bg         = { 0.110f, 0.110f, 0.110f, 1.00f }; // #1C1C1C
	inline constexpr ImVec4 group_bg       = { 0.110f, 0.110f, 0.110f, 1.00f }; // #1C1C1C
	inline constexpr ImVec4 group_header   = { 0.141f, 0.141f, 0.141f, 1.00f }; // #242424
	inline constexpr ImVec4 group_border   = { 0.118f, 0.118f, 0.118f, 1.00f }; // #1E1E1E

	// the rule under a group title is the accent at 61% over the title strip: #87686F on the default accent
	inline constexpr float  rule_mix       = 0.61f;
	inline constexpr ImVec4 check_bg       = { 0.090f, 0.090f, 0.090f, 1.00f }; // #171717, sampled: combo, slider track, sub tab bar

	// slider: the fill is the accent at 86%, which is #AD8189 on the default accent (sampled). knob and badge are neutral
	inline constexpr float  slider_fill_scale = 0.86f;
	inline constexpr ImVec4 slider_knob       = { 0.631f, 0.631f, 0.631f, 1.00f }; // #A1A1A1 sampled
	inline constexpr ImVec4 slider_knob_hover = { 0.816f, 0.816f, 0.816f, 1.00f }; // #D0D0D0 /* assumed */
	inline constexpr ImVec4 badge_bg          = { 0.137f, 0.137f, 0.137f, 1.00f }; // #232323 /* assumed */
	inline constexpr ImVec4 badge_text        = { 0.706f, 0.706f, 0.706f, 1.00f }; // #B4B4B4 /* assumed */
	inline constexpr ImVec4 combo_text        = { 0.604f, 0.604f, 0.604f, 1.00f }; // #9A9A9A /* assumed */
	inline constexpr ImVec4 text           = { 0.863f, 0.863f, 0.863f, 1.00f }; // #DCDCDC
	inline constexpr ImVec4 text_disabled  = { 0.420f, 0.420f, 0.420f, 1.00f }; // #6B6B6B

	// rounding, px
	inline constexpr float window_rounding = 8.0f;
	inline constexpr float group_rounding  = 6.0f;
	inline constexpr float frame_rounding  = 4.0f;
	inline constexpr float tab_rounding    = 6.0f;

	// text is drawn with a 1px soft drop shadow, see primitives.hpp
	inline constexpr ImU32 shadow_color = IM_COL32( 0, 0, 0, 150 );

	inline void load_fonts( )
	{
		auto& io = ImGui::GetIO( );

		ImFontConfig cfg;
		cfg.FontDataOwnedByAtlas = false;
		cfg.OversampleH          = 3;
		cfg.OversampleV          = 2;
		cfg.PixelSnapH           = false;

		io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
			const_cast< unsigned char* >( fonts::varela_round_data ),
			static_cast< int >( fonts::varela_round_size ), 14.0f, &cfg );
	}

	inline void apply( )
	{
		auto& style = ImGui::GetStyle( );

		style.WindowPadding    = { 0.0f, 0.0f };
		style.FramePadding     = { 8.0f, 4.0f };
		style.ItemSpacing      = { 8.0f, 6.0f };
		style.ItemInnerSpacing = { 6.0f, 4.0f };
		style.WindowRounding   = window_rounding;
		style.ChildRounding    = group_rounding;
		style.FrameRounding    = 8.0f;
		style.SelectableTextAlign = { 0.5f, 0.5f };
		style.PopupRounding    = group_rounding;
		style.GrabRounding     = 2.0f;
		style.GrabMinSize      = 10.0f;
		style.ScrollbarSize    = 6.0f;
		style.WindowBorderSize = 0.0f;
		style.ChildBorderSize  = 1.0f;
		style.FrameBorderSize  = 0.0f;
		style.PopupBorderSize  = 1.0f;

		auto* colors = style.Colors;
		colors[ImGuiCol_Text]             = text;
		colors[ImGuiCol_TextDisabled]     = text_disabled;
		colors[ImGuiCol_WindowBg]         = window_bg;
		colors[ImGuiCol_ChildBg]          = group_bg;
		colors[ImGuiCol_PopupBg]          = { 0.125f, 0.125f, 0.125f, 0.98f };
		colors[ImGuiCol_Border]           = group_border;
		colors[ImGuiCol_FrameBg]          = check_bg;
		colors[ImGuiCol_FrameBgHovered]   = { 0.165f, 0.165f, 0.165f, 1.00f };
		colors[ImGuiCol_FrameBgActive]    = { 0.200f, 0.200f, 0.200f, 1.00f };
		colors[ImGuiCol_SliderGrab]       = accent;
		colors[ImGuiCol_SliderGrabActive] = { 0.620f, 0.820f, 1.000f, 1.00f };
		colors[ImGuiCol_CheckMark]        = accent;
		colors[ImGuiCol_Button]           = { 0.160f, 0.160f, 0.160f, 1.00f };
		colors[ImGuiCol_ButtonHovered]    = { 0.200f, 0.200f, 0.200f, 1.00f };
		colors[ImGuiCol_ButtonActive]     = { 0.240f, 0.240f, 0.240f, 1.00f };
		colors[ImGuiCol_Header]           = { 0.200f, 0.200f, 0.200f, 1.00f };
		colors[ImGuiCol_HeaderHovered]    = { 0.240f, 0.240f, 0.240f, 1.00f };
		colors[ImGuiCol_HeaderActive]     = { 0.280f, 0.280f, 0.280f, 1.00f };
		colors[ImGuiCol_Separator]        = accent;
	}
} // namespace yankeehook::gui::theme
