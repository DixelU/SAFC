#pragma once
#ifndef SAFGUIF_TEXTBOX
#define SAFGUIF_TEXTBOX

#include <memory>
#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

// todo: if i plan to continue using SAFGUIF - i'll have to refactor this mess

struct text_box : handleable_ui_part
{
	enum class VerticalOverflow { cut, display, recalibrate };
	_Align text_align;
	VerticalOverflow v_overflow;
	std::string text;
	std::vector<std::unique_ptr<single_text_line>> lines;
	float x_pos, y_pos;
	float width, height;
	float vertical_offset, calculated_text_height;
	single_text_line_settings* stls; // non-owning
	// Backward-compat aliases
	std::string& Text = text;
	float& Xpos = x_pos;
	float& Ypos = y_pos;
	std::uint8_t border_width;
	std::uint32_t rgba_border, rgba_background, symbols_per_line;

	~text_box() override
	{
		std::lock_guard locker(lock);
		lines.clear();
	}
	text_box(std::string txt, single_text_line_settings* stls, float x_pos, float y_pos, float height, float width, float vertical_offset, std::uint32_t rgba_background, std::uint32_t rgba_border, std::uint8_t border_width, _Align text_align = _Align::left, VerticalOverflow v_overflow = VerticalOverflow::cut)
	{
		this->text_align = text_align;
		this->v_overflow = v_overflow;
		this->border_width = border_width;
		this->vertical_offset = vertical_offset;
		this->stls = stls;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->width = width;
		this->rgba_border = rgba_border;
		this->rgba_background = rgba_background;
		this->height = height;
		this->text = txt.size() ? txt : " ";
		recalculate_available_space_for_text();
		text_reformat();
	}
	void text_reformat()
	{
		std::lock_guard locker(lock);
		std::vector<std::vector<std::string>> split_text;
		std::vector<std::string> paragraph;
		std::string line;
		stls->set_new_pos(x_pos, y_pos + 0.5f * height + 0.5f * vertical_offset);

		for (int i = 0; i < (int)text.size(); i++)
		{
			if (text[i] == ' ')
			{
				paragraph.push_back(line);
				line.clear();
			}
			else if (text[i] == '\n')
			{
				paragraph.push_back(line);
				line.clear();
				split_text.push_back(paragraph);
				paragraph.clear();
			}
			else line.push_back(text[i]);
			if (i == (int)text.size() - 1)
			{
				paragraph.push_back(line);
				split_text.push_back(paragraph);
				line.clear();
				paragraph.clear();
			}
		}

		for (int i = 0; i < (int)split_text.size(); i++)
		{
			auto& para = split_text[i];
			auto& lines_ref = paragraph;
			for (int q = 0; q < (int)para.size(); q++)
			{
				if ((para[q].size() + 1 + line.size()) < symbols_per_line)
				{
					if (line.size()) line = (line + " " + para[q]);
					else line = para[q];
				}
				else
				{
					if (line.size()) lines_ref.push_back(line);
					line = para[q];
				}
				while (line.size() >= symbols_per_line)
				{
					paragraph.emplace_back(line.substr(0, symbols_per_line));
					line = line.erase(0, symbols_per_line);
				}
			}
			paragraph.push_back(line);
			line.clear();
		}

		for (int i = 0; i < (int)paragraph.size(); i++)
		{
			stls->move(0, 0 - vertical_offset);
			if (v_overflow == VerticalOverflow::cut && stls->cy_pos < y_pos - height)
				break;

			auto& new_line = lines.emplace_back(stls->create_one(paragraph[i]));

			if (text_align == _Align::right)
				new_line->safe_change_position_argumented(GLOBAL_RIGHT, ((this->x_pos) + (0.5f * width) - this->stls->x_unit_size), lines.back()->cy_pos);
			else if (text_align == _Align::left)
				new_line->safe_change_position_argumented(GLOBAL_LEFT, ((this->x_pos) - (0.5f * width) + this->stls->x_unit_size), lines.back()->cy_pos);
		}
		calculated_text_height = (lines.front()->cy_pos - lines.back()->cy_pos) + lines.front()->calculated_height;

		if (v_overflow == VerticalOverflow::recalibrate)
		{
			float dy = (calculated_text_height - this->height);
			for (auto& line_ptr : lines)
				line_ptr->safe_move(0, (dy + lines.front()->calculated_height) * 0.5f);
		}
	}
	void safe_text_color_change(std::uint32_t new_color)
	{
		std::lock_guard locker(lock);
		for (auto& line_ptr : lines)
			line_ptr->safe_color_change(new_color);
	}
	[[nodiscard]] bool mouse_handler(float mx, float my, char button/*-1 left, 1 right, 0 move*/, char state /*-1 down, 1 up*/) override
	{
		return false;
	}
	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);
		this->text = new_string.size() ? new_string : " ";
		lines.clear();
		recalculate_available_space_for_text();
		text_reformat();
	}
	void keyboard_handler(char ch) override
	{
		return;
	}
	void recalculate_available_space_for_text()
	{
		std::lock_guard locker(lock);
		symbols_per_line = std::floor((width + stls->x_unit_size * 2) / (stls->x_unit_size * 2 + stls->space_width));
	}
	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
		stls->move(dx, dy);
		for (auto& line_ptr : lines)
			line_ptr->safe_move(dx, dy);
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
		if ((std::uint8_t)rgba_background)
		{
			__glcolor(rgba_background);
			glBegin(GL_QUADS);
			glVertex2f(x_pos - (width * 0.5f), y_pos + (0.5f * height));
			glVertex2f(x_pos + (width * 0.5f), y_pos + (0.5f * height));
			glVertex2f(x_pos + (width * 0.5f), y_pos - (0.5f * height));
			glVertex2f(x_pos - (width * 0.5f), y_pos - (0.5f * height));
			glEnd();
		}
		if ((std::uint8_t)rgba_border)
		{
			__glcolor(rgba_border);
			glLineWidth(border_width);
			glBegin(GL_LINE_LOOP);
			glVertex2f(x_pos - (width * 0.5f), y_pos + (0.5f * height));
			glVertex2f(x_pos + (width * 0.5f), y_pos + (0.5f * height));
			glVertex2f(x_pos + (width * 0.5f), y_pos - (0.5f * height));
			glVertex2f(x_pos - (width * 0.5f), y_pos - (0.5f * height));
			glEnd();
		}
		for (auto& line_ptr : lines)
			line_ptr->draw();
	}
	[[nodiscard]] inline std::uint32_t tell_type() override
	{
		return TT_TEXTBOX;
	}
	void SafeTextColorChange(std::uint32_t c) { safe_text_color_change(c); }
};

// Backward-compat alias
using TextBox = text_box;

#endif
