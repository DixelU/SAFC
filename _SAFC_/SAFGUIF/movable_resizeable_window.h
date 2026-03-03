#pragma once
#ifndef SAFGUIF_MRW_H
#define SAFGUIF_MRW_H

#include <functional>

#include "moveable_window.h"

struct moveable_resizeable_window : moveable_window
{
	enum class PinSide
	{
		left, right, bottom, top, center
	};

	bool resize_corner_is_active = false;
	bool resize_corner_is_hovered = false;
	float min_height = 0, min_width = 0;
	std::multimap<std::string, PinSide> pinned_window_activities;
	std::function<void(float, float, float, float)> on_resize;

	moveable_resizeable_window(std::string win_name, single_text_line_settings* win_name_settings,
		float x_pos, float y_pos, float width, float height,
		std::uint32_t rgba_background, std::uint32_t rgba_theme_color,
		std::uint32_t rgba_grad_background = 0,
		std::function<void(float, float, float, float)> on_resize = [](float, float, float, float) {})
		: moveable_window(win_name, win_name_settings, x_pos, y_pos, width, height, rgba_background, rgba_theme_color, rgba_grad_background),
		  on_resize(std::move(on_resize))
	{
	}

	[[nodiscard]] bool is_resizeable() override
	{
		return true;
	}

	void safe_resize(float new_height, float new_width) override
	{
		std::lock_guard locker(lock);

		if (new_height < min_height && new_width < min_width)
			return;
		else if (new_height < min_height)
			new_height = min_height;
		else if (new_width < min_width)
			new_width = min_width;

		float dh = new_height - height, dw = new_width - width;
		on_resize(dh, dw, new_height, new_width);

		not_safe_resize(new_height, new_width);

		for (auto& [ui_part_name, pin_side_val] : pinned_window_activities)
		{
			switch (pin_side_val)
			{
				case PinSide::left:
				case PinSide::top:
					break;
				case PinSide::right:
				case PinSide::bottom:
				{
					int is_bottom = (int)pin_side_val - (int)PinSide::right;
					auto cur_activity = window_activities.find(ui_part_name);
					if (cur_activity != window_activities.end())
						cur_activity->second->safe_move(dw * (!is_bottom), -dh * (!!is_bottom));
					break;
				}
				case PinSide::center:
				{
					auto cur_activity = window_activities.find(ui_part_name);
					if (cur_activity != window_activities.end())
						cur_activity->second->safe_move(dw * 0.5f, -dh * 0.5f);
					break;
				}
			}
		}
	}

	void assign_min_dimensions(float new_min_height, float new_min_width)
	{
		std::lock_guard locker(lock);

		min_height = new_min_height;
		min_width = new_min_width;
	}

	void assign_pinned_activities(const std::initializer_list<std::string>& list, PinSide side)
	{
		std::lock_guard locker(lock);
		for (auto& val : list)
			pinned_window_activities.insert({ val, side });
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);

		if (!drawable)
			return false;

		float center_draggable_x = x_window_pos + width, center_draggable_y = y_window_pos - height;
		float dw = mx - center_draggable_x, dh = center_draggable_y - my;

		if (std::abs(mx - center_draggable_x) + std::abs(my - center_draggable_y) < 2.5f)
		{
			SetCursor(nwse_cursor);
			resize_corner_is_hovered = true;
			if (button_btn == -1 && (state == -1 || state == 1))
				resize_corner_is_active = !resize_corner_is_active;
		}
		else if (resize_corner_is_hovered)
			resize_corner_is_hovered = false;

		if (resize_corner_is_active)
		{
			if (button_btn == -1 && state == 1)
				resize_corner_is_active = false;
			safe_resize(height + dh, width + dw);
			return true;
		}

		return moveable_window::mouse_handler(mx, my, button_btn, state);
	}

	void draw() override
	{
		std::lock_guard locker(lock);

		if (!drawable)
			return;

		float center_draggable_x = x_window_pos + width, center_draggable_y = y_window_pos - height;

		__glcolor(rgba_theme_color | (std::uint32_t)(0xFFFFFFFF * resize_corner_is_active));
		glLineWidth(1.f + resize_corner_is_hovered);
		glBegin(GL_LINE_LOOP);
		glVertex2f(center_draggable_x + 2.5f, center_draggable_y + 2.5f);
		glVertex2f(center_draggable_x + 2.5f, center_draggable_y - 2.5f);
		glVertex2f(center_draggable_x - 2.5f, center_draggable_y - 2.5f);
		glEnd();

		moveable_window::draw();
	}
};

using MoveableResizeableWindow = moveable_resizeable_window;

#endif
