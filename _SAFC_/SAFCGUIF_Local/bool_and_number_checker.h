#pragma once

#ifndef SAFGUIF_L_BAWC
#define SAFGUIF_L_BAWC

#include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/include_all.h"

template<typename bool_type, typename number_type>
struct bool_and_number_checker : handleable_ui_part
{
	float x_pos, y_pos;
	bool_type* flag;
	number_type* number;
	std::unique_ptr<single_text_line> stl_info;

	bool_and_number_checker(float x_pos, float y_pos, single_text_line_settings* stls,
		bool_type* flag, number_type* number)
	{
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->flag = flag;
		this->number = number;
		stls->set_new_pos(x_pos, y_pos + 40);
		this->stl_info.reset(stls->create_one("_"));
	}

	~bool_and_number_checker() override = default;

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
		stl_info->safe_move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		new_x -= x_pos;
		new_y -= y_pos;
		safe_move(new_x, new_y);
	}

	void safe_change_position_argumented(std::uint8_t, float, float) override
	{
		return;
	}

	void keyboard_handler(char) override
	{
		return;
	}

	void safe_string_replace(std::string) override
	{
		return;
	}

	void update_info()
	{
		std::lock_guard locker(lock);
		if (number)
		{
			std::string t = std::to_string(*number);
			if (t != stl_info->current_text)
				stl_info->safe_string_replace(t);
		}
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		update_info();
		if (flag)
		{
			if (*flag)
				special_signs::draw_ok(x_pos, y_pos, 15, 0x00FFFFFF);
			else
				special_signs::draw_wait(x_pos, y_pos, 15, 0x007FFFFF, 20);
		}
		else special_signs::draw_no(x_pos, y_pos, 15, 0xFF0000FF);
		stl_info->draw();
	}

	[[nodiscard]] bool mouse_handler(float, float, char, char) override
	{
		return false;
	}
};

#endif
