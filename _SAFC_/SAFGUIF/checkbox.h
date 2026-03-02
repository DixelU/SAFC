#pragma once
#ifndef SAFGUIF_CHECKBOX
#define SAFGUIF_CHECKBOX

#include <memory>
#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct checkbox : handleable_ui_part
{
	float x_pos, y_pos, side_size;
	std::uint32_t border_rgba_color, unchecked_rgba_background, checked_rgba_background;
	std::unique_ptr<single_text_line> tip;
	bool state, focused;
	std::uint8_t border_width;
	// Backward-compat aliases
	bool& State = state;
	std::unique_ptr<single_text_line>& Tip = tip;

	~checkbox() override = default;

	checkbox(float x_pos, float y_pos, float side_size, std::uint32_t border_rgba_color, std::uint32_t unchecked_rgba_background, std::uint32_t checked_rgba_background, std::uint8_t border_width, bool start_state = false, single_text_line_settings* tip_settings = nullptr, _Align tip_align = _Align::left, std::string tip_text = " ")
	{
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->side_size = side_size;
		this->border_rgba_color = border_rgba_color;
		this->unchecked_rgba_background = unchecked_rgba_background;
		this->checked_rgba_background = checked_rgba_background;
		this->state = start_state;
		this->focused = false;
		this->border_width = border_width;
		if (tip_settings)
		{
			this->tip.reset(tip_settings->create_one(tip_text));
			this->tip->safe_change_position_argumented(tip_align, x_pos - ((tip_align & _Align::left) ? 0.5f : ((tip_align & _Align::right) ? -0.5f : 0)) * side_size, y_pos - side_size);
		}
	}
	void draw() override
	{
		std::lock_guard locker(lock);
		float h_side_size = 0.5f * side_size;
		if (state)
			__glcolor(checked_rgba_background);
		else
			__glcolor(unchecked_rgba_background);
		glBegin(GL_QUADS);
		glVertex2f(x_pos + h_side_size, y_pos + h_side_size);
		glVertex2f(x_pos - h_side_size, y_pos + h_side_size);
		glVertex2f(x_pos - h_side_size, y_pos - h_side_size);
		glVertex2f(x_pos + h_side_size, y_pos - h_side_size);
		glEnd();
		if ((std::uint8_t)border_rgba_color && border_width)
		{
			__glcolor(border_rgba_color);
			glLineWidth(border_width);
			glBegin(GL_LINE_LOOP);
			glVertex2f(x_pos + h_side_size, y_pos + h_side_size);
			glVertex2f(x_pos - h_side_size, y_pos + h_side_size);
			glVertex2f(x_pos - h_side_size, y_pos - h_side_size);
			glVertex2f(x_pos + h_side_size, y_pos - h_side_size);
			glEnd();
			glPointSize(border_width);
			glBegin(GL_POINTS);
			glVertex2f(x_pos + h_side_size, y_pos + h_side_size);
			glVertex2f(x_pos - h_side_size, y_pos + h_side_size);
			glVertex2f(x_pos - h_side_size, y_pos - h_side_size);
			glVertex2f(x_pos + h_side_size, y_pos - h_side_size);
			glEnd();
		}
		if (focused && tip)
			tip->draw();
	}
	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
		if (tip) tip->safe_move(dx, dy);
	}
	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		new_x -= x_pos;
		new_y -= y_pos;
		safe_move(new_x, new_y);
	}
	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		float cw = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & arg))
			) * side_size,
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * side_size;
		safe_change_position(new_x + cw, new_y + ch);
	}
	void keyboard_handler(char ch) override
	{
		return;
	}
	void safe_string_replace(std::string tip_string) override
	{
		std::lock_guard locker(lock);
		if (tip)
			tip->safe_string_replace(tip_string);
	}
	void focus_change()
	{
		std::lock_guard locker(lock);
		this->focused = !this->focused;
		border_rgba_color = (((~(border_rgba_color >> 8)) << 8) | (border_rgba_color & 0xFF));
	}
	[[nodiscard]] bool mouse_handler(float mx, float my, char button, char state_val) override
	{
		std::lock_guard locker(lock);
		if (fabsf(mx - x_pos) < 0.5f * side_size && fabsf(my - y_pos) < 0.5f * side_size)
		{
			if (!focused)
				focus_change();
			if (button)
			{
				if (state_val == 1)
					this->state = !this->state;
				return true;
			}
			else
				return false;
		}
		else
		{
			if (focused)
				focus_change();
			return false;
		}
	}
	[[nodiscard]] inline std::uint32_t tell_type() override
	{
		return TT_CHECKBOX;
	}
};
using CheckBox = checkbox;

#endif // !SAFGUIF_CHECKBOX
