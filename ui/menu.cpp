#include "menu.hpp"

#include <algorithm>
#include <cmath>

#include "animation.hpp"
#include "layout.hpp"
#include "logo.hpp"
#include "primitives.hpp"
#include "scale.hpp"
#include "theme.hpp"

namespace yankeehook::gui
{
	void c_menu::add_page( page_t page )
	{
		m_pages.push_back( std::move( page ) );
		m_nav.push_back( 0 );
	}

	void c_menu::on_render( )
	{
		// the menu waits for the startup animation to finish, so the two never overlap, and then fades in
		static bool s_was_drawn = false;

		if ( !m_draw )
		{
			s_was_drawn = false;
			return;
		}

		const ImGuiID open_id = ImGui::GetID( "yankeehook_open_fade" );

		if ( !s_was_drawn )
		{
			anim::set( open_id, 0.0f );
			s_was_drawn = true;
		}

		const float open_alpha = anim::approach( open_id, 1.0f, 7.0f );

		const auto& io = ImGui::GetIO( );
		const ImVec2 size = ui::S( layout::k_window );

		// first frame: centre. after that the title bar drags m_pos
		if ( m_pos.x < 0.0f )
			m_pos = ImVec2( ( io.DisplaySize.x - size.x ) * 0.5f, ( io.DisplaySize.y - size.y ) * 0.5f );

		ImGui::SetNextWindowPos( m_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( size, ImGuiCond_Always );

		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, open_alpha );

		ImGui::Begin( "##yankeehook", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoScrollWithMouse );

		draw_title_bar( );
		draw_nav( );
		draw_page( );
		draw_bottom_bar( );

		ImGui::End( );

		ImGui::PopStyleVar( );
	}

	void c_menu::draw_title_bar( )
	{
		const auto& io   = ImGui::GetIO( );
		const ImVec2 win = ui::S( layout::k_window );
		const float title_h = ui::S( layout::k_title_h );

		// drag handle: the whole title bar moves the window
		ImGui::SetCursorPos( ImVec2( 0.0f, 0.0f ) );
		ImGui::InvisibleButton( "##drag", ImVec2( win.x, title_h ) );

		if ( ImGui::IsItemActive( ) )
		{
			m_pos.x += io.MouseDelta.x;
			m_pos.y += io.MouseDelta.y;

			// keep the window on screen
			m_pos.x = ImClamp( m_pos.x, 0.0f, io.DisplaySize.x - win.x );
			m_pos.y = ImClamp( m_pos.y, 0.0f, io.DisplaySize.y - win.y );
		}

		auto* dl       = ImGui::GetWindowDrawList( );
		const ImVec2 p = ImGui::GetWindowPos( );

		// the title band, rounded on the top corners to follow the window
		dl->AddRectFilled( p, ImVec2( p.x + win.x, p.y + title_h ), ui::col( theme::title_bg ), ui::S( theme::window_rounding ), ImDrawFlags_RoundCornersTop );

		// the mark on the left, the wordmark beside it. hovering either plays one animation: the mark grows and a halo opens
		// behind it, a light runs once around its outline, then leaves it and crosses the name, warming each letter as it passes
		const float  logo_size = ui::S( 34.0f );
		const ImVec2 logo_min( p.x + ui::S( 18.0f ), p.y + ui::S( 9.0f ) );
		const ImVec2 logo_max( logo_min.x + logo_size, logo_min.y + logo_size );
		const ImVec2 logo_mid( ( logo_min.x + logo_max.x ) * 0.5f, ( logo_min.y + logo_max.y ) * 0.5f );

		// the name is drawn a letter at a time, so each letter can light up on its own
		const char* k_name    = m_name.c_str( );
		const int         k_letters = (std::min)( static_cast<int>( m_name.size( ) ), 15 );

		ImFont*      name_font = ImGui::GetFont( );
		const float  name_size = ui::S( 26.0f );
		const ImVec2 name_pos( p.x + ui::S( 62.0f ), p.y + ui::S( 12.0f ) );

		float letter_x[ 16 ];
		float name_x = name_pos.x;

		for ( int i = 0; i < k_letters; ++i )
		{
			letter_x[ i ] = name_x;
			name_x       += name_font->CalcTextSizeA( name_size, FLT_MAX, 0.0f, k_name + i, k_name + i + 1 ).x;
		}

		letter_x[ k_letters ] = name_x;

		const float name_h = name_font->CalcTextSizeA( name_size, FLT_MAX, 0.0f, "Ag" ).y;

		const float pad     = ui::S( 6.0f );
		const bool  hovered = ImGui::IsWindowHovered( ) &&
			( ImGui::IsMouseHoveringRect( ImVec2( logo_min.x - pad, logo_min.y - pad ), ImVec2( logo_max.x + pad, logo_max.y + pad ) ) ||
			  ImGui::IsMouseHoveringRect( ImVec2( name_pos.x - pad, name_pos.y - pad ), ImVec2( name_x + pad, name_pos.y + name_h + pad ) ) );

		// the one animation: 0 to 1 over the whole run. the light is on the outline for the first half and on the name after.
		// hovering again while it runs does not restart it
		// it also runs by itself, again and again: a few seconds of rest, then one run, so the mark is never dead. hovering starts a run at once
		constexpr float k_sweep_seconds = 2.2f;
		constexpr float k_logo_end      = 0.50f;
		constexpr float k_idle_gap      = 3.2f;

		static bool  was_hovered = false;
		static float sweep       = -1.0f;   // below 0 when it is not running
		static float idle_for    = 0.0f;

		if ( sweep < 0.0f )
		{
			idle_for += io.DeltaTime;

			if ( ( hovered && !was_hovered ) || idle_for >= k_idle_gap )
			{
				sweep    = 0.0f;
				idle_for = 0.0f;
			}
		}

		was_hovered = hovered;

		if ( sweep >= 0.0f )
		{
			sweep += io.DeltaTime / k_sweep_seconds;

			if ( sweep > 1.1f )
				sweep = -1.0f;
		}

		const float hover = anim::approach( ImGui::GetID( "logo_hover" ), hovered ? 1.0f : 0.0f, 11.0f );

		// how far through its run the light is, as a 0 to 1 swell that is 1 in the middle of the run
		const float run = sweep >= 0.0f ? std::sin( ImClamp( sweep / 1.1f, 0.0f, 1.0f ) * 3.14159f ) : 0.0f;

		// the halo breathes all the time, and opens wider while the light runs or the pointer is over the mark
		const float breath = 0.5f + 0.5f * std::sin( static_cast<float>( ImGui::GetTime( ) ) * 1.7f );
		const float glow   = 0.30f + 0.30f * breath + 1.0f * ImMax( hover, run );

		for ( int i = 5; i >= 1; --i )
			dl->AddCircleFilled( logo_mid, logo_size * 0.5f + ui::S( static_cast<float>( i ) * 2.6f ), ui::col( theme::accent, 0.016f * glow ), 32 );

		// the mark swells a little with the light and under the pointer, and warms towards white
		const float  grown = logo_size * ( 1.0f + 0.05f * hover + 0.04f * run );
		const ImVec2 grown_origin( logo_mid.x - grown * 0.5f, logo_mid.y - grown * 0.5f );

		const float warmth = ImMax( hover * 0.30f, run * 0.40f );

		const ImVec4 lit( ImLerp( theme::accent.x, 1.0f, warmth ), ImLerp( theme::accent.y, 1.0f, warmth ),
			ImLerp( theme::accent.z, 1.0f, warmth ), 1.0f );

		// the light moves with an ease, slow at the ends of each leg, so it reads as a glide
		const auto smooth = [ ]( float v )
			{
				v = ImClamp( v, 0.0f, 1.0f );
				return v * v * ( 3.0f - 2.0f * v );
			};

		const float logo_k = smooth( sweep / k_logo_end );
		const float text_k = smooth( ( sweep - k_logo_end ) / ( 1.0f - k_logo_end ) );

		logo::style_t style;
		style.highlight       = ( sweep >= 0.0f && sweep < k_logo_end + 0.05f ) ? logo_k * 1.2f - 0.2f : -1.0f;
		style.highlight_color = ui::col( ImVec4( 0.88f, 0.95f, 1.0f, 1.0f ) );

		logo::draw( dl, grown_origin, grown, ui::col( lit ), style );

		// where the light is right now: on the outline, then on a line through the middle of the name
		bool  dot_on = false;
		ImVec2 dot( 0.0f, 0.0f );

		if ( sweep >= 0.0f )
		{
			dot_on = true;

			if ( sweep < k_logo_end )
				dot = logo::point_at( grown_origin, grown, logo_k );
			else
				dot = ImVec2( ImLerp( logo_max.x + pad * 0.5f, name_x + pad, text_k ), name_pos.y + name_h * 0.55f );
		}

		// the name, a letter at a time. a letter warms as the light comes near and keeps a little of it after the light has passed
		const float  warm_k = ImMax( hover * 0.22f, run * 0.12f );
		const ImVec4 warm( ImLerp( theme::accent.x, 1.0f, warm_k ), ImLerp( theme::accent.y, 1.0f, warm_k ),
			ImLerp( theme::accent.z, 1.0f, warm_k ), 1.0f );
		const ImVec4 flash( 0.88f, 0.95f, 1.0f, 1.0f );

		for ( int i = 0; i < k_letters; ++i )
		{
			float boost = 0.0f;

			if ( dot_on && sweep >= k_logo_end )
			{
				const float centre = ( letter_x[ i ] + letter_x[ i + 1 ] ) * 0.5f;
				const float away   = std::fabs( centre - dot.x );

				boost = ImClamp( 1.0f - away / ui::S( 46.0f ), 0.0f, 1.0f );

				if ( dot.x > centre )
					boost = ImMax( boost, 0.35f * ( 1.0f - ImClamp( ( dot.x - centre ) / ui::S( 120.0f ), 0.0f, 1.0f ) ) );
			}

			const ImVec4 color( ImLerp( warm.x, flash.x, boost ), ImLerp( warm.y, flash.y, boost ), ImLerp( warm.z, flash.z, boost ), 1.0f );

			dl->AddText( name_font, name_size, ImVec2( letter_x[ i ], name_pos.y - boost * ui::S( 1.5f ) ), ui::col( color ), k_name + i, k_name + i + 1 );
		}

		// the light itself, drawn last so it sits on top of both. it fades out at the very end of the run
		if ( dot_on )
		{
			// it fades in at the start of the run and out at the end
			const float fade = ImClamp( sweep / 0.06f, 0.0f, 1.0f ) * ( 1.0f - ImClamp( ( sweep - 1.0f ) / 0.1f, 0.0f, 1.0f ) );

			for ( int i = 3; i >= 1; --i )
				dl->AddCircleFilled( dot, ui::S( 2.2f + static_cast<float>( i ) * 2.2f ), ui::col( theme::accent, 0.10f * fade ), 20 );

			// a thin four point glint through the light
			const float arm = ui::S( 6.5f );
			dl->AddLine( ImVec2( dot.x - arm, dot.y ), ImVec2( dot.x + arm, dot.y ), ui::col( flash, 0.75f * fade ), ui::S( 1.0f ) );
			dl->AddLine( ImVec2( dot.x, dot.y - arm ), ImVec2( dot.x, dot.y + arm ), ui::col( flash, 0.75f * fade ), ui::S( 1.0f ) );

			dl->AddCircleFilled( dot, ui::S( 2.3f ), ui::col( flash, fade ), 16 );
		}
		dl->AddLine( ImVec2( p.x, p.y + title_h ), ImVec2( p.x + win.x, p.y + title_h ), ui::col( ui::mix( theme::title_bg, theme::accent, theme::rule_mix ) ) );
	}

	void c_menu::draw_nav( )
	{
		if ( m_pages.empty( ) || m_pages[ m_page ].nav.empty( ) )
			return;

		const auto& items = m_pages[ m_page ].nav;
		const int   count = static_cast<int>( items.size( ) );
		int&        selected = m_nav[ m_page ];

		const int active_index = selected;

		const float nav_w      = ui::S( layout::k_nav_w );
		const float nav_top    = ui::S( layout::k_nav_top );
		const float nav_item_h = ui::S( layout::k_nav_item_h );
		const float rule_y     = ui::S( layout::k_bottom_rule );
		const float title_h    = ui::S( layout::k_title_h );

		auto* dl       = ImGui::GetWindowDrawList( );
		const ImVec2 p = ImGui::GetWindowPos( );

		dl->AddRectFilled( ImVec2( p.x, p.y + title_h + 1.0f ), ImVec2( p.x + nav_w, p.y + rule_y ), ui::col( theme::nav_bg ) );
		dl->AddLine( ImVec2( p.x + nav_w, p.y + title_h ), ImVec2( p.x + nav_w, p.y + rule_y ), ui::col( theme::group_border ) );

		// the highlight and accent bar slide to the selected item
		const float target_y = nav_top + active_index * nav_item_h;
		const float bar_y    = anim::approach( ImGui::GetID( "nav_bar" ), target_y, 14.0f );

		// the active item: a flat #212121 tile with a 3px accent bar on its left edge, no glow (sampled from the reference)
		dl->AddRectFilled( ImVec2( p.x, p.y + bar_y ), ImVec2( p.x + nav_w, p.y + bar_y + nav_item_h ), IM_COL32( 33, 33, 33, 255 ) );
		dl->AddRectFilled( ImVec2( p.x, p.y + bar_y ), ImVec2( p.x + ui::S( 3.0f ), p.y + bar_y + nav_item_h ), ui::col( theme::accent ) );

		for ( int i = 0; i < count; ++i )
		{
			const float y     = nav_top + i * nav_item_h;
			const bool active = i == active_index;

			ImGui::SetCursorPos( ImVec2( 0.0f, y ) );
			ImGui::PushID( i );

			if ( ImGui::InvisibleButton( "##nav", ImVec2( nav_w, nav_item_h ) ) )
				selected = i;

			ImGui::PopID( );

			ui::bold_text( dl, ImVec2( p.x + ui::S( 10.0f ), p.y + y + ui::S( 8.0f ) ), ui::col( active ? theme::text : theme::text_disabled ), items[ i ].title.c_str( ) );
			ui::shadow_text( dl, ImVec2( p.x + ui::S( 10.0f ), p.y + y + ui::S( 25.0f ) ), ui::col( theme::text_disabled ), items[ i ].sub.c_str( ) );
		}
	}

	void c_menu::draw_page( )
	{
		if ( m_pages.empty( ) )
			return;

		// content fades in every time the page changes
		const ImGuiID fade_id = ImGui::GetID( "page_fade" );

		if ( m_page != m_last_page )
		{
			anim::set( fade_id, 0.0f );
			m_last_page = m_page;
		}

		const float alpha = anim::approach( fade_id, 1.0f, 9.0f );

		// multiplied with the alpha the menu is already at, so the page fade works inside the menu's own fade in
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, alpha * ImGui::GetStyle( ).Alpha );

		if ( m_pages[ m_page ].draw )
			m_pages[ m_page ].draw( m_nav[ m_page ] );

		ImGui::PopStyleVar( );
	}

	void c_menu::draw_bottom_bar( )
	{
		if ( m_pages.empty( ) )
			return;

		const int tab_count = static_cast<int>( m_pages.size( ) );

		auto* dl       = ImGui::GetWindowDrawList( );
		const ImVec2 p = ImGui::GetWindowPos( );

		const float win_w     = ui::S( layout::k_window ).x;
		const float rule_y    = ui::S( layout::k_bottom_rule );
		const float bottom_y  = ui::S( layout::k_bottom_top );
		const float bottom_h  = ui::S( layout::k_bottom_h );
		const float tab_w     = ui::S( layout::k_tab_w );
		const float tab_step  = ui::S( layout::k_tab_step );
		const float tab_mid   = ui::S( layout::k_tab_center );

		// the bottom band, rounded on the bottom corners, with the muted rule on top of it
		dl->AddRectFilled( ImVec2( p.x, p.y + rule_y ), ImVec2( p.x + win_w, p.y + ui::S( layout::k_window ).y ), ui::col( theme::title_bg ), ui::S( theme::window_rounding ), ImDrawFlags_RoundCornersBottom );
		dl->AddLine( ImVec2( p.x, p.y + rule_y ), ImVec2( p.x + win_w, p.y + rule_y ), ui::col( ui::mix( theme::title_bg, theme::accent, theme::rule_mix ) ) );

		// the highlight slides between tabs instead of jumping. the middle tab is the centre of the bar
		const float active_cx = tab_mid + ( m_page - ( tab_count - 1 ) * 0.5f ) * tab_step;
		const float cx        = anim::approach( ImGui::GetID( "tab_highlight" ), active_cx, 16.0f );

		const ImVec2 hmin( p.x + cx - tab_w * 0.5f, p.y + bottom_y );
		const ImVec2 hmax( p.x + cx + tab_w * 0.5f, p.y + bottom_y + bottom_h );

		// a flat tile, no glow: #2D2D2D sampled from the reference
		dl->AddRectFilled( hmin, hmax, IM_COL32( 45, 45, 45, 255 ), ui::S( theme::tab_rounding ) );

		for ( int i = 0; i < tab_count; ++i )
		{
			const float tab_cx = tab_mid + ( i - ( tab_count - 1 ) * 0.5f ) * tab_step;

			ImGui::SetCursorPos( ImVec2( tab_cx - tab_w * 0.5f, bottom_y ) );
			ImGui::PushID( i );

			if ( ImGui::InvisibleButton( "##tab", ImVec2( tab_w, bottom_h ) ) )
				m_page = i;

			const float active = anim::approach( ImGui::GetID( "active" ), m_page == i ? 1.0f : 0.0f, 14.0f );

			ImGui::PopID( );

			// the active tab is white, the others dim. the accent stays on the rules and the sliders
			const ImU32 color = ui::col( ui::mix( theme::text_disabled, theme::text, active ) );

			ui::icon( dl, m_pages[ i ].icon, ImVec2( p.x + tab_cx, p.y + bottom_y + ui::S( 16.0f ) ), ui::S( 16.0f ), color,
				IM_COL32( 28, 28, 28, 255 ) );

			const char* name = m_pages[ i ].name.c_str( );
			const ImVec2 ts  = ImGui::CalcTextSize( name );
			ui::shadow_text( dl, ImVec2( p.x + tab_cx - ts.x * 0.5f, p.y + bottom_y + ui::S( 34.0f ) ), color, name );
		}
	}
} // namespace yankeehook::gui
