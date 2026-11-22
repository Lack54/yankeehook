#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "primitives.hpp"

namespace yankeehook::gui
{
	// one entry of the left rail. title is bold, sub is the dim line under it
	struct nav_item_t
	{
		std::string title;
		std::string sub;
	};

	// one page, reached from the bottom tab bar. nav is the left rail of the page (empty hides the rail). draw is called every frame while the
	// page is shown, with the index of the selected rail item (0 when there is no rail). it draws its groups with ui::group_begin / group_end
	struct page_t
	{
		std::string                name;
		ui::icon_t                 icon = ui::icon_t::gear;
		std::vector<nav_item_t>    nav;
		std::function<void( int )> draw;
	};

	// the menu window: title bar with the animated mark, left rail, page, bottom tab bar
	class c_menu
	{
	public:

		void add_page( page_t page );

		// call once per frame, between ImGui::NewFrame and ImGui::Render
		void on_render( );

	public:

		// the menu is shown while this is true
		bool        m_draw = true;

		// the wordmark in the title bar, at most 15 letters
		std::string m_name = "yankeehook";

	private:

		void draw_title_bar( );
		void draw_nav( );
		void draw_page( );
		void draw_bottom_bar( );

	private:

		ImVec2              m_pos       = { -1.0f, -1.0f };
		int                 m_page      = 0;
		int                 m_last_page = 0;
		std::vector<page_t> m_pages;
		std::vector<int>    m_nav;
	};

	inline auto menu = std::make_unique<c_menu>( );
} // namespace yankeehook::gui
