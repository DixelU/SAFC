#pragma once
#ifndef SAF_EBOX
#define SAF_EBOX

#include "textbox.h"

#include <deque>
#include <vector>
#include <utility>
#include <algorithm>
#include <memory>

constexpr int eb_cursor_flash_fraction = 30;
constexpr int eb_cursor_flash_subcycle = eb_cursor_flash_fraction / 2;

// doesn't work with fonted mode anymore!
struct edit_box : handleable_ui_part
{
private:
	using char_pos = std::pair<std::deque<std::unique_ptr<single_text_line>>::iterator, size_t>;
public:
private:
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

		float char_width = stls->x_unit_size * 2;
		float fixed_width = (width - 2 * char_width);

		size_t maximal_whole_word_size = (size_t)(fixed_width / (char_width + stls->space_width));

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
			else if (cur_word.size() >= maximal_whole_word_size)
			{
				cur_word.insert(cur_word.begin() + cursor_position.second, ch);
				(*cursor_position.first)->safe_string_replace(cur_word.substr(0, maximal_whole_word_size - 1));
				cursor_position.first = words.insert(cursor_position.first + 1, std::unique_ptr<single_text_line>(stls->create_one(cur_word.substr(maximal_whole_word_size - 1, 0x7FFFFFFF))));
				cursor_position.second++;
				if (cursor_position.second < maximal_whole_word_size)
					--cursor_position.first;
				else
					cursor_position.second -= maximal_whole_word_size - 1;
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
			else if (cur_word.size() >= maximal_whole_word_size)
			{
				cur_word.insert(cur_word.begin() + 1, ch);
				(*cursor_position.first)->safe_string_replace(cur_word.substr(0, maximal_whole_word_size - 1));
				cursor_position.first = words.insert(cursor_position.first + 1, std::unique_ptr<single_text_line>(stls->create_one(cur_word.substr(maximal_whole_word_size - 1, 0x7FFFFFFF))));
				cursor_position.second++;
				--cursor_position.first;
			}
			else
			{
				cur_word.insert(cur_word.begin(), ch);
				(*cursor_position.first)->safe_string_replace(cur_word);
				cursor_position.second++;
			}
		}

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
		float space_width = stls->space_width;
		float fixed_width = (width - char_width);
		float top_line = y_pos + fixed_height * 0.5f;
		float left_line = x_pos - fixed_width * 0.5f;
		size_t line_no = 0, col_no = 1;

		for (auto& word : words)
		{
			if (word->current_text == "\n")
			{
				col_no++;
				word->safe_change_position_argumented(
					_Align::right | _Align::center,
					left_line + col_no * char_width + (col_no - 1) * space_width,
					top_line - char_height * line_no);

				line_no++;
				col_no = 1;
				continue;
			}
			else if (word->calculated_width + col_no * char_width + (col_no - 1) * space_width > fixed_width)
			{
				line_no++;
				col_no = word->chars.size();
			}
			else
			{
				col_no += word->chars.size();
			}

			word->safe_change_position_argumented(_Align::right | _Align::center, left_line + col_no * char_width + (col_no - 1) * space_width, top_line - char_height * line_no);
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
				do { move_cursor_by1(_Align::left); }
				while (
					!(cursor_position.first == words.begin() && cursor_position.second == 0) && (
						cur_x_coord + stls->x_unit_size * 0.5f < (*cursor_position.first)->chars[cursor_position.second]->x_pos ||
						cur_y_coord == (*cursor_position.first)->cy_pos
						));
				break;
			}
			case _Align::bottom:
			{
				do { move_cursor_by1(_Align::right); }
				while (
					!(cursor_position.first == words.end() - 1 && cursor_position.second == words.back()->current_text.size() - 1) && (
						cur_x_coord - stls->x_unit_size * 0.5f > (*cursor_position.first)->chars[cursor_position.second]->x_pos ||
						cur_y_coord == (*cursor_position.first)->cy_pos
						));
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
				auto& t = (*cursor_position.first)->chars[cursor_position.second];
				__glcolor(stls->rgba_color);
				glBegin(GL_LINES);
				glVertex2f(t->x_pos - stls->x_unit_size - stls->space_width * 0.5f, t->y_pos + stls->y_unit_size * fonted_fix);
				glVertex2f(t->x_pos - stls->x_unit_size - stls->space_width * 0.5f, t->y_pos - stls->y_unit_size * fonted_fix);
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
