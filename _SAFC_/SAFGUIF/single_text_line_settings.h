#pragma once
#ifndef SAFGUIF_STLS
#define SAFGUIF_STLS

#include <memory>

#include "header_utils.h"
#include "single_text_line.h"

inline static constexpr float char_width_per_height_fonted = 0.666f;
inline static constexpr float char_width_per_height = 0.5f;

constexpr float char_space_between(float char_height) { return char_height / 2.f; }
inline float char_line_width(float char_height) { return std::ceil(char_height / 7.5f); }

struct single_text_line_settings
{
	std::string stl_string;
	float cx_pos, cy_pos, x_unit_size, y_unit_size;
	std::uint8_t base_point, grad_point, line_width, space_width;
	bool is_fonted;
	std::uint32_t default_rgba_c, default_g_rgba_c;
	std::uint32_t rgba_color, g_rgba_color;

	single_text_line_settings(std::string text, float cx_pos, float cy_pos, float x_unit_sz, float y_unit_sz, std::uint8_t line_width, std::uint8_t space_width, std::uint32_t rgba_color, std::uint32_t g_rgba_color, std::uint8_t base_point, std::uint8_t grad_point)
	{
		this->stl_string = std::move(text);
		this->cx_pos = cx_pos;
		this->cy_pos = cy_pos;
		this->x_unit_size = x_unit_sz;
		this->y_unit_size = y_unit_sz;
		this->default_rgba_c = this->rgba_color = rgba_color;
		this->default_g_rgba_c = this->g_rgba_color = g_rgba_color;
		this->line_width = line_width;
		this->space_width = space_width;
		this->base_point = base_point;
		this->grad_point = grad_point;
		this->is_fonted = false;
	}

	single_text_line_settings(std::string text, float cx_pos, float cy_pos, float x_unit_sz, float y_unit_sz, std::uint8_t line_width, std::uint8_t space_width, std::uint32_t rgba_color) :
		single_text_line_settings(text, cx_pos, cy_pos, x_unit_sz, y_unit_sz, line_width, space_width, rgba_color, 0, 255, 255) {}

	single_text_line_settings(std::string text, float cx_pos, float cy_pos, float char_height, std::uint32_t rgba_color, std::uint32_t g_rgba_color, std::uint8_t base_point, std::uint8_t grad_point) :
		single_text_line_settings(text, cx_pos, cy_pos, char_height * char_width_per_height / 2, char_height / 2, (std::uint8_t)char_line_width(char_height), (std::uint8_t)char_space_between(char_height), rgba_color, g_rgba_color, base_point, grad_point) {}

	single_text_line_settings(std::string text, float cx_pos, float cy_pos, float char_height, std::uint32_t rgba_color) :
		single_text_line_settings(text, cx_pos, cy_pos, char_height, rgba_color, 0, 255, 255) {}

	single_text_line_settings(float x_unit_sz, float y_unit_sz, std::uint32_t rgba_color) :
		single_text_line_settings("_", 0, 0, y_unit_sz, rgba_color)
	{
		this->is_fonted = true;
		this->x_unit_size = x_unit_sz;
		this->y_unit_size = y_unit_sz;
		this->default_rgba_c = this->rgba_color = rgba_color;
	}
	single_text_line_settings(float char_height, std::uint32_t rgba_color) :
		single_text_line_settings(char_height * char_width_per_height / 4, char_height / 2, rgba_color) {}

	single_text_line_settings(single_text_line* example, bool keep_text = false) :
		single_text_line_settings(
			keep_text ? example->current_text : " ", example->cx_pos, example->cy_pos, example->x_unit_size, example->y_unit_size,
			example->chars.front()->line_width, example->space_width,
			example->rgba_color, example->g_rgba_color,
			example->is_bicolored ? ((static_cast<bi_colored_dotted_symbol*>(example->chars.front().get())->_point_data & 0xF0) >> 4) : (std::uint8_t)0xF,
			example->is_bicolored ? (static_cast<bi_colored_dotted_symbol*>(example->chars.front().get())->_point_data & 0xF) : (std::uint8_t)0xF
		)
	{
		this->is_fonted = example->is_listed_font;
	}

	[[nodiscard]] single_text_line* create_one()
	{
		single_text_line* ptr = nullptr;
		if (grad_point & 0xF0 && !is_fonted)
			ptr = new single_text_line(stl_string, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color);
		else if (!is_fonted)
			ptr = new single_text_line(stl_string, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color, g_rgba_color, ((base_point & 0xF) << 4) | (grad_point & 0xF));
		else
			ptr = new single_text_line(stl_string, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color, std::nullopt, 0xF, true);
		rgba_color = default_rgba_c;
		g_rgba_color = default_g_rgba_c;
		return ptr;
	}

	[[nodiscard]] single_text_line* create_one(const std::string& text_override)
	{
		single_text_line* ptr = nullptr;
		if (grad_point & 0xF0 && !is_fonted)
			ptr = new single_text_line(text_override, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color);
		else if (!is_fonted)
			ptr = new single_text_line(text_override, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color, g_rgba_color, ((base_point & 0xF) << 4) | (grad_point & 0xF));
		else
			ptr = new single_text_line(text_override, cx_pos, cy_pos, x_unit_size, y_unit_size, space_width, line_width, rgba_color, std::nullopt, 0xF, true);
		rgba_color = default_rgba_c;
		g_rgba_color = default_g_rgba_c;
		return ptr;
	}

	void set_new_pos(float new_x_pos, float new_y_pos)
	{
		this->cx_pos = new_x_pos;
		this->cy_pos = new_y_pos;
	}

	void move(float dx, float dy)
	{
		this->cx_pos += dx;
		this->cy_pos += dy;
	}
};

#endif
