#pragma once
#ifndef SAFGUIF_TEXTBOX
#define SAFGUIF_TEXTBOX

#include <algorithm>
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
	single_text_line_settings& stls;

	std::uint8_t border_width;
	std::uint32_t rgba_border, rgba_background;
	float available_text_width = 0.f;

	~text_box() override
	{
		std::lock_guard locker(lock);

		lines.clear();
	}

	text_box(
		std::string txt,
		single_text_line_settings& stls,
		float x_pos,
		float y_pos,
		float height,
		float width,
		float vertical_offset,
		std::uint32_t rgba_background,
		std::uint32_t rgba_border,
		std::uint8_t border_width,
		_Align text_align = _Align::left,
		VerticalOverflow v_overflow = VerticalOverflow::cut): 
			stls(stls)
	{
		this->text_align = text_align;
		this->v_overflow = v_overflow;
		this->border_width = border_width;
		this->vertical_offset = vertical_offset;
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

		lines.clear();

		std::vector<std::vector<std::string>> split_text;
		std::vector<std::string> paragraph_words;
		std::string word;

		stls.set_new_pos(x_pos, y_pos + 0.5f * height + 0.5f * vertical_offset);

		for (int i = 0; i < (int)text.size(); i++)
		{
			if (text[i] == ' ')
			{
				paragraph_words.push_back(word);
				word.clear();
			}
			else if (text[i] == '\n')
			{
				paragraph_words.push_back(word);
				word.clear();
				split_text.push_back(paragraph_words);
				paragraph_words.clear();
			}
			else
			{
				word.push_back(text[i]);
			}
			if (i == (int)text.size() - 1)
			{
				paragraph_words.push_back(word);
				split_text.push_back(paragraph_words);
				word.clear();
				paragraph_words.clear();
			}
		}

		const float max_line_width = std::max(available_text_width, 0.f);
		auto measure_line_width = [&](const std::string& candidate) -> float
		{
			if (candidate.empty())
				return 0.f;
			std::unique_ptr<single_text_line> temp_line(stls.create_one(candidate));
			return temp_line->calculated_width;
		};
		auto max_chunk_length = [&](const std::string& source) -> size_t
		{
			if (source.empty())
				return 0;
			size_t low = 1;
			size_t high = source.size();
			size_t best = 0;

			while (low <= high)
			{
				size_t mid = (low + high) / 2;
				float width = measure_line_width(std::string(source.data(), mid));
				if (width <= max_line_width)
				{
					best = mid;
					low = mid + 1;
				}
				else
				{
					if (mid == 0)
						break;
					high = mid - 1;
				}
			}

			return best ? best : 1;
		};

		std::vector<std::string> formatted_lines;
		std::string current_line;

		for (const auto& para : split_text)
		{
			for (const auto& para_word : para)
			{
				std::string candidate = current_line;
				if (!candidate.empty())
					candidate.push_back(' ');
				candidate += para_word;

				if (measure_line_width(candidate) <= max_line_width)
				{
					current_line = std::move(candidate);
					continue;
				}

				if (!current_line.empty())
				{
					formatted_lines.push_back(current_line);
					current_line.clear();
				}

				current_line = para_word;
				while (!current_line.empty() && measure_line_width(current_line) > max_line_width)
				{
					size_t chunk_len = max_chunk_length(current_line);
					formatted_lines.emplace_back(current_line.substr(0, chunk_len));
					current_line.erase(0, chunk_len);
					while (!current_line.empty() && current_line.front() == ' ')
						current_line.erase(0, 1);
				}
			}

			formatted_lines.push_back(current_line);
			current_line.clear();
		}

		for (const auto& formatted_line : formatted_lines)
		{
			stls.move(0, 0 - vertical_offset);
			if (v_overflow == VerticalOverflow::cut && stls.cy_pos < y_pos - height)
				break;

			auto& new_line = lines.emplace_back(stls.create_one(formatted_line));

			if (text_align == _Align::right)
				new_line->safe_change_position_argumented(GLOBAL_RIGHT, ((x_pos) + (0.5f * width) - stls.x_unit_size), lines.back()->cy_pos);
			else if (text_align == _Align::left)
				new_line->safe_change_position_argumented(GLOBAL_LEFT, ((x_pos) - (0.5f * width) + stls.x_unit_size), lines.back()->cy_pos);
		}

		if (!lines.empty())
			calculated_text_height = (lines.front()->cy_pos - lines.back()->cy_pos) + lines.front()->calculated_height;
		else
			calculated_text_height = 0.f;

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
		available_text_width = std::max(width + (stls.x_unit_size * 2.f), 0.f);
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		x_pos += dx;
		y_pos += dy;
		
		stls.move(dx, dy);

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
};

#endif
