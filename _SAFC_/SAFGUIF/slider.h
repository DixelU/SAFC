#pragma once
#ifndef SAFGUIF_SLIDER
#define SAFGUIF_SLIDER

#include <functional>
#include <memory>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct slider : handleable_ui_part
{
	enum class Orientation { horizontal, vertical };

	std::unique_ptr<single_text_line> label;
	Orientation orientation;
	float x_pos, y_pos;
	float track_length;
	float handle_size;
	float track_thickness;

	std::uint32_t track_color;
	std::uint32_t handle_color;
	std::uint32_t handle_hover_color;
	std::uint32_t handle_drag_color;
	std::uint32_t handle_border_color;

	float min_value, max_value;
	float current_value;
	float normalized_position; // 0.0 to 1.0

	bool hovered;
	bool dragging;
	bool fire_on_release;
	float drag_offset_x, drag_offset_y;

	std::function<void(float)> on_value_change;

	~slider() override = default;

	slider(
		Orientation orientation,
		float x_pos, float y_pos,
		float track_length,
		float min_value, float max_value,
		float default_value,
		std::function<void(float)> on_value_change,
		std::uint32_t track_color = 0x808080FF,
		std::uint32_t handle_color = 0xFFFFFFFF,
		std::uint32_t handle_hover_color = 0xAAFFFFFF,
		std::uint32_t handle_drag_color = 0x00FFFFFF,
		std::uint32_t handle_border_color = 0x000000FF,
		float handle_size = 12.0f,
		float track_thickness = 4.0f,
		single_text_line_settings* label_stls = nullptr,
		std::string label_text = ""
	)
	{
		this->orientation = orientation;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->track_length = track_length;
		this->min_value = min_value;
		this->max_value = max_value;
		this->current_value = default_value;
		this->on_value_change = std::move(on_value_change);

		this->track_color = track_color;
		this->handle_color = handle_color;
		this->handle_hover_color = handle_hover_color;
		this->handle_drag_color = handle_drag_color;
		this->handle_border_color = handle_border_color;
		this->handle_size = handle_size;
		this->track_thickness = track_thickness;

		this->hovered = false;
		this->dragging = false;
		this->fire_on_release = false;
		this->drag_offset_x = 0.0f;
		this->drag_offset_y = 0.0f;

		if (max_value != min_value)
			this->normalized_position = (default_value - min_value) / (max_value - min_value);
		else
			this->normalized_position = 0.0f;

		this->normalized_position = std::clamp(this->normalized_position, 0.0f, 1.0f);

		if (label_stls && !label_text.empty())
		{
			float label_offset = track_length * 0.5f + handle_size + track_thickness;
			this->label.reset(label_stls->create_one(label_text));
			this->label->safe_change_position_argumented(
				orientation == Orientation::horizontal ? GLOBAL_LEFT : GLOBAL_TOP,
				x_pos + (orientation == Orientation::horizontal ? label_offset : 0),
				y_pos + (orientation == Orientation::vertical ? label_offset : 0)
			);
		}

		this->enabled = true;
	}

	void get_handle_position(float& hx, float& hy)
	{
		if (orientation == Orientation::horizontal)
		{
			hx = x_pos - track_length * 0.5f + normalized_position * track_length;
			hy = y_pos;
		}
		else
		{
			hx = x_pos;
			hy = y_pos - track_length * 0.5f + normalized_position * track_length;
		}
	}

	void draw() override
	{
		std::lock_guard locker(lock);

		if (!enabled)
			return;

		__glcolor(track_color);
		glLineWidth(track_thickness);
		glBegin(GL_LINES);
		if (orientation == Orientation::horizontal)
		{
			glVertex2f(x_pos - track_length * 0.5f, y_pos);
			glVertex2f(x_pos + track_length * 0.5f, y_pos);
		}
		else
		{
			glVertex2f(x_pos, y_pos - track_length * 0.5f);
			glVertex2f(x_pos, y_pos + track_length * 0.5f);
		}
		glEnd();

		float hx, hy;
		get_handle_position(hx, hy);

		std::uint32_t cur_handle_color = dragging ? handle_drag_color : (hovered ? handle_hover_color : handle_color);

		__glcolor(cur_handle_color);
		glBegin(GL_QUADS);
		glVertex2f(hx - handle_size * 0.5f, hy + handle_size * 0.5f);
		glVertex2f(hx + handle_size * 0.5f, hy + handle_size * 0.5f);
		glVertex2f(hx + handle_size * 0.5f, hy - handle_size * 0.5f);
		glVertex2f(hx - handle_size * 0.5f, hy - handle_size * 0.5f);
		glEnd();

		__glcolor(handle_border_color);
		glLineWidth(2.0f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(hx - handle_size * 0.5f, hy + handle_size * 0.5f);
		glVertex2f(hx + handle_size * 0.5f, hy + handle_size * 0.5f);
		glVertex2f(hx + handle_size * 0.5f, hy - handle_size * 0.5f);
		glVertex2f(hx - handle_size * 0.5f, hy - handle_size * 0.5f);
		glEnd();

		if (label)
			label->draw();
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button/*-1 left, 1 right, 0 move, 2 wheel up, 3 wheel down*/, char state /*-1 down, 1 up*/) override
	{
		std::lock_guard locker(lock);

		if (!enabled)
			return false;

		float hx, hy;
		get_handle_position(hx, hy);

		bool over_handle = (fabsf(mx - hx) <= handle_size * 0.5f && fabsf(my - hy) <= handle_size * 0.5f);

		if (dragging)
		{
			if (button == -1 && state == 1)
			{
				dragging = false;
				if (fire_on_release && on_value_change)
					on_value_change(current_value);
			}
			else
			{
				float new_pos;
				if (orientation == Orientation::horizontal)
					new_pos = mx - drag_offset_x - (x_pos - track_length * 0.5f);
				else
					new_pos = my - drag_offset_y - (y_pos - track_length * 0.5f);

				normalized_position = std::clamp(new_pos / track_length, 0.0f, 1.0f);

				float old_value = current_value;
				current_value = min_value + normalized_position * (max_value - min_value);

				if (!fire_on_release && on_value_change && old_value != current_value)
					on_value_change(current_value);

				SetCursor(hand_cursor);
			}
			return true;
		}
		else
		{
			if (over_handle)
			{
				hovered = true;
				SetCursor(hand_cursor);

				if (button == -1 && state == -1)
				{
					dragging = true;
					drag_offset_x = mx - hx;
					drag_offset_y = my - hy;
					return true;
				}
			}
			else
			{
				hovered = false;
			}
		}
		return false;
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		x_pos += dx;
		y_pos += dy;

		if (label)
			label->safe_move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);

		safe_move(new_x - x_pos, new_y - y_pos);
	}

	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y) override
	{
		std::lock_guard locker(lock);

		float cw = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & arg))
			) * (orientation == Orientation::horizontal ? track_length : 0),
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * (orientation == Orientation::vertical ? track_length : 0);

		safe_change_position(new_x + cw, new_y + ch);
	}

	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);

		if (label)
			label->safe_string_replace(new_string);
	}

	void keyboard_handler(char ch) override {}

	void set_value(float value, bool with_trigger = true)
	{
		std::lock_guard locker(lock);

		current_value = std::clamp(value, min_value, max_value);

		if (max_value != min_value)
			normalized_position = (current_value - min_value) / (max_value - min_value);
		else
			normalized_position = 0.0f;

		if (with_trigger && on_value_change)
			on_value_change(current_value);
	}

	[[nodiscard]] float get_value()
	{
		std::lock_guard locker(lock);

		return current_value;
	}

	[[nodiscard]] inline std::uint32_t tell_type() override
	{
		return TT_SLIDER;
	}
};

#endif // !SAFGUIF_SLIDER
