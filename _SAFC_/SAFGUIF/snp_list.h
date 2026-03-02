#pragma once
#ifndef SAFGUIF_SNPLIST
#define SAFGUIF_SNPLIST

#include <deque>
#include <algorithm>
#include <functional>
#include <memory>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "button_settings.h"

struct selectable_properted_list : handleable_ui_part
{
	inline static constexpr int arrow_stick_height = 10;

	_Align text_in_buttons_align;
	std::function<void(int)> on_select;
	std::function<void(int)> on_get_properties;
	float header_cx_pos, header_y_pos, calculated_height, space_between, width;
	button_settings* butt_settings; // non-owning
	// Backward-compat aliases
	float& Width = width;
	float& HeaderCXPos = header_cx_pos;
	float& HeaderYPos = header_y_pos;
	std::deque<std::string> selectors_text;
	std::deque<std::unique_ptr<button>> selectors;
	std::deque<std::uint32_t> selected_id;
	// More backward-compat aliases (must come after their referenced members)
	std::deque<std::string>& SelectorsText = selectors_text;
	std::deque<std::uint32_t>& SelectedID = selected_id;
	std::uint32_t max_visible_lines, current_top_line_id, max_chars_in_line;
	std::uint8_t top_arrow_hovered, bottom_arrow_hovered;

	~selectable_properted_list() override
	{
		std::lock_guard locker(lock);
		selectors.clear();
	}

	selectable_properted_list(button_settings* butt_settings,
		std::function<void(int)> on_select, std::function<void(int)> on_get_properties,
		float header_cx_pos, float header_y_pos, float width, float space_between,
		std::uint32_t max_chars_in_line = 0, std::uint32_t max_visible_lines = 0,
		_Align text_in_buttons_align = _Align::left)
	{
		this->max_chars_in_line = max_chars_in_line;
		this->max_visible_lines = max_visible_lines;
		this->butt_settings = butt_settings;
		this->on_select = std::move(on_select);
		this->on_get_properties = std::move(on_get_properties);
		this->header_cx_pos = header_cx_pos;
		this->width = width;
		this->space_between = space_between;
		this->header_y_pos = header_y_pos;
		this->current_top_line_id = 0;
		this->text_in_buttons_align = text_in_buttons_align;
		this->bottom_arrow_hovered = this->top_arrow_hovered = false;
		this->calculated_height = 0;
	}

	void recalculate_current_height()
	{
		std::lock_guard locker(lock);
		calculated_height = space_between * selectors.size();
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);
		top_arrow_hovered = bottom_arrow_hovered = 0;
		if (fabsf(mx - header_cx_pos) < 0.5f * width && my < header_y_pos && my > header_y_pos - calculated_height)
		{
			if (button_btn == 2 /*UP*/)
			{
				if (state == -1)
					safe_rotate_list(-3);
			}
			else if (button_btn == 3 /*DOWN*/)
			{
				if (state == -1)
					safe_rotate_list(3);
			}
		}
		if (max_visible_lines && selectors.size() < selectors_text.size())
		{
			if (fabsf(mx - header_cx_pos) < 0.5f * width)
			{
				if (my > header_y_pos && my < header_y_pos + arrow_stick_height)
				{
					top_arrow_hovered = 1;
					if (button_btn == -1 && state == -1)
					{
						safe_rotate_list(-1);
						return true;
					}
				}
				else if (my < header_y_pos - calculated_height && my > header_y_pos - calculated_height - arrow_stick_height)
				{
					bottom_arrow_hovered = 1;
					if (button_btn == -1 && state == -1)
					{
						safe_rotate_list(1);
						return true;
					}
				}
			}
		}
		if (button_btn)
		{
			bool flag = true;
			for (int i = 0; i < (int)selectors.size(); i++)
			{
				if (selectors[i]->mouse_handler(mx, my, button_btn, state) && flag)
				{
					if (button_btn == -1)
					{
						if (shift_held)
							selected_id.clear();
						auto it = std::find(selected_id.begin(), selected_id.end(), i + current_top_line_id);
						if (it == selected_id.end())
							selected_id.push_back(i + current_top_line_id);
						else
							selected_id.erase(it);
						if (on_select)
							on_select(i + current_top_line_id);
					}
					if (button_btn == 1)
					{
						if (on_get_properties)
							on_get_properties(i + current_top_line_id);
					}
					flag = false;
					return true;
				}
			}
		}
		else
		{
			for (int i = 0; i < (int)selectors.size(); i++)
				selectors[i]->mouse_handler(mx, my, 0, 0);
		}
		for (auto id : selected_id)
			if (id < selectors_text.size())
				if (id >= current_top_line_id && id < current_top_line_id + max_visible_lines)
					selectors[id - current_top_line_id]->mouse_handler(selectors[id - current_top_line_id]->x_pos, selectors[id - current_top_line_id]->y_pos, 0, 0);
		return false;
	}

	void safe_string_replace(std::string new_string) override
	{
		std::lock_guard locker(lock);
		safe_string_replace(new_string, 0xFFFFFFFF);
	}

	void safe_string_replace(const std::string& new_string, std::uint32_t line_id)
	{
		std::lock_guard locker(lock);
		if (line_id == 0xFFFFFFFF)
		{
			safe_string_replace(new_string, selected_id.front());
		}
		else
		{
			selectors_text[line_id] = new_string;
			if (line_id > current_top_line_id && line_id - current_top_line_id < max_visible_lines)
				selectors[line_id - current_top_line_id]->safe_string_replace(new_string);
		}
	}

	void safe_update_lines()
	{
		std::lock_guard locker(lock);
		while (selectors_text.size() < selectors.size())
			selectors.pop_back();
		if (current_top_line_id + max_visible_lines > selectors_text.size())
		{
			if (selectors_text.size() >= max_visible_lines) current_top_line_id = selectors_text.size() - max_visible_lines;
			else current_top_line_id = 0;
		}
		for (int i = 0; i < (int)selectors.size(); i++)
		{
			if (i + current_top_line_id < selectors_text.size())
				selectors[i]->safe_string_replace(
					(max_chars_in_line) ?
					(selectors_text[i + current_top_line_id].substr(0, max_chars_in_line))
					:
					(selectors_text[i + current_top_line_id])
				);
		}
		reset_align_all(text_in_buttons_align);
	}

	[[nodiscard]] bool is_resizeable() override { return true; }

	void safe_resize(float new_height, float new_width) override
	{
		std::lock_guard locker(lock);
		float dcx = (new_width - width) * 0.5f;
		header_cx_pos += dcx;
		float old_width = width;
		width = new_width;
		float lines = std::floor(new_height / space_between);
		int difference = (int)lines - (int)max_visible_lines;

		float y_space = butt_settings->stls->space_width + 2 * butt_settings->stls->x_unit_size;
		int new_max_chars = (int)(width / y_space);
		int important_diff = (int)(old_width / y_space) - (int)max_chars_in_line;
		max_chars_in_line = new_max_chars - important_diff;

		if (!selectors.empty())
		{
			if (difference < 0)
			{
				while (difference && !selectors.empty())
				{
					selectors.pop_back();
					if (current_top_line_id && selectors.size() + current_top_line_id == selectors_text.size())
						current_top_line_id--;
					difference++;
				}
			}
			else if (difference > 0)
			{
				butt_settings->height = space_between;
				butt_settings->width = new_width;
				butt_settings->on_click = nullptr;
				while (difference && selectors.size() + current_top_line_id < selectors_text.size() + 1)
				{
					butt_settings->change_position(header_cx_pos, header_y_pos - (selectors.size() + 0.5f) * space_between);
					selectors.push_back(butt_settings->create_one(selectors_text[selectors.size() + current_top_line_id - 1]));
					difference--;
				}
			}
		}

		for (auto& btn : selectors)
		{
			btn->width = width;
			btn->safe_change_position(header_cx_pos, btn->y_pos);
		}
		max_visible_lines = (std::uint32_t)lines;
		safe_update_lines();
		recalculate_current_height();
	}

	void safe_rotate_list(std::int32_t delta)
	{
		std::lock_guard locker(lock);
		if (!max_visible_lines)
			return;
		if (delta < 0 && current_top_line_id < (std::uint32_t)(0 - delta))
			current_top_line_id = 0;
		else if (delta > 0 && current_top_line_id + delta + max_visible_lines > selectors_text.size())
			current_top_line_id = (std::uint32_t)selectors_text.size() - max_visible_lines;
		else current_top_line_id += delta;
		safe_update_lines();
	}

	void safe_remove_string_by_id(std::uint32_t id)
	{
		std::lock_guard locker(lock);
		if (id >= selectors_text.size()) return;
		if (selectors_text.empty()) return;
		if (max_visible_lines)
		{
			if (id < current_top_line_id)
				current_top_line_id--;
			else if (id == current_top_line_id)
				if (current_top_line_id == selectors_text.size() - 1)
					current_top_line_id--;
		}
		selectors_text.erase(selectors_text.begin() + id);
		safe_update_lines();
		selected_id.clear();
	}

	void remove_selected()
	{
		std::lock_guard locker(lock);
		std::sort(selected_id.begin(), selected_id.end());
		while (!selected_id.empty())
		{
			if (max_visible_lines)
			{
				if (selected_id.back() < current_top_line_id)
					current_top_line_id--;
				else if (selected_id.back() == current_top_line_id)
					if (current_top_line_id == selectors_text.size() - 1)
						current_top_line_id--;
			}
			selectors_text.erase(selectors_text.begin() + selected_id.back());
			selected_id.pop_back();
		}
		safe_update_lines();
		selected_id.clear();
	}

	void reset_align_for(std::uint32_t id, _Align align)
	{
		std::lock_guard locker(lock);
		if (id >= selectors.size()) return;
		float nx = header_cx_pos - ((align == _Align::left) ? 0.5f : ((align == _Align::right) ? -0.5f : 0)) * (width - space_between);
		selectors[id]->stl->safe_change_position_argumented(align, nx, selectors[id]->y_pos);
	}

	void reset_align_all(_Align align)
	{
		std::lock_guard locker(lock);
		if (align == _Align::center) return;
		float nx = header_cx_pos - ((align == _Align::left) ? 0.5f : ((align == _Align::right) ? -0.5f : 0)) * (width - space_between);
		for (int i = 0; i < (int)selectors.size(); i++)
			selectors[i]->stl->safe_change_position_argumented(align, nx, selectors[i]->y_pos);
	}

	void safe_push_back_new_string(std::string button_text)
	{
		std::lock_guard locker(lock);
		if (max_chars_in_line) button_text = button_text.substr(0, max_chars_in_line);
		selectors_text.push_back(button_text);
		if (max_visible_lines && selectors_text.size() > max_visible_lines)
		{
			safe_update_lines();
			return;
		}
		butt_settings->change_position(header_cx_pos, header_y_pos - (selectors_text.size() - 0.5f) * space_between);
		butt_settings->height = space_between;
		butt_settings->width = width;
		butt_settings->on_click = nullptr;
		selectors.push_back(butt_settings->create_one(button_text));
		recalculate_current_height();
		reset_align_for((std::uint32_t)selectors_text.size() - 1, text_in_buttons_align);
	}

	void push_strings(std::vector<std::string> strings)
	{
		std::lock_guard locker(lock);
		for (auto& s : strings)
			safe_push_back_new_string(s);
	}

	void push_strings(std::initializer_list<std::string> strings)
	{
		std::lock_guard locker(lock);
		for (auto& s : strings)
			safe_push_back_new_string(s);
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
				) * calculated_height;
		safe_change_position(new_x + cw, new_y - 0.5f * calculated_height + ch);
	}

	void keyboard_handler(char) override {}

	void safe_change_position(float new_cx_pos, float new_header_y_pos) override
	{
		std::lock_guard locker(lock);
		new_cx_pos -= header_cx_pos;
		new_header_y_pos -= header_y_pos;
		safe_move(new_cx_pos, new_header_y_pos);
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		header_cx_pos += dx;
		header_y_pos += dy;
		for (auto& btn : selectors)
			btn->safe_move(dx, dy);
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		if (selectors.size() < selectors_text.size())
		{
			// TOP BAR
			if (top_arrow_hovered)
				__glcolor(butt_settings->hovered_rgba_border);
			else
				__glcolor(butt_settings->rgba_border);
			glBegin(GL_QUADS);
			glVertex2f(header_cx_pos - 0.5f * width, header_y_pos);
			glVertex2f(header_cx_pos + 0.5f * width, header_y_pos);
			glVertex2f(header_cx_pos + 0.5f * width, header_y_pos + arrow_stick_height);
			glVertex2f(header_cx_pos - 0.5f * width, header_y_pos + arrow_stick_height);
			// BOTTOM BAR
			if (bottom_arrow_hovered)
				__glcolor(butt_settings->hovered_rgba_border);
			else
				__glcolor(butt_settings->rgba_border);
			glVertex2f(header_cx_pos - 0.5f * width, header_y_pos - calculated_height);
			glVertex2f(header_cx_pos + 0.5f * width, header_y_pos - calculated_height);
			glVertex2f(header_cx_pos + 0.5f * width, header_y_pos - calculated_height - arrow_stick_height);
			glVertex2f(header_cx_pos - 0.5f * width, header_y_pos - calculated_height - arrow_stick_height);
			glEnd();
			// TOP ARROW
			if (top_arrow_hovered)
			{
				if (butt_settings->hovered_rgba_color & 0xFF)
					__glcolor(butt_settings->hovered_rgba_color);
				else
					__glcolor(butt_settings->rgba_color);
			}
			else
				__glcolor(butt_settings->rgba_color);
			glBegin(GL_TRIANGLES);
			glVertex2f(header_cx_pos, header_y_pos + 9 * arrow_stick_height / 10);
			glVertex2f(header_cx_pos + arrow_stick_height * 0.5f, header_y_pos + arrow_stick_height / 10);
			glVertex2f(header_cx_pos - arrow_stick_height * 0.5f, header_y_pos + arrow_stick_height / 10);
			// BOTTOM ARROW
			if (bottom_arrow_hovered)
			{
				if (butt_settings->hovered_rgba_color & 0xFF)
					__glcolor(butt_settings->hovered_rgba_color);
				else
					__glcolor(butt_settings->rgba_color);
			}
			else
				__glcolor(butt_settings->rgba_color);
			glVertex2f(header_cx_pos, header_y_pos - calculated_height - 9 * arrow_stick_height / 10);
			glVertex2f(header_cx_pos + arrow_stick_height * 0.5f, header_y_pos - calculated_height - arrow_stick_height / 10);
			glVertex2f(header_cx_pos - arrow_stick_height * 0.5f, header_y_pos - calculated_height - arrow_stick_height / 10);
			glEnd();
		}
		for (auto& btn : selectors)
			btn->draw();
	}

	[[nodiscard]] inline std::uint32_t tell_type() override { return TT_SELPROPLIST; }

	// Backward-compat method wrappers
	void SafePushBackNewString(std::string s) { safe_push_back_new_string(std::move(s)); }
	void RemoveSelected() { remove_selected(); }
	void SafeRemoveStringByID(std::uint32_t id) { safe_remove_string_by_id(id); }
	void SafeUpdateLines() { safe_update_lines(); }
};

using SelectablePropertedList = selectable_properted_list;

#endif // !SAFGUIF_SNPLIST
