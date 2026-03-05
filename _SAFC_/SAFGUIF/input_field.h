#pragma once
#ifndef SAFGUIF_IF
#define SAFGUIF_IF

#include <memory>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct input_field : handleable_ui_part
{
	enum class pass_chars_type
	{
		pass_numbers        = 0b1,
		pass_first_point    = 0b10,
		pass_front_minus    = 0b100,
		pass_all            = 0xFF
	};

	enum class Type
	{
		NaturalNumbers  = (int)pass_chars_type::pass_numbers,
		WholeNumbers    = (int)pass_chars_type::pass_numbers | (int)pass_chars_type::pass_front_minus,
		FP_PositiveNumbers = (int)pass_chars_type::pass_numbers | (int)pass_chars_type::pass_first_point,
		FP_Any          = (int)pass_chars_type::pass_numbers | (int)pass_chars_type::pass_first_point | (int)pass_chars_type::pass_front_minus,
		text            = (int)pass_chars_type::pass_all,
	};

	_Align input_align, tip_align_val;
	Type input_type;
	std::string current_string, default_string;
	std::string* output_source;
	std::uint32_t max_chars, border_rgba_color;
	float x_pos, y_pos, height, width;
	bool focused, first_input;
	std::unique_ptr<single_text_line> stl;
	std::unique_ptr<single_text_line> tip;

	input_field(
		std::string default_str,
		float x_pos,
		float y_pos,
		float height,
		float width,
		single_text_line_settings* default_string_settings,
		std::string* output_source,
		std::uint32_t border_rgba_color,
		single_text_line_settings* tip_line_settings = nullptr,
		std::string tip_line_text = " ",
		std::uint32_t max_chars = 0,
		_Align input_align = _Align::left,
		_Align tip_align = _Align::center,
		Type input_type = Type::text)
	{
		this->default_string = default_str;
		default_string_settings->set_new_pos(x_pos, y_pos);
		this->stl.reset(default_string_settings->create_one(default_str));
		if (tip_line_settings)
		{
			this->tip.reset(tip_line_settings->create_one(tip_line_text));
			this->tip->safe_change_position_argumented(tip_align, x_pos - ((tip_align == _Align::left) ? 0.5f : ((tip_align == _Align::right) ? -0.5f : 0)) * width, y_pos - height);
		}

		this->input_align = input_align;
		this->input_type = input_type;
		this->tip_align_val = tip_align;

		this->max_chars = max_chars;
		this->border_rgba_color = border_rgba_color;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->height = (default_string_settings->y_unit_size * 2 > height) ? default_string_settings->y_unit_size * 2 : height;
		this->width = width;
		this->current_string.clear();
		this->first_input = this->focused = false;
		this->output_source = output_source;
	}

	~input_field() override = default;

	[[nodiscard]] bool mouse_handler(float mx, float my, char button/*-1 left, 1 right, 0 move*/, char state /*-1 down, 1 up*/) override
	{
		if (!enabled)
			return false;

		if (abs(mx - x_pos) < 0.5f * width && abs(my - y_pos) < 0.5f * height)
		{
			if (!focused)
				focus_change();
			return (bool)button;
		}
		else
		{
			if (focused)
				focus_change();
			return false;
		}
	}

	inline static bool check_string_on_type(const std::string& string, input_field::Type check_type)
	{
		bool first_symb = true;
		bool met_minus = false;
		bool met_point = false;
		for (const auto& ch : string)
		{
			switch (check_type)
			{
				case input_field::Type::NaturalNumbers:
					if (!(ch >= '0' && ch <= '9'))
						return false;
					break;
				case input_field::Type::WholeNumbers:
					if (!((ch >= '0' && ch <= '9')))
					{
						if (first_symb && !met_minus && ch == '-')
							met_minus = true;
						else
							return false;
					}
					break;
				case input_field::Type::FP_PositiveNumbers:
					if (!((ch >= '0' && ch <= '9')))
					{
						if (!met_point && ch == '.')
							met_point = true;
						else
							return false;
					}
					break;
				case input_field::Type::FP_Any:
					if (!((ch >= '0' && ch <= '9')))
					{
						if (!met_point && ch == '.')
							met_point = true;
						else if (first_symb && !met_minus && ch == '-')
							met_minus = true;
						else
							return false;
					}
					break;
				case input_field::Type::text:
					break;
				default:
					break;
			}
			first_symb = false;
		}

		return !first_symb;
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		stl->safe_move(dx, dy);
		if (tip)
			tip->safe_move(dx, dy);

		x_pos += dx;
		y_pos += dy;
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);

		new_x = x_pos - new_x;
		new_y = y_pos - new_y;

		safe_move(new_x, new_y);
	}

	void focus_change()
	{
		std::lock_guard locker(lock);

		this->focused = !this->focused;
		border_rgba_color = (((~(border_rgba_color >> 8)) << 8) | (border_rgba_color & 0xFF));
	}

	void update_input_string(const std::string& new_string = "")
	{
		std::lock_guard locker(lock);

		if (new_string.size())
			current_string.clear();

		float x = x_pos - ((input_align == _Align::left) ? 1 : ((input_align == _Align::right) ? -1 : 0)) * (0.5f * width - stl->x_unit_size);

		this->stl->safe_string_replace(new_string.size() ? new_string.substr(0, this->max_chars) : current_string);
		this->stl->safe_change_position_argumented(input_align, x, y_pos);
	}

	void backspace()
	{
		std::lock_guard locker(lock);

		process_first_input();
		if (current_string.size())
		{
			current_string.pop_back();
			update_input_string();
		}
		else
			this->stl->safe_string_replace(" ");
	}

	void flush_current_string_without_gui_update(bool set_default = false)
	{
		std::lock_guard locker(lock);

		this->current_string = set_default ? this->default_string : "";
	}

	void put_into_source(std::string* another_source = nullptr)
	{
		std::lock_guard locker(lock);

		if (output_source)
		{
			if (current_string.size())
				*output_source = current_string;
		}
		else if (another_source && current_string.size())
			*another_source = current_string;
	}

	void process_first_input()
	{
		std::lock_guard locker(lock);

		if (first_input)
		{
			first_input = false;
			current_string.clear();
		}
	}

	[[nodiscard]] std::string get_current_input(const std::string& replacement)
	{
		if (input_field::check_string_on_type(current_string, input_type))
			return current_string;

		if (stl && input_field::check_string_on_type(stl->current_text, input_type))
			return stl->current_text;

		if (input_field::check_string_on_type(replacement, input_type))
			return replacement;

		return "0";
	}

	void keyboard_handler(char ch) override
	{
		std::lock_guard locker(lock);

		if (!enabled)
			return;
		if (!focused)
			return;

		if (ch >= 32)
		{
			if ((int)input_type & (int)pass_chars_type::pass_numbers)
			{
				if (ch >= '0' && ch <= '9')
				{
					input_char(ch);
					return;
				}
			}
			if ((int)input_type & (int)pass_chars_type::pass_front_minus)
			{
				if (ch == '-' && current_string.empty())
				{
					input_char(ch);
					return;
				}
			}
			if ((int)input_type & (int)pass_chars_type::pass_first_point)
			{
				if (ch == '.' && current_string.find('.') >= current_string.size())
				{
					input_char(ch);
					return;
				}
			}
			if (((int)input_type & (int)pass_chars_type::pass_all) == (int)pass_chars_type::pass_all)
				input_char(ch);
		}
		else if (ch == 13)
			put_into_source();
		else if (ch == 8)
			backspace();
	}

	void input_char(char ch)
	{
		std::lock_guard locker(lock);

		process_first_input();

		if (!max_chars || current_string.size() < max_chars)
		{
			current_string.push_back(ch);
			update_input_string();
		}
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

	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);

		current_string = new_string.substr(0, this->max_chars);

		update_input_string(new_string);
		first_input = true;
	}

	void draw() override
	{
		std::lock_guard locker(lock);

		if (!enabled)
			return;

		__glcolor(border_rgba_color);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x_pos + 0.5f * width, y_pos + 0.5f * height);
		glVertex2f(x_pos - 0.5f * width, y_pos + 0.5f * height);
		glVertex2f(x_pos - 0.5f * width, y_pos - 0.5f * height);
		glVertex2f(x_pos + 0.5f * width, y_pos - 0.5f * height);
		glEnd();

		this->stl->draw();

		if (focused && tip)
			tip->draw();
	}

	[[nodiscard]] inline std::uint32_t tell_type() override
	{
		return TT_INPUT_FIELD;
	}
};

#endif // !SAFGUIF_IF
