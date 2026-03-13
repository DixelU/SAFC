#pragma once
#ifndef SAF_EBOX
#define SAF_EBOX

#include "textbox.h"

#include <deque>
#include <vector>
#include <utility>
#include <algorithm>
#include <limits>
#include <memory>

constexpr int eb_cursor_flash_fraction = 90;
constexpr int eb_cursor_flash_subcycle = eb_cursor_flash_fraction / 2;

// doesn't work with fonted mode anymore!
struct edit_box : handleable_ui_part
{
private:
	using char_pos = std::pair<std::deque<std::unique_ptr<single_text_line>>::iterator, size_t>;
public:
private:
	float available_line_width() const
	{
		float char_width = stls->x_unit_size * 2;
		return std::max(width - 2 * char_width, 0.f);
	}

	float measure_string_width(const std::string& candidate) const
	{
		if (candidate.empty())
			return 0.f;
		std::unique_ptr<single_text_line> probe(stls->create_one(candidate));
		return probe->calculated_width;
	}

	size_t max_chars_that_fit(const std::string& source, float max_width) const
	{
		if (source.empty() || max_width <= std::numeric_limits<float>::epsilon())
			return 1;

		size_t low = 1;
		size_t high = source.size();
		size_t best = 0;

		while (low <= high)
		{
			size_t mid = (low + high) / 2;
			float width = measure_string_width(source.substr(0, mid));

			if (width <= max_width)
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
	}

	void split_word_if_needed(char_pos& cursor, float max_width)
	{
		if (cursor.first == words.end())
			return;

		auto it = cursor.first;
		while (it != words.end())
		{
			auto& word_line = *it;
			const std::string current_text = word_line->current_text;
			if (current_text == "\n" || current_text.empty())
				break;

			float width = measure_string_width(current_text);
			if (width <= max_width)
				break;

			size_t fit_len = max_chars_that_fit(current_text, max_width);
			std::string remainder = current_text.substr(fit_len);
			word_line->safe_string_replace(current_text.substr(0, fit_len));
			auto inserted = words.insert(it + 1, std::unique_ptr<single_text_line>(stls->create_one(remainder)));

			if (cursor.first == it && cursor.second >= fit_len)
			{
				cursor.first = inserted;
				cursor.second = cursor.second - fit_len;
			}

			it = inserted;
		}
	}

	std::string buffered_cur_text;
	std::uint8_t visibility_countdown;
public:
	std::deque<std::unique_ptr<single_text_line>> words;
	char_pos cursor_position; // points at the symbol after the cursor in current word
	single_text_line_settings* stls; // non-owning
	float x_pos, y_pos, height, width, vertical_offset;
	std::uint32_t rgba_background, rgba_border;
	std::uint8_t border_width;

	edit_box(std::string text,
		single_text_line_settings* stls,
		float x_pos,
		float y_pos,
		float height,
		float width,
		float vertical_offset,
		std::uint32_t rgba_background,
		std::uint32_t rgba_border,
		std::uint8_t border_width):
			stls(stls),
			x_pos(x_pos),
			y_pos(y_pos),
			height(height),
			width(width),
			vertical_offset(vertical_offset),
			rgba_background(rgba_background),
			rgba_border(rgba_border),
			border_width(border_width),
			buffered_cur_text(text),
			visibility_countdown(0)
	{
		words.push_back(std::unique_ptr<single_text_line>(stls->create_one(" ")));

		cursor_position = { words.begin(), 0 };
		rearrange_positions();

		for (auto& ch : text)
			write_symbol_at_cursor_pos(ch);
	}

	void write_symbol_at_cursor_pos(char ch, bool rearrange = true)
	{
		std::lock_guard locker(lock);

		std::string cur_word = (cursor_position.first != words.end()) ? (*cursor_position.first)->current_text : "";

		float fixed_width = available_line_width();

		if (cursor_position.second)
		{
			if (ch == '\n' || ch == ' ')
			{
				std::string str = " ";
				str[0] = ch;
				(*cursor_position.first)->safe_string_replace(cur_word.substr(0, cursor_position.second));
				cursor_position.first = words.insert(cursor_position.first + 1, std::unique_ptr<single_text_line>(stls->create_one(str)));
				cursor_position.first = words.insert(cursor_position.first + 1, std::unique_ptr<single_text_line>(stls->create_one(cur_word.substr(cursor_position.second, 0x7FFFFFFF))));
				cursor_position.second = 0;
			}
			else if (ch < 32 || ch == 127)
			{
				return;
			}
			else
			{
				cur_word.insert(cur_word.begin() + cursor_position.second, ch);
				(*cursor_position.first)->safe_string_replace(cur_word);
				cursor_position.second++;
			}
		}
		else
		{
			if (ch == '\n' || ch == ' ')
			{
				std::string str = " ";
				str[0] = ch;
				cursor_position.first = words.insert(cursor_position.first, std::unique_ptr<single_text_line>(stls->create_one(str)));
				++cursor_position.first;
				cursor_position.second = 0;
			}
			else if (ch < 32 || ch == 127)
				return;
			else
			{
				cur_word.insert(cur_word.begin(), ch);
				(*cursor_position.first)->safe_string_replace(cur_word);
				cursor_position.second++;
			}
		}

		split_word_if_needed(cursor_position, fixed_width);
		visibility_countdown = eb_cursor_flash_subcycle;
		if (rearrange)
			rearrange_positions();
	}

	void remove_symbol_before_cursor_pos()
	{
		std::lock_guard locker(lock);

		if (cursor_position.second)
		{
			std::string cur_word = (*cursor_position.first)->current_text;
			cur_word.erase(cur_word.begin() + cursor_position.second - 1);
			(*cursor_position.first)->safe_string_replace(cur_word);
			cursor_position.second--;
			rearrange_positions();
		}
		else if (cursor_position.first != words.begin())
		{
			auto it = cursor_position.first - 1;
			if ((*it)->current_text.size() > 1)
			{
				std::string cur_word = (*it)->current_text;
				cur_word.pop_back();
				(*it)->safe_string_replace(cur_word);
			}
			else
			{
				cursor_position.first = words.erase(it);
			}

			rearrange_positions();
		}
	}

	void remove_symbol_after_cursor_pos()
	{
		std::lock_guard locker(lock);

		if (cursor_position.first == words.end() - 1 && cursor_position.second == words.back()->current_text.size() - 1)
			return;

		move_cursor_by1(_Align::right);
		remove_symbol_before_cursor_pos();
	}

	void update_buffered_cur_text()
	{
		std::lock_guard locker(lock);

		buffered_cur_text.clear();
		for (auto& word : words)
		{
			if (word->current_text != "\t")
				buffered_cur_text += word->current_text;
		}
	}

	void rearrange_positions()
	{
		float char_width = stls->x_unit_size * 2;
		float char_height = vertical_offset;
		float fixed_height = height - vertical_offset;
		float top_line = y_pos + fixed_height * 0.5f;
		float inner_width = std::max(width - char_width, 0.f);
		float left_line = x_pos - inner_width * 0.5f;
		float available_width = inner_width;
		float cursor = 0.f;
		size_t line_no = 0;

		for (auto& word : words)
		{
			const auto& text = word->current_text;
			if (text == "\n")
			{
				float newline_center = left_line + cursor + stls->x_unit_size;
				float newline_y = top_line - char_height * line_no;
				word->safe_change_position_argumented(_Align::center, newline_center, newline_y);

				line_no++;
				cursor = 0.f;
				continue;
			}

			float word_width = word->calculated_width;
			if (word_width <= 0.f)
				continue;

			if (cursor + word_width > available_width && word_width < available_width)
			{
				line_no++;
				cursor = 0.f;
			}

			float center_x = left_line + cursor + word_width * 0.5f;
			float center_y = top_line - char_height * line_no;
			word->safe_change_position_argumented(_Align::center, center_x, center_y);

			cursor += word_width;
			bool only_whitespace = std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; });
			if (!only_whitespace)
				cursor += stls->space_width;
		}
	}

	void clear()
	{
		std::lock_guard locker(lock);

		words.clear();
		words.push_back(std::unique_ptr<single_text_line>(stls->create_one("\t")));
		words.push_back(std::unique_ptr<single_text_line>(stls->create_one("\n")));

		cursor_position = { words.begin(), 0 };
	}

	void reparse_current_text_from_scratch()
	{
		std::lock_guard locker(lock);

		clear();

		for (auto& ch : buffered_cur_text)
			write_symbol_at_cursor_pos(ch);
	}

	~edit_box() override
	{
		std::lock_guard locker(lock);

		words.clear();
	}

	[[nodiscard]] bool mouse_handler(float, float, char, char) override { return false; }

	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);

		buffered_cur_text = std::move(new_string);

		reparse_current_text_from_scratch();
	}

	[[nodiscard]] inline std::string unsafe_get_current_text() const
	{
		return buffered_cur_text;
	}

	void move_cursor_by1(_Align move)
	{
		std::lock_guard locker(lock);

		if (cursor_position.first == words.end())
			return;

		auto y = cursor_position.first;

		auto cur_y_coord = (*y)->cy_pos;
		auto cur_x_coord = (*y)->chars[cursor_position.second]->x_pos;

		switch (move)
		{
			case _Align::left:
			{
				if (cursor_position.second > 0)
					--cursor_position.second;
				else if (cursor_position.first != words.begin())
				{
					--cursor_position.first;
					cursor_position.second = (*cursor_position.first)->current_text.size() - 1;
				}
				break;
			}
			case _Align::right:
			{
				if (cursor_position.second < (*cursor_position.first)->current_text.size() - 1)
					++cursor_position.second;
				else if (cursor_position.first != words.end() - 1)
				{
					++cursor_position.first;
					cursor_position.second = 0;
				}
				break;
			}
			case _Align::top:
			{
				float start_y = (*cursor_position.first)->cy_pos;
				do { move_cursor_by1(_Align::left); }
				while (cursor_position.first != words.begin() &&
					((*cursor_position.first)->current_text == "\n" || (*cursor_position.first)->cy_pos == start_y));
				break;
			}
			case _Align::bottom:
			{
				float start_y = (*cursor_position.first)->cy_pos;
				do { move_cursor_by1(_Align::right); }
				while (cursor_position.first != words.end() - 1 &&
					((*cursor_position.first)->current_text == "\n" || (*cursor_position.first)->cy_pos == start_y));
				break;
			}
			case _Align::top | _Align::left: // HOME
			{
				if (cursor_position.second > 0)
				{
					cursor_position.second = 0;
					break;
				}

				auto it = cursor_position.first;
				while (it != words.begin())
				{
					--it;
					if ((*it)->current_text == "\n")
					{
						++it;
						cursor_position.first = it;
						cursor_position.second = 0;
						break;
					}
					cursor_position.first = it;
					cursor_position.second = 0;
				}
				break;
			}
			case _Align::top | _Align::right: // END
			{
				auto it = cursor_position.first;
				cursor_position.second = std::min(cursor_position.second, (*it)->current_text.size());
				while (it + 1 != words.end())
				{
					auto next = it + 1;
					if ((*next)->current_text == "\n")
						break;
					++it;
					cursor_position.first = it;
					cursor_position.second = (*it)->current_text.size();
				}
				break;
			}
			case _Align::center:
				break;
		}
	}

	void keyboard_handler(char ch) override
	{
		std::lock_guard locker(lock);

		switch (ch)
		{
			case 13:
				ch = '\n';
				visibility_countdown = eb_cursor_flash_subcycle;
				break;
			case 8:
				visibility_countdown = eb_cursor_flash_subcycle;
				remove_symbol_before_cursor_pos();
				return;
			case 127:
				visibility_countdown = eb_cursor_flash_subcycle;
				remove_symbol_after_cursor_pos();
				return;
			case 1:
				move_cursor_by1(_Align::bottom);
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
			case 2:
				move_cursor_by1(_Align::top);
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
			case 3:
				move_cursor_by1(_Align::left);
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
			case 4:
				move_cursor_by1(_Align::right);
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
			case 5:
				move_cursor_by1(static_cast<_Align>(_Align::top | _Align::left));
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
			case 6:
				move_cursor_by1(static_cast<_Align>(_Align::top | _Align::right));
				visibility_countdown = eb_cursor_flash_subcycle;
				return;
		}

		write_symbol_at_cursor_pos(ch);
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		x_pos += dx;
		y_pos += dy;

		stls->move(dx, dy);

		for (auto& word : words)
			word->safe_move(dx, dy);
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

		if (visibility_countdown)
			visibility_countdown--;

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

		if ((timer_v % eb_cursor_flash_fraction > eb_cursor_flash_subcycle) || visibility_countdown)
		{
			if (cursor_position.first != words.end())
			{
				const float fonted_fix = (is_fonted ? 0.75f : 1.5f);
				const auto& traced_symbol = (*cursor_position.first)->chars[cursor_position.second];

				glLineWidth(1);
				__glcolor(stls->rgba_color);
				glBegin(GL_LINES);
				glVertex2f(traced_symbol->x_pos + stls->x_unit_size - stls->space_width * 0.5f, traced_symbol->y_pos + stls->y_unit_size * fonted_fix);
				glVertex2f(traced_symbol->x_pos + stls->x_unit_size - stls->space_width * 0.5f, traced_symbol->y_pos - stls->y_unit_size * fonted_fix);
				glEnd();
			}
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

		float bottom_y = y_pos - 0.5f * height;
		for (auto& word : words)
		{
			if (word->cy_pos < bottom_y)
				break;
			word->draw();
		}
	}

	[[nodiscard]] inline std::uint32_t tell_type() override { return TT_EDITBOX; }
};

#endif
