#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "animation.hpp"
#include "scale.hpp"
#include "theme.hpp"
#include "color.hpp"

// every size in this file is a design pixel at 100%, and goes through S() so the whole menu scales together

namespace yankeehook::gui::ui
{
	inline constexpr float k_panel_w = 240.0f;

	inline ImU32 col( const ImVec4& c, float alpha = 1.0f )
	{
		return ImGui::GetColorU32( ImVec4( c.x, c.y, c.z, c.w * alpha ) );
	}

	inline ImU32 text_col( )
	{
		return ImGui::GetColorU32( ImGuiCol_Text );
	}

	inline ImVec4 mix( const ImVec4& a, const ImVec4& b, float t )
	{
		return utility::color::mix( a, b, t );
	}

	// the custom text size, scaled. built-in widgets follow io.FontGlobalScale instead
	inline ImVec2 text_size( const char* text )
	{
		return ImGui::GetFont( )->CalcTextSizeA( S( k_font_px ), FLT_MAX, 0.0f, text );
	}

	inline void draw_text( ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, const char* end )
	{
		dl->AddText( ImGui::GetFont( ), S( k_font_px ), pos, color, text, end );
	}

	// the shadow colour with its alpha scaled, so a fading text fades its shadow with it
	inline ImU32 shadow_color_alpha( float alpha )
	{
		return IM_COL32( 0, 0, 0, static_cast<int>( 150.0f * alpha ) );
	}

	// text with a soft shadow. alpha scales the shadow too, so fades go out together
	inline void shadow_text( ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, float alpha = 1.0f )
	{
		const char* end = ImGui::FindRenderedTextEnd( text );
		draw_text( dl, ImVec2( pos.x + S( 1.0f ), pos.y + S( 1.0f ) ), shadow_color_alpha( alpha ), text, end );
		draw_text( dl, pos, color, text, end );
	}

	// the font has one weight, so bold is a second pass a little to the right
	inline void bold_text( ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, float alpha = 1.0f )
	{
		const char* end = ImGui::FindRenderedTextEnd( text );
		draw_text( dl, ImVec2( pos.x + S( 1.0f ), pos.y + S( 1.0f ) ), shadow_color_alpha( alpha ), text, end );
		draw_text( dl, pos, color, text, end );
		draw_text( dl, ImVec2( pos.x + S( 0.6f ), pos.y ), color, text, end );
	}

	// text that advances the cursor, with the shadow
	inline void text_label( const char* text )
	{
		const ImVec2 p  = ImGui::GetCursorScreenPos( );
		const ImVec2 ts = text_size( text );

		shadow_text( ImGui::GetWindowDrawList( ), p, text_col( ), text );
		ImGui::Dummy( ts );
	}

	// flush right, reserving w design pixels on the current line
	inline void right_align( float w )
	{
		ImGui::SameLine( );
		ImGui::SetCursorPosX( ImGui::GetCursorPosX( ) + ImGui::GetContentRegionAvail( ).x - S( w ) );
	}

	// a settings row: the label sits centred on the row, and the control is laid out in the right-hand slot
	// (control_w design pixels wide). the control gets its slot's top left and the row height, and the row
	// always advances by row_h, so labels and controls share one vertical centre
	template <typename control_t>
	inline void row( const char* label, float row_h, float control_w, control_t&& control )
	{
		const float h      = S( row_h );
		const float cw     = S( control_w );
		const ImVec2 start = ImGui::GetCursorScreenPos( );
		const float avail  = ImGui::GetContentRegionAvail( ).x;

		const float text_h = text_size( "Ag" ).y;
		shadow_text( ImGui::GetWindowDrawList( ), ImVec2( start.x, start.y + ( h - text_h ) * 0.5f ), text_col( ), label );

		control( ImVec2( start.x + avail - cw, start.y ), h );

		// reserve the whole row, so the window sizes to it and the next row starts below
		ImGui::SetCursorScreenPos( start );
		ImGui::Dummy( ImVec2( avail, h ) );
	}

	// glow halo around a rect, drawn before the rect itself
	inline void glow( ImDrawList* dl, ImVec2 min, ImVec2 max, const ImVec4& color, float rounding, float strength = 0.45f )
	{
		const int layers = 4;

		for ( int i = layers; i >= 1; --i )
		{
			const float grow = S( static_cast<float>( i ) * 1.6f );
			dl->AddRectFilled( ImVec2( min.x - grow, min.y - grow ), ImVec2( max.x + grow, max.y + grow ),
				col( color, strength / layers ), S( rounding ) + grow );
		}
	}

	// soft dark shadow under a folder, drawn in the parent window before the child
	inline void drop_shadow( ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding )
	{
		for ( int i = 1; i <= 6; ++i )
		{
			const float g = S( static_cast<float>( i ) * 1.5f );
			dl->AddRectFilled( ImVec2( min.x - g, min.y - g + S( 3.0f ) ), ImVec2( max.x + g, max.y + g + S( 3.0f ) ),
				col( ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ), 0.06f ), S( rounding ) + g );
		}
	}

	// folder: bordered child with a bold title and an accent rule under the header.
	// it never scrolls, so wheel input over it does nothing. pos and size are design pixels
	// the title of the group that is open, so it can be drawn again over rows that have scrolled up under it
	inline const char* g_group_title = nullptr;

	inline void group_begin( const char* id, const char* title, ImVec2 pos, ImVec2 size )
	{
		const ImVec2 sp = S( pos );
		const ImVec2 ss = S( size );
		const ImVec2 wp = ImGui::GetWindowPos( );

		drop_shadow( ImGui::GetWindowDrawList( ), ImVec2( wp.x + sp.x, wp.y + sp.y ),
			ImVec2( wp.x + sp.x + ss.x, wp.y + sp.y + ss.y ), theme::group_rounding );

		ImGui::SetCursorPos( sp );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( S( 10.0f ), S( 34.0f ) ) );
		ImGui::BeginChild( id, ss, ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar );

		const ImVec2 p = ImGui::GetWindowPos( );
		auto* dl       = ImGui::GetWindowDrawList( );

		// the title strip: lighter than the body, rounded on the top corners only, with the muted accent rule under it
		dl->AddRectFilled( p, ImVec2( p.x + ss.x, p.y + S( 26.0f ) ), col( theme::group_header ), S( theme::group_rounding ), ImDrawFlags_RoundCornersTop );

		g_group_title = title;
		bold_text( dl, ImVec2( p.x + S( 10.0f ), p.y + S( 8.0f ) ), text_col( ), title );
		dl->AddLine( ImVec2( p.x + 1.0f, p.y + S( 26.0f ) ), ImVec2( p.x + ss.x - 1.0f, p.y + S( 26.0f ) ),
			col( mix( theme::group_header, theme::accent, theme::rule_mix ) ) );
	}

	inline void group_end( )
	{
		// a group scrolls with the wheel only when its rows go past its bottom edge. the window's own padding counts at the bottom too, which
		// would give every group a little range to scroll, so the range is taken from where the rows really end
		const float height   = ImGui::GetWindowHeight( );
		const float overflow = ImGui::GetCursorPosY( ) + S( 6.0f ) - height;
		const float limit    = overflow > 0.0f ? overflow : 0.0f;

		if ( ImGui::GetScrollY( ) > limit )
			ImGui::SetScrollY( limit );

		// a thin bar at the right edge shows there is more, and where the view is
		if ( limit > 0.0f )
		{
			auto*        dl    = ImGui::GetWindowDrawList( );
			const ImVec2 p     = ImGui::GetWindowPos( );
			const float  width = ImGui::GetWindowWidth( );

			// the rows scroll under the title strip, so the strip is drawn again over them
			if ( ImGui::GetScrollY( ) > 0.0f && g_group_title )
			{
				dl->AddRectFilled( p, ImVec2( p.x + width, p.y + S( 26.0f ) ), col( theme::group_header ), S( theme::group_rounding ), ImDrawFlags_RoundCornersTop );
				bold_text( dl, ImVec2( p.x + S( 10.0f ), p.y + S( 8.0f ) ), text_col( ), g_group_title );
				dl->AddLine( ImVec2( p.x + 1.0f, p.y + S( 26.0f ) ), ImVec2( p.x + width - 1.0f, p.y + S( 26.0f ) ), col( mix( theme::group_header, theme::accent, theme::rule_mix ) ) );
			}
			const float  track = height - S( 40.0f );
			const float  thumb = (std::max)( S( 18.0f ), track * height / ( height + limit ) );
			const float  top   = p.y + S( 32.0f ) + ( track - thumb ) * ( ImGui::GetScrollY( ) / limit );

			dl->AddRectFilled( ImVec2( p.x + width - S( 5.0f ), top ), ImVec2( p.x + width - S( 2.0f ), top + thumb ), col( theme::accent, 0.55f ), S( 2.0f ) );
		}

		ImGui::EndChild( );
		ImGui::PopStyleVar( );
	}

	// open state of a sub window. generation changes on every opening, so each opening is a new window
	// and it starts from its default position instead of remembering where it was dragged last time
	struct panel_t
	{
		bool open       = false;
		int  generation = 0;
	};

	// pops exactly the style variables and colours pushed since it was created, when it goes out of scope.
	// a push and its pop can then never fall out of step, and a stray push cannot leak into the next window
	class style_scope
	{
	public:

		style_scope( )
			: m_vars( GImGui->StyleVarStack.Size ), m_colors( GImGui->ColorStack.Size )
		{
		}

		~style_scope( )
		{
			const int vars   = GImGui->StyleVarStack.Size - m_vars;
			const int colors = GImGui->ColorStack.Size - m_colors;

			if ( vars > 0 )
				ImGui::PopStyleVar( vars );

			if ( colors > 0 )
				ImGui::PopStyleColor( colors );
		}

		style_scope( const style_scope& ) = delete;
		style_scope& operator=( const style_scope& ) = delete;

	private:

		int m_vars;
		int m_colors;
	};

	inline panel_t& panel( const std::string& key )
	{
		static std::unordered_map<std::string, panel_t> states;
		return states[ key ];
	}

	inline void panel_open_now( const std::string& key )
	{
		auto& p = panel( key );
		p.open  = true;
		++p.generation;
	}

	inline void panel_toggle( const std::string& key )
	{
		auto& p = panel( key );

		if ( p.open )
		{
			p.open = false;
			return;
		}

		p.open = true;
		++p.generation;
	}

	// where a sub window opens: beside the window it came from, at the height given. it goes to the right when that fits on
	// the screen, and to the left when it does not. so it never sits on top of the window it was opened from
	inline ImVec2 beside_window( float y )
	{
		const ImVec2 wp      = ImGui::GetWindowPos( );
		const ImVec2 ws      = ImGui::GetWindowSize( );
		const ImVec2 display = ImGui::GetIO( ).DisplaySize;
		const float  gap     = S( 8.0f );
		const float  width   = S( k_panel_w );

		const float right_x = wp.x + ws.x + gap;

		if ( right_x + width <= display.x )
			return ImVec2( right_x, y );

		return ImVec2( wp.x - gap - width, y );
	}

	// a draggable settings window. the header is ours: the bold title on the left, the close X on the right, and a rule
	// under it. dragging the header moves the window. first_pos is where it first appears (screen pixels)
	template <typename body_t>
	inline void sub_window( const std::string& key, const char* title, ImVec2 first_pos, body_t&& body )
	{
		auto& p = panel( key );

		if ( !p.open )
			return;

		ImGui::SetNextWindowPos( first_pos, ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSizeConstraints( ImVec2( S( k_panel_w ), 0.0f ), ImVec2( S( k_panel_w ), FLT_MAX ) );

		style_scope style;
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, S( ImVec2( 12.0f, 10.0f ) ) );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, S( ImVec2( 8.0f, 6.0f ) ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, S( theme::group_rounding ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.0f );

		// the same surfaces as a group: its body colour, with a border a step lighter than the group's so it lifts off the page
		ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.165f, 0.165f, 0.165f, 1.00f ) );
		ImGui::PushStyleColor( ImGuiCol_WindowBg, theme::group_bg );

		// the generation is part of the name, so a reopened window is a new window
		const std::string name = std::string( title ) + "###" + key + "#" + std::to_string( p.generation );
		const auto flags       = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;

		if ( ImGui::Begin( name.c_str( ), &p.open, flags ) )
		{
			auto* dl             = ImGui::GetWindowDrawList( );
			const float close    = S( 16.0f );
			const float header_h = S( 24.0f );
			const ImVec2 top     = ImGui::GetCursorScreenPos( );
			const float width    = ImGui::GetContentRegionAvail( ).x;

			// the drag handle is the header, left of the close button
			ImGui::InvisibleButton( "##header", ImVec2( width - close - S( 6.0f ), header_h ) );

			if ( ImGui::IsItemActive( ) )
			{
				const ImVec2 pos = ImGui::GetWindowPos( );
				const ImVec2 d   = ImGui::GetIO( ).MouseDelta;
				ImGui::SetWindowPos( ImVec2( pos.x + d.x, pos.y + d.y ) );
			}

			bold_text( dl, ImVec2( top.x, top.y + S( 4.0f ) ), text_col( ), title );

			// the close X, top right
			ImGui::SetCursorScreenPos( ImVec2( top.x + width - close, top.y + ( header_h - close ) * 0.5f ) );

			if ( ImGui::InvisibleButton( "##close", ImVec2( close, close ) ) )
				p.open = false;

			const bool  hovered = ImGui::IsItemHovered( );
			const float inset   = S( 4.0f );
			const ImVec2 cmin( top.x + width - close + inset, top.y + ( header_h - close ) * 0.5f + inset );
			const ImVec2 cmax( top.x + width - inset, top.y + ( header_h - close ) * 0.5f + close - inset );
			const ImU32  cross = col( hovered ? theme::accent : theme::text_disabled );

			dl->AddLine( cmin, cmax, cross, S( 1.6f ) );
			dl->AddLine( ImVec2( cmax.x, cmin.y ), ImVec2( cmin.x, cmax.y ), cross, S( 1.6f ) );

			// the rule under the header, then the body
			dl->AddLine( ImVec2( top.x, top.y + header_h ), ImVec2( top.x + width, top.y + header_h ), col( mix( theme::group_bg, theme::accent, theme::rule_mix ) ) );
			ImGui::SetCursorScreenPos( ImVec2( top.x, top.y + header_h + S( 8.0f ) ) );

			body( );
		}

		ImGui::End( );

	}
	// right click hook. a checkbox or a slider calls bind_row with its label and rectangle every frame. the kit has no keybinds, so by default it
	// does nothing. a host that has them sets g_bind_hook, which is called on a right click inside the rectangle, and opens its own window there
	// (panel_open_now and sub_window are the tools for that, see the README)
	using bind_hook_t = void ( * )( const char* name, ImVec2 min, ImVec2 max );
	inline bind_hook_t g_bind_hook = nullptr;

	inline void bind_row( const char* name, ImVec2 min, ImVec2 max )
	{
		if ( g_bind_hook && ImGui::IsMouseHoveringRect( min, max ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			g_bind_hook( name, min, max );
	}

	// button with an eased hover highlight. size is in design pixels, a zero width fills the row
	inline bool button( const char* label, ImVec2 size, bool selected = false )
	{
		ImGui::PushID( label );

		const ImVec2 p = ImGui::GetCursorScreenPos( );
		const ImVec2 s = S( size );
		const float  w = size.x <= 0.0f ? ImGui::GetContentRegionAvail( ).x : s.x;

		const bool  clicked = ImGui::InvisibleButton( "##btn", ImVec2( w, s.y ) );
		const bool  held    = ImGui::IsItemActive( );
		const float hover   = anim::approach( ImGui::GetID( "hover" ), ImGui::IsItemHovered( ) ? 1.0f : 0.0f, 14.0f );

		const ImVec4 base = ImVec4( 0.165f, 0.165f, 0.165f, 1.0f );
		const ImVec4 lift = ImVec4( 0.215f, 0.215f, 0.215f, 1.0f );
		const ImVec4 fill = held ? ImVec4( 0.240f, 0.240f, 0.240f, 1.0f ) : mix( base, lift, hover );

		const ImVec2 bmax( p.x + w, p.y + s.y );
		auto* dl = ImGui::GetWindowDrawList( );

		dl->AddRectFilled( p, bmax, col( fill ), S( theme::tab_rounding ) );
		dl->AddRect( p, bmax, col( theme::group_border ), S( theme::tab_rounding ) );

		const char* end   = ImGui::FindRenderedTextEnd( label );
		const ImVec2 ts   = text_size( label );
		const ImVec4 text = mix( theme::text, theme::accent, selected ? 1.0f : 0.0f );

		bold_text( dl, ImVec2( p.x + ( w - ts.x ) * 0.5f, p.y + ( s.y - ts.y ) * 0.5f ), col( text ), label );
		( void ) end;

		ImGui::PopID( );
		return clicked;
	}

	// checkbox: accent fill with a dark tick, eased in and out. right click opens its keybind window
	inline bool checkbox( const char* label, bool& value, float reserve = 0.0f )
	{
		ImGui::PushID( label );

		const ImVec2 p       = ImGui::GetCursorScreenPos( );
		const float  w       = ImGui::GetContentRegionAvail( ).x - S( reserve );
		const float  h       = S( 16.0f );
		const bool   clicked = ImGui::InvisibleButton( "##cb", ImVec2( w, h ) );
		const bool   hovered = ImGui::IsItemHovered( );

		if ( clicked )
			value = !value;

		const float state = anim::approach( ImGui::GetID( "state" ), value ? 1.0f : 0.0f, 16.0f );
		const float hover = anim::approach( ImGui::GetID( "hover" ), hovered ? 1.0f : 0.0f, 12.0f );

		auto* dl = ImGui::GetWindowDrawList( );
		const ImVec2 box_min( p.x, p.y + S( 1.0f ) );
		const ImVec2 box_max( p.x + S( 14.0f ), p.y + S( 15.0f ) );

		// checked: the fill is the accent at 94% and the border at 84%, #BD8E98 and #A87D85 on the default accent (sampled).
		// unchecked: the dark control fill with a faint border that lifts on hover
		const ImVec4 checked_fill( theme::accent.x * 0.94f, theme::accent.y * 0.94f, theme::accent.z * 0.94f, 1.0f );
		const ImVec4 checked_edge( theme::accent.x * 0.84f, theme::accent.y * 0.84f, theme::accent.z * 0.84f, 1.0f );

		const ImVec4 fill = mix( theme::check_bg, checked_fill, state );
		const ImVec4 edge = mix( mix( theme::group_border, theme::text_disabled, hover ), checked_edge, state );

		dl->AddRectFilled( box_min, box_max, col( fill ), S( theme::frame_rounding ) );
		dl->AddRect( box_min, box_max, col( edge ), S( theme::frame_rounding ) );

		if ( state > 0.01f )
		{
			const ImU32 tick = col( theme::window_bg, state );
			const float t    = S( 2.0f );
			dl->AddLine( ImVec2( box_min.x + S( 3.0f ), box_min.y + S( 6.5f ) ), ImVec2( box_min.x + S( 5.6f ), box_min.y + S( 9.2f ) ), tick, t );
			dl->AddLine( ImVec2( box_min.x + S( 5.6f ), box_min.y + S( 9.2f ) ), ImVec2( box_min.x + S( 10.0f ), box_min.y + S( 3.8f ) ), tick, t );
		}

		// a label may carry a "##id" suffix that tells two rows with the same text apart, for the ID and for the keybind it opens
		const std::string shown( label, ImGui::FindRenderedTextEnd( label ) );

		shadow_text( dl, ImVec2( p.x + S( 22.0f ), p.y ), text_col( ), shown.c_str( ) );
		bind_row( label, p, ImVec2( p.x + w, p.y + h ) );

		ImGui::PopID( );
		return clicked;
	}

	// colour picker, laid out like the reference: saturation square with the hue and alpha bars beside it,
	// and Copy and Paste under them. the picker keeps its own hsv so the hue survives a grey or black colour
	struct picker_state_t
	{
		float  h = 0.0f;
		float  s = 0.0f;
		float  v = 0.0f;
		ImVec4 last { 0.0f, 0.0f, 0.0f, -1.0f }; // the colour the hsv was last taken from
		char   hex[ 9 ] = { };                   // RRGGBBAA of the hex field while it is being typed in
	};

	// a light checker behind translucent colours
	inline void checker( ImDrawList* dl, ImVec2 min, ImVec2 max )
	{
		const float cell = S( 6.0f );

		dl->AddRectFilled( min, max, IM_COL32( 170, 170, 170, 255 ), 0.0f );

		int row = 0;

		for ( float y = min.y; y < max.y; y += cell, ++row )
		{
			int column = row;

			for ( float x = min.x; x < max.x; x += cell, ++column )
			{
				if ( column % 2 == 0 )
					continue;

				dl->AddRectFilled( ImVec2( x, y ), ImVec2( ImMin( x + cell, max.x ), ImMin( y + cell, max.y ) ), IM_COL32( 110, 110, 110, 255 ) );
			}
		}
	}

	inline ImU32 to_u32( float r, float g, float b, float a )
	{
		return ImGui::ColorConvertFloat4ToU32( ImVec4( r, g, b, a ) );
	}

	inline void color_picker( ImVec4& c )
	{
		static std::unordered_map<const void*, picker_state_t> states;
		auto& st = states[ &c ];

		// the colour changed outside the picker (paste, another swatch), so re-derive hsv
		if ( st.last.x != c.x || st.last.y != c.y || st.last.z != c.z || st.last.w != c.w )
			ImGui::ColorConvertRGBtoHSV( c.x, c.y, c.z, st.h, st.s, st.v );

		const float sv_w   = S( 170.0f );
		const float sv_h   = S( 170.0f );
		const float bar_w  = S( 14.0f );
		const float gap    = S( 8.0f );
		const float gap2   = S( 6.0f );
		const float btn_h  = S( 24.0f );
		const float total  = sv_w + gap + bar_w + gap2 + bar_w;
		const float btn_w  = ( total - S( 6.0f ) ) * 0.5f;

		auto* dl     = ImGui::GetWindowDrawList( );
		const ImVec2 origin = ImGui::GetCursorScreenPos( );

		// saturation / value square
		ImGui::SetCursorScreenPos( origin );
		ImGui::InvisibleButton( "##sv", ImVec2( sv_w, sv_h ) );

		if ( ImGui::IsItemActive( ) )
		{
			const ImVec2 mouse = ImGui::GetIO( ).MousePos;
			st.s = ImClamp( ( mouse.x - origin.x ) / sv_w, 0.0f, 1.0f );
			st.v = ImClamp( 1.0f - ( mouse.y - origin.y ) / sv_h, 0.0f, 1.0f );
		}

		float hr = 0.0f, hg = 0.0f, hb = 0.0f;
		ImGui::ColorConvertHSVtoRGB( st.h, 1.0f, 1.0f, hr, hg, hb );

		const ImVec2 sv_max( origin.x + sv_w, origin.y + sv_h );
		dl->AddRectFilledMultiColor( origin, sv_max, IM_COL32( 255, 255, 255, 255 ), to_u32( hr, hg, hb, 1.0f ),
			to_u32( hr, hg, hb, 1.0f ), IM_COL32( 255, 255, 255, 255 ) );
		dl->AddRectFilledMultiColor( origin, sv_max, IM_COL32( 0, 0, 0, 0 ), IM_COL32( 0, 0, 0, 0 ),
			IM_COL32( 0, 0, 0, 255 ), IM_COL32( 0, 0, 0, 255 ) );
		dl->AddRect( origin, sv_max, col( theme::group_border ), S( theme::group_rounding ) );

		const ImVec2 thumb( origin.x + st.s * sv_w, origin.y + ( 1.0f - st.v ) * sv_h );
		dl->AddCircle( thumb, S( 6.0f ), IM_COL32( 0, 0, 0, 220 ), 0, S( 2.0f ) );
		dl->AddCircle( thumb, S( 6.0f ), IM_COL32( 255, 255, 255, 255 ), 0, S( 1.5f ) );

		// hue bar, vertical, six segments around the colour wheel
		const ImVec2 hue_min( origin.x + sv_w + gap, origin.y );
		const ImVec2 hue_max( hue_min.x + bar_w, origin.y + sv_h );

		ImGui::SetCursorScreenPos( hue_min );
		ImGui::InvisibleButton( "##hue", ImVec2( bar_w, sv_h ) );

		if ( ImGui::IsItemActive( ) )
			st.h = ImClamp( ( ImGui::GetIO( ).MousePos.y - origin.y ) / sv_h, 0.0f, 1.0f );

		const int segments = 6;

		for ( int i = 0; i < segments; ++i )
		{
			float r0 = 0.0f, g0 = 0.0f, b0 = 0.0f;
			float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
			ImGui::ColorConvertHSVtoRGB( static_cast<float>( i ) / segments, 1.0f, 1.0f, r0, g0, b0 );
			ImGui::ColorConvertHSVtoRGB( static_cast<float>( i + 1 ) / segments, 1.0f, 1.0f, r1, g1, b1 );

			const float y0 = origin.y + sv_h * static_cast<float>( i ) / segments;
			const float y1 = origin.y + sv_h * static_cast<float>( i + 1 ) / segments;

			dl->AddRectFilledMultiColor( ImVec2( hue_min.x, y0 ), ImVec2( hue_max.x, y1 ),
				to_u32( r0, g0, b0, 1.0f ), to_u32( r0, g0, b0, 1.0f ), to_u32( r1, g1, b1, 1.0f ), to_u32( r1, g1, b1, 1.0f ) );
		}

		dl->AddRect( hue_min, hue_max, col( theme::group_border ), S( theme::frame_rounding ) );

		const float hy = origin.y + st.h * sv_h;
		dl->AddRectFilled( ImVec2( hue_min.x - S( 2.0f ), hy - S( 3.0f ) ), ImVec2( hue_max.x + S( 2.0f ), hy + S( 3.0f ) ), IM_COL32( 255, 255, 255, 255 ), S( 2.0f ) );
		dl->AddRect( ImVec2( hue_min.x - S( 2.0f ), hy - S( 3.0f ) ), ImVec2( hue_max.x + S( 2.0f ), hy + S( 3.0f ) ), IM_COL32( 0, 0, 0, 160 ), S( 2.0f ) );

		// alpha bar, vertical: opaque at the top, transparent at the bottom, over a checker
		const ImVec2 alpha_min( hue_max.x + gap2, origin.y );
		const ImVec2 alpha_max( alpha_min.x + bar_w, origin.y + sv_h );

		ImGui::SetCursorScreenPos( alpha_min );
		ImGui::InvisibleButton( "##alpha", ImVec2( bar_w, sv_h ) );

		if ( ImGui::IsItemActive( ) )
			c.w = 1.0f - ImClamp( ( ImGui::GetIO( ).MousePos.y - origin.y ) / sv_h, 0.0f, 1.0f );

		checker( dl, alpha_min, alpha_max );
		dl->AddRectFilledMultiColor( alpha_min, alpha_max,
			to_u32( c.x, c.y, c.z, 1.0f ), to_u32( c.x, c.y, c.z, 1.0f ),
			to_u32( c.x, c.y, c.z, 0.0f ), to_u32( c.x, c.y, c.z, 0.0f ) );
		dl->AddRect( alpha_min, alpha_max, col( theme::group_border ), S( theme::frame_rounding ) );

		const float ay = origin.y + ( 1.0f - c.w ) * sv_h;
		dl->AddRectFilled( ImVec2( alpha_min.x - S( 2.0f ), ay - S( 3.0f ) ), ImVec2( alpha_max.x + S( 2.0f ), ay + S( 3.0f ) ), IM_COL32( 255, 255, 255, 255 ), S( 2.0f ) );
		dl->AddRect( ImVec2( alpha_min.x - S( 2.0f ), ay - S( 3.0f ) ), ImVec2( alpha_max.x + S( 2.0f ), ay + S( 3.0f ) ), IM_COL32( 0, 0, 0, 160 ), S( 2.0f ) );

		// rgb from hsv, alpha kept
		ImGui::ColorConvertHSVtoRGB( st.h, st.s, st.v, c.x, c.y, c.z );
		st.last = c;

		// the hex field: RRGGBBAA, edited in place. while it is not being typed in it follows the colour, and a valid
		// 6 or 8 digit value is applied as it is typed (6 digits keep the alpha)
		const float field_h = S( 24.0f );
		const ImGuiID hex_id = ImGui::GetID( "##hex" );

		if ( ImGui::GetActiveID( ) != hex_id )
		{
			std::snprintf( st.hex, sizeof( st.hex ), "%02X%02X%02X%02X", static_cast<int>( c.x * 255.0f + 0.5f ), static_cast<int>( c.y * 255.0f + 0.5f ),
				static_cast<int>( c.z * 255.0f + 0.5f ), static_cast<int>( c.w * 255.0f + 0.5f ) );
		}

		const ImVec2 field_pos( origin.x, origin.y + sv_h + gap );
		ImGui::SetCursorScreenPos( field_pos );

		{
			style_scope field_style;
			ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( S( 20.0f ), ( field_h - ImGui::GetFontSize( ) ) * 0.5f ) );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, S( theme::frame_rounding ) );
			ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
			ImGui::PushStyleColor( ImGuiCol_FrameBg, theme::check_bg );
			ImGui::PushStyleColor( ImGuiCol_Border, theme::group_border );

			ImGui::SetNextItemWidth( total );

			if ( ImGui::InputText( "##hex", st.hex, sizeof( st.hex ), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase ) )
			{
				const std::size_t length = std::strlen( st.hex );

				if ( length == 6 || length == 8 )
				{
					unsigned int r = 0, g = 0, b = 0, a = 255;
					const int count = sscanf_s( st.hex, "%2x%2x%2x%2x", &r, &g, &b, &a );

					if ( count >= 3 )
					{
						c = ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, count == 4 ? a / 255.0f : c.w );
						ImGui::ColorConvertRGBtoHSV( c.x, c.y, c.z, st.h, st.s, st.v );
						st.last = c;
					}
				}
			}
		}

		// the # sits in the left padding of the field
		dl->AddText( ImGui::GetFont( ), S( k_font_px ), ImVec2( field_pos.x + S( 9.0f ), field_pos.y + ( field_h - text_size( "#" ).y ) * 0.5f ),
			col( theme::text_disabled ), "#" );

		// copy and paste, #RRGGBBAA, under the field
		ImGui::SetCursorScreenPos( ImVec2( origin.x, origin.y + sv_h + gap + field_h + S( 6.0f ) ) );

		char hex[ 16 ];
		std::snprintf( hex, sizeof( hex ), "#%02X%02X%02X%02X", static_cast<int>( c.x * 255.0f + 0.5f ), static_cast<int>( c.y * 255.0f + 0.5f ),
			static_cast<int>( c.z * 255.0f + 0.5f ), static_cast<int>( c.w * 255.0f + 0.5f ) );

		// button() scales its own size, so the already scaled widths are divided back to design pixels here
		if ( button( "Copy", ImVec2( btn_w / g_scale, btn_h / g_scale ) ) )
			ImGui::SetClipboardText( hex );

		ImGui::SameLine( 0.0f, S( 6.0f ) );

		if ( button( "Paste", ImVec2( btn_w / g_scale, btn_h / g_scale ) ) )
		{
			const char* clip = ImGui::GetClipboardText( );

			unsigned int r = 0, g = 0, b = 0, a = 255;
			const char* digits = ( clip && clip[ 0 ] == '#' ) ? clip + 1 : clip;
			const int count    = digits ? sscanf_s( digits, "%2x%2x%2x%2x", &r, &g, &b, &a ) : 0;

			if ( count >= 3 )
			{
				c = ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, count == 4 ? a / 255.0f : c.w );
				ImGui::ColorConvertRGBtoHSV( c.x, c.y, c.z, st.h, st.s, st.v );
				st.last = c;
			}
		}
	}

	// swatch that opens the colour picker window on click
	inline void swatch( const char* label, ImVec4& color, float w = 26.0f, float h = 11.0f )
	{
		ImGui::PushID( label );

		const ImVec2 p       = ImGui::GetCursorScreenPos( );
		const ImVec2 size    = S( ImVec2( w, h ) );
		const bool   clicked = ImGui::InvisibleButton( "##swatch", size );
		const ImVec2 pmax( p.x + size.x, p.y + size.y );

		const std::string key = std::string( "sw:" ) + label;

		if ( clicked )
			panel_toggle( key );

		ImGui::GetWindowDrawList( )->AddRectFilled( p, pmax, col( color ), S( theme::frame_rounding ) );

		sub_window( key, "Color", beside_window( ImGui::GetWindowPos( ).y ), [ & ]( )
			{
				color_picker( color );
			} );

		ImGui::PopID( );
	}

	// a settings row whose right side is a colour swatch, centred on the row
	inline void swatch_row( const char* label, const char* id, ImVec4& color )
	{
		row( label, 24.0f, 26.0f, [ & ]( ImVec2 at, float row_h )
			{
				ImGui::SetCursorScreenPos( ImVec2( at.x, at.y + ( row_h - S( 11.0f ) ) * 0.5f ) );
				swatch( id, color );
			} );
	}

	// dropdown. the list is as wide as the box or the widest item, whichever is larger
	// a dropdown box with its shown text centred in the box. the list opens as a normal combo
	inline bool begin_combo_centered( const char* id, const char* preview )
	{
		const bool open = ImGui::BeginCombo( id, "" );

		const ImVec2 min  = ImGui::GetItemRectMin( );
		const ImVec2 max  = ImGui::GetItemRectMax( );
		const ImVec2 size = ImGui::CalcTextSize( preview );

		ImGui::GetWindowDrawList( )->AddText( ImVec2( ( min.x + max.x - size.x ) * 0.5f, ( min.y + max.y - size.y ) * 0.5f ), col( theme::text ), preview );

		return open;
	}

	inline bool combo( const char* id, int& current, const char* const* items, int count, bool multi = false )
	{
		ImGui::PushID( id );

		const ImVec2 p       = ImGui::GetCursorScreenPos( );
		const float  w       = ImGui::GetContentRegionAvail( ).x;
		const float  h       = S( 24.0f );
		const bool   clicked = ImGui::InvisibleButton( "##combo", ImVec2( w, h ) );
		const ImVec2 bmax( p.x + w, p.y + h );

		if ( clicked )
			ImGui::OpenPopup( "##list" );

		auto* dl = ImGui::GetWindowDrawList( );
		dl->AddRectFilled( p, bmax, col( theme::check_bg ), S( theme::frame_rounding ) );
		dl->AddRect( p, bmax, col( theme::group_border ), S( theme::frame_rounding ) );

		// the text sits on the box's vertical centre, the same centre a row label uses
		const char* text = items[ current ];
		const ImVec2 ts  = text_size( text );
		shadow_text( dl, ImVec2( p.x + S( 9.0f ), p.y + ( h - ts.y ) * 0.5f ), col( theme::combo_text ), text );

		const float cx   = bmax.x - S( 12.0f );
		const float cy   = p.y + h * 0.5f;
		const ImU32 mark = col( theme::text_disabled );

		if ( multi )
		{
			for ( int i = -1; i <= 1; ++i )
				dl->AddLine( ImVec2( cx - S( 4.0f ), cy + i * S( 3.0f ) ), ImVec2( cx + S( 4.0f ), cy + i * S( 3.0f ) ), mark, S( 1.4f ) );
		}
		else
		{
			dl->AddLine( ImVec2( cx - S( 4.0f ), cy - S( 2.0f ) ), ImVec2( cx, cy + S( 2.0f ) ), mark, S( 1.4f ) );
			dl->AddLine( ImVec2( cx, cy + S( 2.0f ) ), ImVec2( cx + S( 4.0f ), cy - S( 2.0f ) ), mark, S( 1.4f ) );
		}

		float widest = 0.0f;

		for ( int i = 0; i < count; ++i )
			widest = ImMax( widest, text_size( items[ i ] ).x );

		const float pop_w = ImMax( w, widest + S( 36.0f ) );

		ImGui::SetNextWindowPos( ImVec2( p.x, bmax.y + S( 4.0f ) ), ImGuiCond_Always );
		ImGui::SetNextWindowSizeConstraints( ImVec2( pop_w, 0.0f ), ImVec2( pop_w, FLT_MAX ) );

		style_scope style;
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, S( ImVec2( 6.0f, 6.0f ) ) );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, S( ImVec2( 0.0f, 2.0f ) ) );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, S( theme::group_rounding ) );

		bool changed = false;

		if ( ImGui::BeginPopup( "##list", ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			for ( int i = 0; i < count; ++i )
			{
				if ( ImGui::Selectable( items[ i ], current == i, 0, ImVec2( 0.0f, S( 22.0f ) ) ) )
				{
					current = i;
					changed = true;
					ImGui::CloseCurrentPopup( );
				}
			}

			ImGui::EndPopup( );
		}

		ImGui::PopID( );
		return changed;
	}

	// a row of tabs in one bordered bar, with a divider between them. the selected tab is in the accent, the others dim and
	// lift on hover. every tab gets the same width, so the bar always fills the row. returns true when the selection changed
	inline bool sub_tabs( const char* id, const char* const* names, int count, int& current )
	{
		ImGui::PushID( id );

		const ImVec2 p    = ImGui::GetCursorScreenPos( );
		const float  w    = ImGui::GetContentRegionAvail( ).x;
		const float  h    = S( 28.0f );
		const float  cell = w / static_cast<float>( count );
		const ImVec2 bmax( p.x + w, p.y + h );

		auto* dl = ImGui::GetWindowDrawList( );
		dl->AddRectFilled( p, bmax, col( theme::check_bg ), S( theme::frame_rounding ) );
		dl->AddRect( p, bmax, col( theme::group_border ), S( theme::frame_rounding ) );

		bool changed = false;

		for ( int i = 0; i < count; ++i )
		{
			const float x = p.x + cell * static_cast<float>( i );

			ImGui::SetCursorScreenPos( ImVec2( x, p.y ) );
			ImGui::PushID( i );

			if ( ImGui::InvisibleButton( "##tab", ImVec2( cell, h ) ) && current != i )
			{
				current = i;
				changed = true;
			}

			const float hover    = anim::approach( ImGui::GetID( "hover" ), ImGui::IsItemHovered( ) ? 1.0f : 0.0f, 12.0f );
			const float selected = anim::approach( ImGui::GetID( "selected" ), current == i ? 1.0f : 0.0f, 14.0f );

			ImGui::PopID( );

			const ImVec4 color = mix( mix( theme::text_disabled, theme::text, hover * 0.6f ), theme::accent, selected );
			const ImVec2 ts    = text_size( names[ i ] );

			bold_text( dl, ImVec2( x + ( cell - ts.x ) * 0.5f, p.y + ( h - ts.y ) * 0.5f ), col( color ), names[ i ] );

			if ( i > 0 )
				dl->AddLine( ImVec2( x, p.y + S( 6.0f ) ), ImVec2( x, bmax.y - S( 6.0f ) ), col( theme::group_border ) );
		}

		// reserve the whole bar, so the next widget starts below it
		ImGui::SetCursorScreenPos( p );
		ImGui::Dummy( ImVec2( w, h ) );

		ImGui::PopID( );
		return changed;
	}

	// a settings row whose right side is a dropdown. the label and the box share one vertical centre
	inline bool combo_row( const char* label, const char* id, int& current, const char* const* items, int count, float control_w = 120.0f )
	{
		bool changed = false;

		row( label, 24.0f, control_w, [ & ]( ImVec2 at, float )
			{
				ImGui::SetCursorScreenPos( at );
				changed = combo( id, current, items, count );
			} );

		return changed;
	}

	enum class icon_t { crosshair, cursor, moon, gear, brush, code, bars, eye, disk };

	// vector icons, drawn at center c with size s (already scaled by the caller)
	inline void icon( ImDrawList* dl, icon_t kind, ImVec2 c, float s, ImU32 color, ImU32 bg )
	{
		const float r = s * 0.5f;
		const float t = s / 10.0f > 1.5f ? s / 10.0f : 1.5f;

		switch ( kind )
		{
		case icon_t::crosshair:
			dl->AddCircle( c, r * 0.72f, color, 0, t );
			dl->AddLine( ImVec2( c.x, c.y - r ), ImVec2( c.x, c.y - r * 0.4f ), color, t );
			dl->AddLine( ImVec2( c.x, c.y + r * 0.4f ), ImVec2( c.x, c.y + r ), color, t );
			dl->AddLine( ImVec2( c.x - r, c.y ), ImVec2( c.x - r * 0.4f, c.y ), color, t );
			dl->AddLine( ImVec2( c.x + r * 0.4f, c.y ), ImVec2( c.x + r, c.y ), color, t );
			dl->AddCircleFilled( c, t, color );
			break;

		case icon_t::cursor:
			dl->AddTriangleFilled( ImVec2( c.x - r * 0.6f, c.y - r ), ImVec2( c.x - r * 0.6f, c.y + r * 0.8f ), ImVec2( c.x + r * 0.7f, c.y + r * 0.3f ), color );
			break;

		case icon_t::moon:
			dl->AddCircleFilled( c, r, color, 24 );
			dl->AddCircleFilled( ImVec2( c.x + r * 0.45f, c.y - r * 0.25f ), r * 0.85f, bg, 24 );
			break;

		case icon_t::gear:
			for ( int i = 0; i < 8; ++i )
			{
				const float a = i * 0.785398f;
				const ImVec2 d( std::cos( a ), std::sin( a ) );
				dl->AddLine( ImVec2( c.x + d.x * r * 0.62f, c.y + d.y * r * 0.62f ), ImVec2( c.x + d.x * r, c.y + d.y * r ), color, t * 1.8f );
			}
			dl->AddCircleFilled( c, r * 0.72f, color, 20 );
			dl->AddCircleFilled( c, r * 0.3f, bg, 12 );
			break;

		case icon_t::brush:
			dl->AddLine( ImVec2( c.x + r * 0.7f, c.y - r * 0.7f ), ImVec2( c.x - r * 0.1f, c.y + r * 0.1f ), color, t * 1.4f );
			dl->AddTriangleFilled( ImVec2( c.x - r * 0.05f, c.y + r * 0.05f ), ImVec2( c.x - r * 0.75f, c.y + r * 0.55f ), ImVec2( c.x - r * 0.35f, c.y + r * 0.95f ), color );
			break;

		case icon_t::code:
			dl->AddLine( ImVec2( c.x - r * 0.4f, c.y - r * 0.6f ), ImVec2( c.x - r * 0.9f, c.y ), color, t );
			dl->AddLine( ImVec2( c.x - r * 0.9f, c.y ), ImVec2( c.x - r * 0.4f, c.y + r * 0.6f ), color, t );
			dl->AddLine( ImVec2( c.x + r * 0.4f, c.y - r * 0.6f ), ImVec2( c.x + r * 0.9f, c.y ), color, t );
			dl->AddLine( ImVec2( c.x + r * 0.9f, c.y ), ImVec2( c.x + r * 0.4f, c.y + r * 0.6f ), color, t );
			dl->AddLine( ImVec2( c.x + r * 0.2f, c.y - r * 0.8f ), ImVec2( c.x - r * 0.2f, c.y + r * 0.8f ), color, t );
			break;

		case icon_t::eye:
			// almond outline from two curved lids, and a pupil
			dl->AddBezierCubic( ImVec2( c.x - r, c.y ), ImVec2( c.x - r * 0.45f, c.y - r * 0.95f ), ImVec2( c.x + r * 0.45f, c.y - r * 0.95f ), ImVec2( c.x + r, c.y ), color, t );
			dl->AddBezierCubic( ImVec2( c.x - r, c.y ), ImVec2( c.x - r * 0.45f, c.y + r * 0.95f ), ImVec2( c.x + r * 0.45f, c.y + r * 0.95f ), ImVec2( c.x + r, c.y ), color, t );
			dl->AddCircleFilled( c, r * 0.32f, color, 16 );
			break;

		case icon_t::disk:
				// a floppy disk: a square with a label window at the top and a shutter at the bottom
				dl->AddRect( ImVec2( c.x - r * 0.8f, c.y - r * 0.8f ), ImVec2( c.x + r * 0.8f, c.y + r * 0.8f ), color, S( 2.0f ), 0, t );
				dl->AddRectFilled( ImVec2( c.x - r * 0.4f, c.y - r * 0.8f ), ImVec2( c.x + r * 0.4f, c.y - r * 0.2f ), color );
				dl->AddRectFilled( ImVec2( c.x - r * 0.4f, c.y + r * 0.3f ), ImVec2( c.x + r * 0.4f, c.y + r * 0.8f ), color );
				break;

			case icon_t::bars:
			for ( int i = -1; i <= 1; ++i )
				dl->AddLine( ImVec2( c.x - r, c.y + i * r * 0.6f ), ImVec2( c.x + r, c.y + i * r * 0.6f ), color, t );
			break;
		}
	}

	// gear that opens a draggable settings window under it. body draws the window's rows
	template <typename body_t>
	inline void gear_panel( const char* id, const char* title, body_t&& body )
	{
		ImGui::PushID( id );

		const ImVec2 size    = S( ImVec2( 14.0f, 14.0f ) );
		const bool   clicked = ImGui::InvisibleButton( "##gear", size );
		const bool   hovered = ImGui::IsItemHovered( );
		const ImVec2 gmin    = ImGui::GetItemRectMin( );
		const ImVec2 gmax    = ImGui::GetItemRectMax( );
		const float  hover   = anim::approach( ImGui::GetID( "hover" ), hovered ? 1.0f : 0.0f, 12.0f );

		const std::string key = std::string( "gp:" ) + id;

		if ( clicked )
			panel_toggle( key );

		icon( ImGui::GetWindowDrawList( ), icon_t::gear, ImVec2( ( gmin.x + gmax.x ) * 0.5f, ( gmin.y + gmax.y ) * 0.5f ), S( 12.0f ),
			col( mix( theme::text_disabled, theme::text, hover ) ), col( theme::group_bg ) );

		sub_window( key, title, beside_window( gmax.y ), body );

		ImGui::PopID( );
	}

	// two-state chip with an eased glow
	inline bool chip_toggle( const char* label, bool& value, float w )
	{
		ImGui::PushID( label );

		const ImVec2 p       = ImGui::GetCursorScreenPos( );
		const float  cw      = S( w );
		const float  ch      = S( 22.0f );
		const bool   clicked = ImGui::InvisibleButton( "##chip", ImVec2( cw, ch ) );

		if ( clicked )
			value = !value;

		const float state = anim::approach( ImGui::GetID( "state" ), value ? 1.0f : 0.0f, 14.0f );

		auto* dl = ImGui::GetWindowDrawList( );
		const ImVec2 bmax( p.x + cw, p.y + ch );

		if ( state > 0.01f )
			glow( dl, p, bmax, theme::accent, theme::tab_rounding, 0.35f * state );

		dl->AddRectFilled( p, bmax, col( theme::check_bg ), S( theme::tab_rounding ) );
		dl->AddRect( p, bmax, col( mix( theme::group_border, theme::accent, state ) ), S( theme::tab_rounding ) );

		const ImVec2 ts = text_size( label );
		shadow_text( dl, ImVec2( p.x + ( cw - ts.x ) * 0.5f, p.y + ( ch - ts.y ) * 0.5f ),
			col( mix( theme::text_disabled, theme::text, state ) ), label );

		ImGui::PopID( );
		return clicked;
	}
} // namespace yankeehook::gui::ui
