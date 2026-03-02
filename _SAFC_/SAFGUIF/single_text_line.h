#pragma once
#ifndef SAFGUIF_STL
#define SAFGUIF_STL

#include <memory>
#include "header_utils.h"
#include "symbols.h"


struct single_text_line
{
	std::string current_text;
	float cx_pos, cy_pos;
	float space_width;
	std::uint32_t rgba_color, g_rgba_color;
	float calculated_width, calculated_height;
	bool is_bicolored, is_listed_font;
	float x_unit_size, y_unit_size;
	std::vector<std::unique_ptr<dotted_symbol>> chars;
	// Backward-compat aliases
	float& CXpos = cx_pos;
	float& CYpos = cy_pos;
	std::string& _CurrentText = current_text;

	~single_text_line() = default;
	single_text_line(const single_text_line&) = delete;

	single_text_line(std::string text, float cx_pos, float cy_pos, float x_unit_sz, float y_unit_sz, float space_width, std::uint8_t line_width = 2, std::uint32_t rgba_color = 0xFFFFFFFF, std::optional<std::uint32_t> g_rgba_color_opt = std::nullopt, std::uint8_t orig_n_grad_points = ((5 << 4) | 5), bool is_listed_font = false)
	{
		if (!text.size()) text = " ";
		this->current_text = text;
		calculated_height = 2 * y_unit_sz;
		calculated_width = text.size() * 2.f * x_unit_sz + (text.size() - 1) * space_width;
		this->cx_pos = cx_pos;
		this->cy_pos = cy_pos;
		this->rgba_color = rgba_color;
		this->g_rgba_color = 0;
		if (g_rgba_color_opt)
			this->g_rgba_color = *g_rgba_color_opt;
		this->space_width = space_width;
		this->x_unit_size = x_unit_sz;
		this->y_unit_size = y_unit_sz;
		this->is_listed_font = is_listed_font;
		float char_x_position = cx_pos - (calculated_width * 0.5f) + x_unit_sz, char_x_pos_increment = 2.f * x_unit_sz + space_width;
		for (int i = 0; i < (int)text.size(); i++)
		{
			if (!g_rgba_color_opt && !is_listed_font)
				chars.push_back(std::make_unique<dotted_symbol>(text[i], char_x_position, cy_pos, x_unit_sz, y_unit_sz, line_width, rgba_color));
			else if (g_rgba_color_opt && !is_listed_font)
				chars.push_back(std::make_unique<bi_colored_dotted_symbol>(text[i], char_x_position, cy_pos, x_unit_sz, y_unit_sz, line_width, rgba_color, this->g_rgba_color, (std::uint8_t)(orig_n_grad_points >> 4), (std::uint8_t)(orig_n_grad_points & 0xF)));
			else
				chars.push_back(std::make_unique<lfontsymbol>(text[i], char_x_position, cy_pos, x_unit_sz, y_unit_sz, rgba_color));
			char_x_position += char_x_pos_increment;
		}
		if (g_rgba_color_opt)
		{
			this->is_bicolored = true;
			this->g_rgba_color = *g_rgba_color_opt;
		}
		else
			this->is_bicolored = false;

		recalculate_width();
	}

	void safe_color_change(std::uint32_t new_rgba_color)
	{
		if (is_bicolored)
		{
			safe_color_change(new_rgba_color, g_rgba_color,
				static_cast<bi_colored_dotted_symbol*>(chars.front().get())->_point_data >> 4,
				static_cast<bi_colored_dotted_symbol*>(chars.front().get())->_point_data & 0xF
			);
			return;
		}
		std::uint8_t r = (new_rgba_color >> 24), g = (new_rgba_color >> 16) & 0xFF, b = (new_rgba_color >> 8) & 0xFF, a = (new_rgba_color) & 0xFF;
		rgba_color = new_rgba_color;
		for (auto& ch : chars)
		{
			ch->R = r;
			ch->G = g;
			ch->B = b;
			ch->A = a;
		}
	}
	void safe_color_change(std::uint32_t new_base_rgba_color, std::uint32_t new_g_rgba_color, std::uint8_t base_point, std::uint8_t g_point)
	{
		if (!is_bicolored)
			return safe_color_change(new_base_rgba_color);
		for (auto& ch : chars)
			ch->refill_gradient(new_base_rgba_color, new_g_rgba_color, base_point, g_point);
	}
	void safe_change_position(float new_cx_pos, float new_cy_pos)
	{
		new_cx_pos = new_cx_pos - cx_pos;
		new_cy_pos = new_cy_pos - cy_pos;
		safe_move(new_cx_pos, new_cy_pos);
	}
	void safe_move(float dx, float dy)
	{
		cx_pos += dx;
		cy_pos += dy;
		for (auto& ch : chars)
			ch->safe_char_move(dx, dy);
	}
	bool safe_replace_char(int i, char ch_val)
	{
		if (i >= (int)chars.size()) return false;
		if (is_listed_font)
		{
			auto ch = dynamic_cast<lfontsymbol*>(chars[i].get());
			if (!ch)
			{
				std::cerr << "Error during safe char replacement [1]" << std::endl;
				return false;
			}
			ch->symb = ch_val;
			ch->reinit_glyph_metrics();
		}
		else
		{
			chars[i]->render_way = ASCII[ch_val];
			chars[i]->update_point_placement_positions();
		}
		return true;
	}
	bool safe_replace_char(int i, const std::string& ch_render_way)
	{
		if (i >= (int)chars.size()) return false;
		if (is_listed_font) return false;
		chars[i]->render_way = ch_render_way;
		chars[i]->update_point_placement_positions();
		return true;
	}
	void recalculate_width()
	{
		calculated_width = is_listed_font ?
			horizontally_reposition_fonted_symbols() :
			legacy_calculate_width_and_reposition_nonfonteds();
	}
	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y)
	{
		float cw = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & arg))
			) * calculated_width,
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * calculated_height;
		safe_change_position(new_x + cw, new_y + ch);
	}
	void safe_string_replace(std::string new_string)
	{
		if (!new_string.size()) new_string = " ";
		current_text = new_string;
		while (new_string.size() > chars.size())
		{
			if (is_bicolored)
				chars.push_back(std::make_unique<bi_colored_dotted_symbol>(*static_cast<bi_colored_dotted_symbol*>(chars.front().get())));
			else if (is_listed_font)
				chars.push_back(std::make_unique<lfontsymbol>(*static_cast<lfontsymbol*>(chars.front().get())));
			else
				chars.push_back(std::make_unique<dotted_symbol>(*chars.front()));
		}
		while (new_string.size() < chars.size())
			chars.pop_back();
		for (int i = 0; i < (int)chars.size(); i++)
			safe_replace_char(i, new_string[i]);
		recalculate_width();
	}

	inline float default_width_formulae()
	{
		return chars.size() * (2.f * x_unit_size) + (chars.size() - 1) * space_width;
	}

	float legacy_calculate_width_and_reposition_nonfonteds()
	{
		auto width = default_width_formulae();
		float char_x_position = cx_pos - (width * 0.5f) + x_unit_size, char_x_pos_increment = 2.f * x_unit_size + space_width;
		for (auto& ch : chars)
		{
			ch->x_pos = char_x_position;
			char_x_position += char_x_pos_increment;
		}
		return width;
	}

	float horizontally_reposition_fonted_symbols()
	{
		float pixel_size = (internal_range * 2) / window_base_width;
		float total_width = 0;
		ptrdiff_t total_pixel_width = 0;

		for (auto& ch : chars)
		{
			auto fonted_symb = dynamic_cast<lfontsymbol*>(ch.get());
			if (!fonted_symb)
			{
				std::cerr << "Error during repositionment of a character [1]" << std::endl;
				return total_pixel_width * pixel_size;
			}
			total_pixel_width += fonted_symb->gm.gmCellIncX;
		}

		if (chars.size())
		{
			auto last_char = dynamic_cast<lfontsymbol*>(chars.back().get());
			if (!last_char)
			{
				std::cerr << "Error during repositionment of a character [2]" << std::endl;
				return total_pixel_width * pixel_size;
			}
			total_pixel_width -= ((ptrdiff_t)last_char->gm.gmCellIncX - (ptrdiff_t)last_char->gm.gmBlackBoxX);
		}

		total_width = total_pixel_width * pixel_size;
		ptrdiff_t linear_pixel_h_pos = 0;
		float linear_h_pos = cx_pos - (total_width * 0.5f);

		for (auto& ch : chars)
		{
			auto fonted_symb = dynamic_cast<lfontsymbol*>(ch.get());
			fonted_symb->safe_position_change(
				linear_h_pos + (linear_pixel_h_pos * pixel_size),
				fonted_symb->y_pos);
			linear_pixel_h_pos += fonted_symb->gm.gmCellIncX;
		}
		return total_width;
	}

	void draw()
	{
		if (calculated_width < std::numeric_limits<float>::epsilon())
			recalculate_width();
		for (auto& ch : chars)
			ch->draw();
	}

	// Backward-compat method wrappers
	void SafeMove(float dx, float dy) { safe_move(dx, dy); }
	void SafeStringReplace(std::string s) { safe_string_replace(std::move(s)); }
	void SafeChangePosition(float x, float y) { safe_change_position(x, y); }
	void SafeChangePosition_Argumented(std::uint8_t a, float x, float y) { safe_change_position_argumented(a, x, y); }
};

#endif
