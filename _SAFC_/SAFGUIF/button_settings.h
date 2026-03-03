#pragma once
#ifndef SAFGUIF_BS
#define SAFGUIF_BS

#include <functional>
#include <memory>

#include "button.h"

struct button_settings
{
	std::string button_text, tip_text;
	std::function<void()> on_click;
	float x_pos, y_pos, width, height, char_height;
	std::uint32_t rgba_color, g_rgba_color, rgba_background, rgba_border, hovered_rgba_color, hovered_rgba_background, hovered_rgba_border;
	std::uint8_t base_point, grad_point, border_width;
	bool stls_based_settings;

	single_text_line_settings* tip;  // non-owning
	single_text_line_settings* stls; // non-owning (or owned via owned_stls)
	std::unique_ptr<single_text_line_settings> owned_stls; // only set when constructed from button*

	// Full-gradient constructor
	button_settings(std::string button_text, std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, float char_height,
		std::uint32_t rgba_color, std::uint32_t g_rgba_color,
		std::uint8_t base_point, std::uint8_t grad_point, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border,
		single_text_line_settings* tip, std::string tip_text)
	{
		this->stls_based_settings = false;
		this->stls = nullptr;
		this->button_text = std::move(button_text);
		this->tip_text = std::move(tip_text);
		this->on_click = std::move(on_click);
		this->tip = tip;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->width = width;
		this->height = height;
		this->char_height = char_height;
		this->rgba_color = rgba_color;
		this->g_rgba_color = g_rgba_color;
		this->base_point = base_point;
		this->grad_point = grad_point;
		this->border_width = border_width;
		this->rgba_background = rgba_background;
		this->rgba_border = rgba_border;
		this->hovered_rgba_color = hovered_rgba_color;
		this->hovered_rgba_background = hovered_rgba_background;
		this->hovered_rgba_border = hovered_rgba_border;
	}

	// STLS-based constructor
	button_settings(std::string button_text, single_text_line_settings* button_text_stls,
		std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border,
		single_text_line_settings* tip, std::string tip_text = " ")
	{
		this->stls_based_settings = true;
		this->button_text = std::move(button_text);
		this->tip_text = std::move(tip_text);
		this->on_click = std::move(on_click);
		this->tip = tip;
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->width = width;
		this->height = height;
		this->stls = button_text_stls;
		this->border_width = border_width;
		this->g_rgba_color = 0;
		this->base_point = 0;
		this->grad_point = 0;
		this->rgba_color = button_text_stls->rgba_color;
		this->rgba_background = rgba_background;
		this->rgba_border = rgba_border;
		this->hovered_rgba_color = hovered_rgba_color;
		this->hovered_rgba_background = hovered_rgba_background;
		this->hovered_rgba_border = hovered_rgba_border;
	}

	// Convenience overloads (delegate to full-gradient ctor)
	button_settings(std::string button_text, std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, float char_height,
		std::uint32_t rgba_color, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border,
		single_text_line_settings* tip, std::string tip_text)
		: button_settings(button_text, on_click, x_pos, y_pos, width, height, char_height,
			rgba_color, 0, 15, 15, border_width, rgba_background, rgba_border,
			hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, tip, tip_text) {}

	button_settings(std::string button_text, single_text_line_settings* button_text_stls,
		std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(button_text, button_text_stls, on_click, x_pos, y_pos, width, height, border_width,
			rgba_background, rgba_border, hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	button_settings(std::string button_text, std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, float char_height,
		std::uint32_t rgba_color, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(button_text, on_click, x_pos, y_pos, width, height, char_height,
			rgba_color, 0, 15, 15, border_width, rgba_background, rgba_border,
			hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	button_settings(single_text_line_settings* button_text_stls, std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(" ", button_text_stls, on_click, x_pos, y_pos, width, height, border_width,
			rgba_background, rgba_border, hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	button_settings(std::string button_text, std::function<void()> on_click,
		float x_pos, float y_pos, float width, float height, float char_height,
		std::uint32_t rgba_color, std::uint32_t g_rgba_color,
		std::uint8_t base_point, std::uint8_t grad_point, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(button_text, on_click, x_pos, y_pos, width, height, char_height,
			rgba_color, g_rgba_color, base_point, grad_point, border_width,
			rgba_background, rgba_border, hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	button_settings(single_text_line_settings* button_text_stls,
		float x_pos, float y_pos, float width, float height, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(" ", button_text_stls, nullptr, x_pos, y_pos, width, height, border_width,
			rgba_background, rgba_border, hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	button_settings(float x_pos, float y_pos, float width, float height, float char_height,
		std::uint32_t rgba_color, std::uint8_t border_width,
		std::uint32_t rgba_background, std::uint32_t rgba_border,
		std::uint32_t hovered_rgba_color, std::uint32_t hovered_rgba_background, std::uint32_t hovered_rgba_border)
		: button_settings(" ", nullptr, x_pos, y_pos, width, height, char_height,
			rgba_color, 0, 15, 15, border_width, rgba_background, rgba_border,
			hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, nullptr, " ") {}

	// Copy-from-button constructor (allocates owned STLS)
	explicit button_settings(button* example, bool keep_text = false)
	{
		this->stls_based_settings = true;
		this->button_text = keep_text ? example->stl->current_text : " ";
		if (this->button_text.empty()) this->button_text = " ";

		this->tip_text = " ";
		this->tip = nullptr;

		this->on_click = example->on_click;
		this->x_pos = example->x_pos;
		this->y_pos = example->y_pos;
		this->width = example->width;
		this->height = example->height;
		this->hovered_rgba_color = example->hovered_rgba_color;
		this->rgba_color = example->rgba_color;
		this->owned_stls = std::make_unique<single_text_line_settings>(example->stl.get());
		this->stls = owned_stls.get();
		this->g_rgba_color = this->stls->g_rgba_color;
		this->border_width = example->border_width;
		this->rgba_background = example->rgba_background;
		this->rgba_border = example->rgba_border;
		this->hovered_rgba_background = example->hovered_rgba_background;
		this->hovered_rgba_border = example->hovered_rgba_border;
	}

	void move(float dx, float dy)
	{
		x_pos += dx;
		y_pos += dy;
	}

	void change_position(float new_x, float new_y)
	{
		new_x -= x_pos;
		new_y -= y_pos;
		move(new_x, new_y);
	}

	[[nodiscard]] std::unique_ptr<button> create_one(const std::string& btn_text, bool keep_text = false)
	{
		if (stls && stls_based_settings)
			return std::make_unique<button>(keep_text ? button_text : btn_text, stls, on_click,
				x_pos, y_pos, width, height, border_width,
				rgba_background, rgba_border,
				hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, tip, tip_text);
		else
			return std::make_unique<button>(keep_text ? button_text : btn_text, on_click,
				x_pos, y_pos, width, height, char_height,
				rgba_color, g_rgba_color, base_point, grad_point, border_width,
				rgba_background, rgba_border,
				hovered_rgba_color, hovered_rgba_background, hovered_rgba_border, tip, tip_text);
	}
};

using button_settings = button_settings;

#endif // !SAFGUIF_BS
