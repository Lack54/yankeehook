#pragma once

#include "primitives.hpp"
#include "theme.hpp"

namespace yankeehook::gui::widgets
{
    struct slider_result_t
    {
        bool changed      = false;
        bool gear_clicked = false;
    };

    namespace
    {
        // geometry (px at 100% DPI)
        constexpr float k_gap_label_badge  = 8.0f;
        constexpr float k_badge_pad_x      = 6.0f;
        constexpr float k_badge_rounding   = 4.0f;
        constexpr float k_gap_header_track = 8.0f;
        constexpr float k_track_h          = 8.0f;    // sampled from the reference
        constexpr float k_track_rounding   = 3.0f;
        constexpr float k_track_hit_h      = 14.0f;
        constexpr float k_knob_w           = 6.0f;
        constexpr float k_knob_h           = 14.0f;
        constexpr float k_gear_size        = 14.0f;
        constexpr float k_row_bottom_gap   = 6.0f;
    } // namespace

    // a labelled slider row: the label and a value badge on top, the track under them. zero_text replaces the badge at lo (e.g. "Off")
    inline slider_result_t slider_float_row( const char* label, float* value, float lo, float hi, const char* fmt = "%.0f", bool show_gear = false, const char* zero_text = nullptr )
    {
        slider_result_t result { };

        if ( !label || !value || !fmt || hi <= lo )
            return result;

        ImGui::PushID( label );

        auto* dl            = ImGui::GetWindowDrawList( );
        const ImVec2 origin = ImGui::GetCursorScreenPos( );
        const float width   = ImGui::GetContentRegionAvail( ).x;
        const float line_h  = ImGui::GetTextLineHeight( );
        const float x0      = origin.x;
        const float x1      = origin.x + width;

        char value_buf[ 32 ];

        if ( zero_text && *value == lo )
            ImFormatString( value_buf, sizeof( value_buf ), "%s", zero_text );
        else
            ImFormatString( value_buf, sizeof( value_buf ), fmt, *value );

        const ImVec2 label_sz = ImGui::CalcTextSize( label, ImGui::FindRenderedTextEnd( label ) );
        const ImVec2 value_sz = ImGui::CalcTextSize( value_buf );

        ui::bold_text( dl, origin, ui::text_col( ), label );

        const ImVec2 badge_min( x0 + label_sz.x + k_gap_label_badge, origin.y - 2.0f );
        const ImVec2 badge_max( badge_min.x + value_sz.x + k_badge_pad_x * 2.0f, origin.y + line_h + 2.0f );

        dl->AddRectFilled( badge_min, badge_max, ui::col( theme::badge_bg ), k_badge_rounding );
        ui::shadow_text( dl, ImVec2( badge_min.x + k_badge_pad_x, origin.y ), ui::col( theme::badge_text ), value_buf );

        // track: hit area + interaction
        const float track_y     = origin.y + line_h + k_gap_header_track;
        const float track_mid_y = track_y + k_track_hit_h * 0.5f;

        ImGui::SetCursorScreenPos( ImVec2( x0, track_y ) );
        ImGui::InvisibleButton( "##track", ImVec2( width, k_track_hit_h ) );

        if ( ImGui::IsItemActive( ) )
        {
            // the knob centre travels between half a knob in from each end, so the mouse maps onto that span
            const float t         = ImClamp( ( ImGui::GetIO( ).MousePos.x - x0 - k_knob_w * 0.5f ) / ( width - k_knob_w ), 0.0f, 1.0f );
            const float new_value = lo + t * ( hi - lo );

            if ( new_value != *value )
            {
                *value         = new_value;
                result.changed = true;
            }
        }

        const bool  hovered = ImGui::IsItemHovered( ) || ImGui::IsItemActive( );
        const float hover   = anim::approach( ImGui::GetID( "knob_hover" ), hovered ? 1.0f : 0.0f, 14.0f );

        const float t_val = ImClamp( ( *value - lo ) / ( hi - lo ), 0.0f, 1.0f );

        // the knob is kept inside the track, so it is whole at both ends like in the reference
        const float half_knob = k_knob_w * 0.5f;
        const float knob_x    = x0 + half_knob + t_val * ( width - k_knob_w );

        const ImVec2 track_min( x0, track_mid_y - k_track_h * 0.5f );
        const ImVec2 track_max( x1, track_mid_y + k_track_h * 0.5f );

        dl->AddRectFilled( track_min, track_max, ui::col( theme::check_bg ), k_track_rounding );

        // flat fill, no glow. the accent at 86% is the sampled fill colour
        if ( t_val > 0.0f )
        {
            const ImVec4 fill( theme::accent.x * theme::slider_fill_scale, theme::accent.y * theme::slider_fill_scale,
                theme::accent.z * theme::slider_fill_scale, 1.0f );

            dl->AddRectFilled( track_min, ImVec2( knob_x, track_max.y ), ui::col( fill ), k_track_rounding );
        }

        const ImVec2 knob_min( knob_x - half_knob, track_mid_y - k_knob_h * 0.5f );
        const ImVec2 knob_max( knob_x + half_knob, track_mid_y + k_knob_h * 0.5f );

        dl->AddRectFilled( knob_min, knob_max, ui::col( ui::mix( theme::slider_knob, theme::slider_knob_hover, hover ) ), 2.0f );

        // gear
        if ( show_gear )
        {
            const ImVec2 gear_min( x1 - k_gear_size, origin.y );

            ImGui::SetCursorScreenPos( gear_min );
            result.gear_clicked = ImGui::InvisibleButton( "##gear", ImVec2( k_gear_size, k_gear_size ) );

            const bool gear_hover = ImGui::IsItemHovered( );
            ui::icon( dl, ui::icon_t::gear, ImVec2( gear_min.x + k_gear_size * 0.5f, gear_min.y + k_gear_size * 0.5f ), 12.0f,
                ui::col( gear_hover ? theme::text : theme::text_disabled ), ui::col( theme::group_bg ) );
        }

        ui::bind_row( label, origin, ImVec2( x1, track_y + k_track_hit_h ) );

        // advance layout past the whole row
        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( width, line_h + k_gap_header_track + k_track_hit_h + k_row_bottom_gap ) );

        ImGui::PopID( );

        return result;
    }
} // namespace yankeehook::gui::widgets
