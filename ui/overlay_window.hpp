#pragma once

#include <cfloat>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "animation.hpp"
#include "primitives.hpp"
#include "theme.hpp"

namespace yankeehook::gui::hud
{
	// one row of an overlay list: the name on the left, the value on the right. a row that is not active is drawn dim
	struct overlay_row_t
	{
		std::string name;
		std::string value;
		bool        active = true;
	};

	// a small draggable list window: bold title, a rule, then the rows. it fades in while there are rows and out when there are none, and keeps the
	// last rows while it fades. title and id name the window, so two lists need two ids. first_pos is where it first appears, in screen pixels.
	// pivot is the window's own anchor, (1, 0) pins its top right corner to first_pos. call every frame, between NewFrame and Render
	inline void draw_overlay_list( const char* id, const char* title, const std::vector<overlay_row_t>& rows, bool enabled, ImVec2 first_pos, ImVec2 pivot = ImVec2( 0.0f, 0.0f ), float width = 190.0f )
	{
		static std::unordered_map<ImGuiID, std::vector<overlay_row_t>> last_rows;

		const ImGuiID key = ImHashStr( id );

		if ( !rows.empty( ) )
			last_rows[ key ] = rows;

		const auto& shown = last_rows[ key ];

		// one alpha drives the background, the text and the shadows, so everything fades together
		const float alpha = anim::approach( key, enabled && !rows.empty( ) ? 1.0f : 0.0f, 9.0f );

		if ( alpha < 0.01f || shown.empty( ) )
			return;

		ImGui::SetNextWindowPos( first_pos, ImGuiCond_FirstUseEver, pivot );
		ImGui::SetNextWindowSizeConstraints( ImVec2( ui::S( width ), 0.0f ), ImVec2( ui::S( width ), FLT_MAX ) );

		ui::style_scope style;
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, alpha );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.0f, 8.0f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.0f, 4.0f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, theme::group_rounding );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.0f );
		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.09f, 0.09f, 0.09f, 0.92f ) );
		ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( theme::group_border.x, theme::group_border.y, theme::group_border.z, 0.6f ) );

		// no title bar, so the body drags the window
		const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		const std::string window = std::string( "##" ) + id;

		if ( ImGui::Begin( window.c_str( ), nullptr, flags ) )
		{
			auto* dl = ImGui::GetWindowDrawList( );

			// everything is placed from two edges, the left and the right of the content, snapped to whole pixels, so the title, the rule and
			// every row start at the same x, and every value ends at the same x as the rule
			const ImVec2 origin = ImGui::GetCursorScreenPos( );
			const float  left   = ImFloor( origin.x );
			const float  right  = ImFloor( origin.x + ImGui::GetContentRegionAvail( ).x );
			const float  line_h = ImGui::GetTextLineHeight( );

			ui::bold_text( dl, ImVec2( left, ImFloor( origin.y ) ), ui::col( theme::text, alpha ), title, alpha );
			ImGui::Dummy( ImVec2( right - left, line_h ) );

			ImGui::Separator( );

			for ( const auto& row : shown )
			{
				const float y = ImFloor( ImGui::GetCursorScreenPos( ).y );

				ui::shadow_text( dl, ImVec2( left, y ), ui::col( row.active ? theme::text : theme::text_disabled, alpha ), row.name.c_str( ), alpha );

				const ImVec2 size = ui::text_size( row.value.c_str( ) );
				ui::shadow_text( dl, ImVec2( ImFloor( right - size.x ), y ), ui::col( theme::accent, alpha ), row.value.c_str( ), alpha );

				ImGui::Dummy( ImVec2( right - left, line_h ) );
			}
		}

		ImGui::End( );
	}
} // namespace yankeehook::gui::hud
