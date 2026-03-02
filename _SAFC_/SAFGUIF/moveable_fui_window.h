#pragma once
#ifndef _SAFGUIF_MFW_H
#define _SAFGUIF_MFW_H

#include <numbers>

#include "moveable_window.h"

// Moveable futuristic window (non-resizeable)
struct moveable_fui_window : public moveable_window
{
	std::uint32_t border_rgba;
	float headers_hat_width;
	float headers_hat_height;
	float top_handles_height;
	float bottom_handles_height;
	float handles_budge_depth;

	inline static constexpr float budge_slope = 1.f;

	moveable_fui_window(std::string win_name, single_text_line_settings* win_name_settings,
		float x_pos, float y_pos, float width, float height,
		float headers_hat_width, float headers_hat_height,
		float top_handles_height, float bottom_handles_height,
		float handles_budge_depth,
		std::uint32_t rgba_background, std::uint32_t rgba_theme_color, std::uint32_t border_rgba) :
		moveable_window(win_name, win_name_settings, x_pos, y_pos, width, height, rgba_background, rgba_theme_color),
		border_rgba(border_rgba),
		headers_hat_width(headers_hat_width),
		headers_hat_height(headers_hat_height),
		top_handles_height(top_handles_height),
		bottom_handles_height(bottom_handles_height),
		handles_budge_depth(handles_budge_depth)
	{
		if (this->window_name)
			this->window_name->safe_change_position_argumented(
				_Align::center,
				x_pos + this->width * 0.5f,
				y_pos - (window_header_size - this->headers_hat_height) * 0.5f);
	}

	~moveable_fui_window() override = default;

	void draw() override
	{
		std::lock_guard locker(lock);

		if (!drawable)
			return;

		// HEADER
		float full_header_hat_width = headers_hat_width + headers_hat_height * 2 * budge_slope;
		float headers_hat_begin_x = x_window_pos + 0.5f * (width - full_header_hat_width);

		__glcolor(rgba_theme_color);
		glBegin(GL_QUADS);
		// Main header
		glVertex2f(x_window_pos, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos - window_header_size);
		glVertex2f(x_window_pos, y_window_pos - window_header_size);

		// header hat
		glVertex2f(headers_hat_begin_x, y_window_pos);
		glVertex2f(headers_hat_begin_x + budge_slope * headers_hat_height, y_window_pos + headers_hat_height);
		glVertex2f(headers_hat_begin_x + budge_slope * headers_hat_height + headers_hat_width, y_window_pos + headers_hat_height);
		glVertex2f(headers_hat_begin_x + full_header_hat_width, y_window_pos);
		glEnd();

		// main body (background)
		__glcolor(rgba_background);
		// Top handles area
		glBegin(GL_POLYGON);
		glVertex2f(x_window_pos, y_window_pos - window_header_size);
		glVertex2f(x_window_pos, y_window_pos - window_header_size - top_handles_height);
		glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width, y_window_pos - window_header_size - top_handles_height);
		glVertex2f(x_window_pos + width, y_window_pos - window_header_size);
		glEnd();

		// Bottom handles area
		glBegin(GL_POLYGON);
		glVertex2f(x_window_pos, y_window_pos - height);
		glVertex2f(x_window_pos, y_window_pos - height + bottom_handles_height);
		glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width, y_window_pos - height + bottom_handles_height);
		glVertex2f(x_window_pos + width, y_window_pos - height);
		glEnd();

		// Middle area
		glBegin(GL_POLYGON);
		glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
		glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
		glEnd();

		auto fading_border_rgba = border_rgba;
		for (size_t line_width = 1; line_width <= 2; line_width *= 2)
		{
			__glcolor(fading_border_rgba);

			// Top handles border
			glLineWidth((float)line_width);
			glBegin(GL_LINE_STRIP);
			glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
			glVertex2f(x_window_pos, y_window_pos - window_header_size - top_handles_height);
			glVertex2f(x_window_pos, y_window_pos - window_header_size);
			glVertex2f(x_window_pos + width, y_window_pos - window_header_size);
			glVertex2f(x_window_pos + width, y_window_pos - window_header_size - top_handles_height);
			glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth);
			glEnd();

			// Bottom handles border
			glBegin(GL_LINE_STRIP);
			glVertex2f(x_window_pos + handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
			glVertex2f(x_window_pos, y_window_pos - height + bottom_handles_height);
			glVertex2f(x_window_pos, y_window_pos - height);
			glVertex2f(x_window_pos + width, y_window_pos - height);
			glVertex2f(x_window_pos + width, y_window_pos - height + bottom_handles_height);
			glVertex2f(x_window_pos + width - handles_budge_depth, y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth);
			glEnd();

			// header hat border
			glBegin(GL_LINE_STRIP);
			glVertex2f(x_window_pos, y_window_pos - window_header_size);
			glVertex2f(x_window_pos, y_window_pos);
			glVertex2f(headers_hat_begin_x, y_window_pos);
			glVertex2f(headers_hat_begin_x + budge_slope * headers_hat_height, y_window_pos + headers_hat_height);
			glVertex2f(headers_hat_begin_x + budge_slope * headers_hat_height + headers_hat_width, y_window_pos + headers_hat_height);
			glVertex2f(headers_hat_begin_x + full_header_hat_width, y_window_pos);
			glVertex2f(x_window_pos + width, y_window_pos);
			glVertex2f(x_window_pos + width, y_window_pos - window_header_size);
			glEnd();

			fading_border_rgba = (fading_border_rgba & 0xFFFFFF00) | ((fading_border_rgba & 0xFF) >> 1);
		}

		glLineWidth(3.f + hovered_close_button);
		__glcolor(border_rgba | (0xFF * hovered_close_button) | 0xFF000000);
		constexpr int close_button_sides = 3;
		glBegin(GL_LINES);
		for (int i = 0; i < close_button_sides; i++)
		{
			float x_offset = sinf(2 * std::numbers::pi_v<float> * i / float(close_button_sides)) * 0.35f * window_header_size;
			float y_offset = cosf(2 * std::numbers::pi_v<float> * i / float(close_button_sides)) * 0.35f * window_header_size;

			glVertex2f(x_window_pos + width - window_header_size * 0.5f, y_window_pos - window_header_size * 0.45f);
			glVertex2f(x_window_pos + width - window_header_size * 0.5f + x_offset, y_window_pos - window_header_size * 0.45f - y_offset);
		}
		glEnd();

		if (window_name)
			window_name->draw();

		for (auto& [_, elem] : window_activities)
			if (elem) elem->draw();
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);

		if (!drawable)
			return false;

		hovered_close_button = false;

		float close_button_x[] =
		{
			x_window_pos + width - window_header_size,
			x_window_pos + width,
			x_window_pos + width,
			x_window_pos + width - window_header_size
		};
		float close_button_y[] =
		{
			y_window_pos,
			y_window_pos,
			y_window_pos - window_header_size,
			y_window_pos - window_header_size
		};

		float full_header_hat_width = headers_hat_width + headers_hat_height * 2 * budge_slope;
		float headers_hat_begin_x = x_window_pos + 0.5f * (width - full_header_hat_width);
		float header_geometry_x[] =
		{
			x_window_pos,
			headers_hat_begin_x,
			headers_hat_begin_x + budge_slope * headers_hat_height,
			headers_hat_begin_x + budge_slope * headers_hat_height + headers_hat_width,
			headers_hat_begin_x + full_header_hat_width,
			x_window_pos + width,
			x_window_pos + width,
			x_window_pos
		};
		float header_geometry_y[] =
		{
			y_window_pos,
			y_window_pos,
			y_window_pos + headers_hat_height,
			y_window_pos + headers_hat_height,
			y_window_pos,
			y_window_pos,
			y_window_pos - window_header_size,
			y_window_pos - window_header_size
		};

		float window_geometry_x[] =
		{
			x_window_pos,
			x_window_pos,
			x_window_pos + handles_budge_depth,
			x_window_pos + handles_budge_depth,
			x_window_pos,
			x_window_pos,
			x_window_pos + width,
			x_window_pos + width,
			x_window_pos + width - handles_budge_depth,
			x_window_pos + width - handles_budge_depth,
			x_window_pos + width,
			x_window_pos + width
		};
		float window_geometry_y[] =
		{
			y_window_pos - window_header_size,
			y_window_pos - window_header_size - top_handles_height,
			y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth,
			y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth,
			y_window_pos - height + bottom_handles_height,
			y_window_pos - height,
			y_window_pos - height,
			y_window_pos - height + bottom_handles_height,
			y_window_pos - height + bottom_handles_height + budge_slope * handles_budge_depth,
			y_window_pos - window_header_size - top_handles_height - budge_slope * handles_budge_depth,
			y_window_pos - window_header_size - top_handles_height,
			y_window_pos - window_header_size
		};

		bool in_header = false;

		// close button
		if (in_header |= PointInPoly(close_button_x, close_button_y, mx, my))
		{
			if (button_btn && state == 1)
			{
				drawable = false;
				cursor_follow_mode = false;
				return true;
			}
			else if (!button_btn)
				hovered_close_button = true;
		}
		// window header
		else if (in_header |= PointInPoly(header_geometry_x, header_geometry_y, mx, my))
		{
			if (button_btn == -1)
			{
				if (state == -1)
				{
					cursor_follow_mode = !cursor_follow_mode;
					pc_cur_x = mx;
					pc_cur_y = my;
				}
				else if (state == 1)
					cursor_follow_mode = !cursor_follow_mode;
			}
		}
		if (cursor_follow_mode)
		{
			safe_move(mx - pc_cur_x, my - pc_cur_y);
			pc_cur_x = mx;
			pc_cur_y = my;
			return true;
		}

		bool flag = false;
		auto it = window_activities.begin();
		while (it != window_activities.end())
		{
			if (it->second)
				flag = it->second->mouse_handler(mx, my, button_btn, state);
			if (map_was_changed)
			{
				map_was_changed = false;
				break;
			}
			++it;
		}

		if (in_header || PointInPoly(window_geometry_x, window_geometry_y, mx, my))
		{
			if (button_btn) return true;
			else return flag;
		}
		else
			return flag;
	}

	void safe_window_rename(const std::string& new_window_title, bool force_no_shift = false) override
	{
		std::lock_guard locker(lock);
		if (!window_name)
			return;

		window_name->safe_string_replace(new_window_title);
		if (force_no_shift)
			return;

		this->window_name->safe_change_position_argumented(
			_Align::center,
			x_window_pos + width * 0.5f,
			y_window_pos - (window_header_size - headers_hat_height) * 0.5f);
	}
};

using MoveableFuiWindow = moveable_fui_window;

#endif
