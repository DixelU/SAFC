#pragma once
#ifndef SAFC_SIMPLE_PLAYER_VIEWER
#define SAFC_SIMPLE_PLAYER_VIEWER

#include "../SAFGUIF/handleable_ui_part.h"
#include "../SAFC_InnerModules/simple_player.h"

struct player_viewer : public handleable_ui_part
{
	float xpos, ypos;
	std::unique_ptr<simple_player::draw_data> data;

	player_viewer(float xpos, float ypos):
		xpos(xpos),
		ypos(ypos),
		data(std::make_unique<simple_player::draw_data>())
	{
		data->init(40, 22.5f, 0);
		data->move(xpos - 0.5f * data->width, ypos);
	}

	void draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(lock);
		player->draw(*data);
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(lock);
		xpos += dx;
		ypos += dy;
		data->move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard<std::recursive_mutex> locker(lock);
		safe_move(new_x - xpos, new_y - ypos);
	}

	void safe_change_position_argumented(std::uint8_t argument, float new_x, float new_y) override
	{
		std::lock_guard<std::recursive_mutex> locker(lock);
		const float centered_width = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & argument))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & argument))) * data->width;
		const float centered_height = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_BOTTOM & argument))
			- (std::int32_t)((bool)(GLOBAL_TOP & argument))) * data->height;
		safe_change_position(new_x + centered_width, new_y + centered_height);
	}

	void rescale_and_reposition(float new_x, float new_y, float new_width, float new_height)
	{
		std::lock_guard<std::recursive_mutex> locker(lock);
		const float width_factor_change = new_width / data->width;
		constexpr float black_relative_height = 22.5f / 40.f;
		xpos = new_x;
		ypos = new_y;
		data->reinit(new_width, new_height,
			data->last_keyboard_height * width_factor_change,
			data->last_keyboard_height * width_factor_change * black_relative_height, 0.f);
		data->move(new_x - 0.5f * data->width, new_y);
	}

	void keyboard_handler(char) override {}
	void safe_string_replace(std::string) override {}
	[[nodiscard]] bool mouse_handler(float, float, char, char) override { return false; }
};

#endif
