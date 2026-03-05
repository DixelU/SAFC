#pragma once
#ifndef SAFGUIF_BUTTON
#define SAFGUIF_BUTTON

#include <functional>
#include <memory>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct button : handleable_ui_part
{
	std::unique_ptr<single_text_line> stl, tip;

	float x_pos, y_pos;
	float width, height;

	std::uint32_t rgba_color, rgba_background, rgba_border;
	std::uint32_t hovered_rgba_color, hovered_rgba_background, hovered_rgba_border;
	std::uint8_t border_width;

	bool hovered;
	std::function<void()> on_click;

	~button() override = default;

	button(	std::string button_text,
		std::function<void()> on_click,
		float x_pos,
		float y_pos,
		float width,
		float height,
		float char_height,
		std::uint32_t rgba_color,
		std::uint32_t g_rgba_color,
		std::uint8_t base_point/*15 if gradient is disabled*/,
		std::uint8_t grad_point,
		std::uint8_t border_width,
		std::uint32_t rgba_background,
		std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color,
		std::uint32_t hovered_rgba_background,
		std::uint32_t hovered_rgba_border,
		single_text_line_settings* tip_stls,
		std::string tip_text = " ")
	{
		single_text_line_settings stls(button_text, x_pos, y_pos, char_height, rgba_color, g_rgba_color, base_point, grad_point);
		this->stl.reset(stls.create_one());
		if (tip_stls)
		{
			tip_stls->set_new_pos(x_pos, y_pos - height);
			this->tip.reset(tip_stls->create_one(tip_text));
		}
		this->border_width = border_width;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->width = width;
		this->height = height;
		this->rgba_color = rgba_color;
		this->rgba_border = rgba_border;
		this->rgba_background = rgba_background;
		this->hovered_rgba_color = hovered_rgba_color;
		this->hovered_rgba_border = hovered_rgba_border;
		this->hovered_rgba_background = hovered_rgba_background;
		this->hovered = false;
		this->on_click = std::move(on_click);
		this->enabled = true;
	}

	button(	std::string button_text,
		single_text_line_settings& button_text_stls,
		std::function<void()> on_click,
		float x_pos,
		float y_pos,
		float width,
		float height,
		std::uint8_t border_width,
		std::uint32_t rgba_background,
		std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color,
		std::uint32_t hovered_rgba_background,
		std::uint32_t hovered_rgba_border,
		single_text_line_settings* tip_stls,
		std::string tip_text = " ")
	{
		button_text_stls.set_new_pos(x_pos, y_pos);
		this->stl.reset(button_text_stls.create_one(button_text));
		if (tip_stls)
		{
			tip_stls->set_new_pos(x_pos, y_pos - height);
			this->tip.reset(tip_stls->create_one(tip_text));
		}

		this->border_width = border_width;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->width = width;
		this->height = height;
		this->rgba_color = button_text_stls.rgba_color;
		this->rgba_border = rgba_border;
		this->rgba_background = rgba_background;
		this->hovered_rgba_color = hovered_rgba_color;
		this->hovered_rgba_border = hovered_rgba_border;
		this->hovered_rgba_background = hovered_rgba_background;
		this->hovered = false;
		this->on_click = std::move(on_click);
		this->enabled = true;
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn/*-1 left, 1 right, 0 move*/, char state /*-1 down, 1 up*/) override
	{
		std::lock_guard locker(lock);

		if (!enabled) [[unlikely]]
			return false;

		mx = x_pos - mx;
		my = y_pos - my;
		if (hovered) [[likely]]
		{
			if (fabsf(mx) > width * 0.5f || fabsf(my) > height * 0.5f)
			{
				hovered = false;
				stl->safe_color_change(rgba_color);
			}
			else
			{
				SetCursor(hand_cursor);
				if (button_btn && state == 1)
				{
					if (button_btn == -1 && on_click)
						on_click();
					return true;
				}
			}
		}
		else
		{
			if (fabsf(mx) <= width * 0.5f && fabsf(my) <= height * 0.5f)
			{
				hovered = true;
				stl->safe_color_change(hovered_rgba_color);
			}
		}
		return false;
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		if (tip)
			tip->safe_move(dx, dy);

		stl->safe_move(dx, dy);

		x_pos += dx;
		y_pos += dy;
	}

	void keyboard_handler(char ch) override
	{
		return;
	}

	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);

		this->stl->safe_string_replace(new_string);
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
			) * width,
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * height;

		safe_change_position(new_x + cw, new_y + ch);
	}

	void draw() override
	{
		std::lock_guard locker(lock);

		if (!enabled) [[unlikely]]
			return;

		if (hovered) [[likely]]
		{
			if ((std::uint8_t)hovered_rgba_background)
			{
				__glcolor(hovered_rgba_background);
				glBegin(GL_QUADS);
				glVertex2f(x_pos - width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos - 0.5f * height);
				glVertex2f(x_pos - width * 0.5f, y_pos - 0.5f * height);
				glEnd();
			}
			if ((std::uint8_t)hovered_rgba_border)
			{
				__glcolor(hovered_rgba_border);
				glLineWidth(border_width);
				glBegin(GL_LINE_LOOP);
				glVertex2f(x_pos - width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos - 0.5f * height);
				glVertex2f(x_pos - width * 0.5f, y_pos - 0.5f * height);
				glEnd();
			}
		}
		else
		{
			if ((std::uint8_t)rgba_background)
			{
				__glcolor(rgba_background);
				glBegin(GL_QUADS);
				glVertex2f(x_pos - width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos - 0.5f * height);
				glVertex2f(x_pos - width * 0.5f, y_pos - 0.5f * height);
				glEnd();
			}
			if ((std::uint8_t)rgba_border)
			{
				__glcolor(rgba_border);
				glLineWidth(border_width);
				glBegin(GL_LINE_LOOP);
				glVertex2f(x_pos - width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos + 0.5f * height);
				glVertex2f(x_pos + width * 0.5f, y_pos - 0.5f * height);
				glVertex2f(x_pos - width * 0.5f, y_pos - 0.5f * height);
				glEnd();
			}
		}

		if (tip && hovered)
			tip->draw();

		stl->draw();
	}

	[[nodiscard]] inline std::uint32_t tell_type() override
	{
		return TT_BUTTON;
	}
};

#endif // !SAFGUIF_BUTTON
