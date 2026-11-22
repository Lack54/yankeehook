#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>

// the yankeehook mark: a fox head in thin line art. drawn from points on a 34 unit grid, so it scales with the size given.
// the outline can be drawn partway (the startup animation traces it), a bright segment can travel along it (the hover
// animation), and the inner lines fade in separately

namespace yankeehook::gui::logo
{
	struct style_t
	{
		float draw      = 1.0f;                       // 0 to 1: how much of the outline is drawn
		float details   = 1.0f;                       // 0 to 1: opacity of the inner ears, eyes and nose
		float highlight = -1.0f;                      // 0 to 1: where a bright segment starts on the outline. below 0 is off
		ImU32 highlight_color = IM_COL32( 255, 255, 255, 255 );
	};

	// the colour with its alpha scaled
	inline ImU32 with_alpha( ImU32 color, float alpha )
	{
		const ImU32 base = ( color >> 24 ) & 0xFF;
		const ImU32 next = static_cast<ImU32>( static_cast<float>( base ) * ImClamp( alpha, 0.0f, 1.0f ) );

		return ( color & 0x00FFFFFF ) | ( next << 24 );
	}

	// the point a given fraction of the way along the outline, from the left ear tip round to where it started. the
	// hover animation uses it to put a light at the front of the line that runs along the outline
	inline ImVec2 point_at( ImVec2 origin, float size, float fraction )
	{
		const float u = size / 34.0f;

		const auto pt = [ & ]( float x, float y )
		{
			return ImVec2( origin.x + x * u, origin.y + y * u );
		};

		const ImVec2 head[ 10 ] =
		{
			pt( 2.0f, 1.0f ), pt( 12.5f, 9.0f ), pt( 21.5f, 9.0f ), pt( 32.0f, 1.0f ),
			pt( 32.0f, 16.0f ), pt( 26.0f, 26.0f ), pt( 17.0f, 33.0f ), pt( 8.0f, 26.0f ), pt( 2.0f, 16.0f ),
			pt( 2.0f, 1.0f ),
		};

		float lengths[ 9 ];
		float total = 0.0f;

		for ( int i = 0; i < 9; ++i )
		{
			const float dx = head[ i + 1 ].x - head[ i ].x;
			const float dy = head[ i + 1 ].y - head[ i ].y;

			lengths[ i ] = std::sqrt( dx * dx + dy * dy );
			total       += lengths[ i ];
		}

		float remaining = ImClamp( fraction, 0.0f, 1.0f ) * total;

		for ( int i = 0; i < 9; ++i )
		{
			if ( remaining <= lengths[ i ] )
				return ImLerp( head[ i ], head[ i + 1 ], remaining / lengths[ i ] );

			remaining -= lengths[ i ];
		}

		return head[ 9 ];
	}

	inline void draw( ImDrawList* dl, ImVec2 origin, float size, ImU32 color, const style_t& style = style_t { } )
	{
		const float u = size / 34.0f;

		const auto pt = [ & ]( float x, float y )
		{
			return ImVec2( origin.x + x * u, origin.y + y * u );
		};

		const float line = ImMax( 1.6f, 1.9f * u );

		// the head outline, ear tip to ear tip over the brow, down the cheeks to the nose, and back to the start
		const ImVec2 head[ 10 ] =
		{
			pt( 2.0f, 1.0f ), pt( 12.5f, 9.0f ), pt( 21.5f, 9.0f ), pt( 32.0f, 1.0f ),
			pt( 32.0f, 16.0f ), pt( 26.0f, 26.0f ), pt( 17.0f, 33.0f ), pt( 8.0f, 26.0f ), pt( 2.0f, 16.0f ),
			pt( 2.0f, 1.0f ),
		};

		float lengths[ 9 ];
		float total = 0.0f;

		for ( int i = 0; i < 9; ++i )
		{
			const float dx = head[ i + 1 ].x - head[ i ].x;
			const float dy = head[ i + 1 ].y - head[ i ].y;

			lengths[ i ] = std::sqrt( dx * dx + dy * dy );
			total       += lengths[ i ];
		}

		// the outline, up to the part that is drawn so far. the joints get a dot, so the corners stay round
		const float drawn = ImClamp( style.draw, 0.0f, 1.0f ) * total;
		float       start = 0.0f;

		for ( int i = 0; i < 9 && start < drawn; ++i )
		{
			const float t   = ImMin( lengths[ i ], drawn - start ) / lengths[ i ];
			const ImVec2 to = ImLerp( head[ i ], head[ i + 1 ], t );

			dl->AddLine( head[ i ], to, color, line );
			dl->AddCircleFilled( head[ i ], line * 0.5f, color, 8 );

			start += lengths[ i ];
		}

		// the travelling highlight, a fifth of the outline long, over the part that is drawn
		if ( style.highlight >= 0.0f )
		{
			const float h0 = style.highlight * total;
			const float h1 = h0 + 0.2f * total;

			start = 0.0f;

			for ( int i = 0; i < 9; ++i )
			{
				const float s0 = start;
				const float s1 = start + lengths[ i ];
				const float a  = ImMax( s0, h0 );
				const float b  = ImMin( ImMin( s1, h1 ), drawn );

				if ( b > a )
					dl->AddLine( ImLerp( head[ i ], head[ i + 1 ], ( a - s0 ) / lengths[ i ] ), ImLerp( head[ i ], head[ i + 1 ], ( b - s0 ) / lengths[ i ] ),
						style.highlight_color, line * 1.3f );

				start = s1;
			}
		}

		if ( style.details <= 0.01f )
			return;

		const ImU32 inner = with_alpha( color, style.details );

		// the inner ears, the eyes slanted in towards the nose, and the nose
		dl->AddLine( pt( 6.0f, 6.0f ), pt( 9.5f, 12.0f ), inner, line * 0.8f );
		dl->AddLine( pt( 28.0f, 6.0f ), pt( 24.5f, 12.0f ), inner, line * 0.8f );
		dl->AddLine( pt( 8.5f, 17.0f ), pt( 13.5f, 19.5f ), inner, line );
		dl->AddLine( pt( 25.5f, 17.0f ), pt( 20.5f, 19.5f ), inner, line );
		dl->AddCircleFilled( pt( 17.0f, 28.5f ), 2.1f * u, inner, 12 );
	}
} // namespace yankeehook::gui::logo
