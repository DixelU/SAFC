#pragma once
#ifndef SAFGUIF_WVC
#define SAFGUIF_WVC

#include <functional>
#include <memory>

#include "header_utils.h"
#include "input_field.h"

struct wheel_variable_changer : handleable_ui_part
{
	enum class Type { exponential, addictable };
	enum class Sensitivity { on_enter, on_click, on_wheel };

	Type type;
	Sensitivity sen;
	std::unique_ptr<input_field> var_if, fac_if;
	float width, height;
	float x_pos, y_pos;
	double variable;
	double factor;
	bool is_hovered, wheel_field_hovered;
	std::function<void(double)> on_apply;

	~wheel_variable_changer() override = default;

	wheel_variable_changer(std::function<void(double)> on_apply, float x_pos, float y_pos, double default_var, double default_fact,
		single_text_line_settings* stls, std::string var_string = " ", std::string fac_string = " ",
		Type type = Type::exponential)
		: width(100), height(50)
	{
		this->on_apply = std::move(on_apply);
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->variable = default_var;
		this->factor = default_fact;
		this->is_hovered = wheel_field_hovered = false;
		this->type = type;
		this->sen = Sensitivity::on_wheel;
		var_if.reset(new input_field(std::to_string(default_var).substr(0, 8), x_pos - 25.f, y_pos + 15, 10, 40, stls, nullptr, 0x007FFFFF, stls, var_string, 8, _Align::center, _Align::center, input_field::Type::FP_PositiveNumbers));
		fac_if.reset(new input_field(std::to_string(default_fact).substr(0, 8), x_pos - 25.f, y_pos - 10, 10, 40, stls, nullptr, 0x007FFFFF, stls, fac_string, 8, _Align::center, _Align::center, input_field::Type::FP_PositiveNumbers));
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		__glcolor(0xFFFFFF3F + wheel_field_hovered * 0x3F);
		glBegin(GL_QUADS);
		glVertex2f(x_pos, y_pos + height * 0.5f);
		glVertex2f(x_pos, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos + height * 0.5f);
		glEnd();
		__glcolor(0x007FFF3F + wheel_field_hovered * 0x3F);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x_pos, y_pos + height * 0.5f);
		glVertex2f(x_pos, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos + height * 0.5f);
		glEnd();
		glBegin(GL_LINE_LOOP);
		glVertex2f(x_pos - width * 0.5f, y_pos + height * 0.5f);
		glVertex2f(x_pos - width * 0.5f, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos - height * 0.5f);
		glVertex2f(x_pos + width * 0.5f, y_pos + height * 0.5f);
		glEnd();
		var_if->draw();
		fac_if->draw();
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
		var_if->safe_move(dx, dy);
		fac_if->safe_move(dx, dy);
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

	void checkup_inputs()
	{
		std::lock_guard locker(lock);
		variable = std::stod(var_if->get_current_input("0"));
		factor = std::stod(fac_if->get_current_input("0"));
	}

	void keyboard_handler(char ch) override
	{
		std::lock_guard locker(lock);
		fac_if->keyboard_handler(ch);
		var_if->keyboard_handler(ch);
		if (is_hovered)
		{
			if (ch == 13)
			{
				checkup_inputs();
				if (on_apply)
					on_apply(variable);
			}
		}
	}

	void safe_string_replace(std::string) override {}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);
		fac_if->mouse_handler(mx, my, button_btn, state);
		var_if->mouse_handler(mx, my, button_btn, state);
		mx -= x_pos;
		my -= y_pos;
		if (fabsf(mx) < width * 0.5f && fabsf(my) < height * 0.5f)
		{
			is_hovered = true;
			if (mx >= 0 && mx <= width * 0.5f && fabsf(my) < height * 0.5f)
			{
				if (sen == Sensitivity::on_click && state == 1)
					if (on_apply)
						on_apply(variable);
				wheel_field_hovered = true;
				if (button_btn)
				{
					checkup_inputs();
					if (button_btn == 2 /*UP*/)
					{
						if (state == -1)
						{
							switch (type)
							{
							case wheel_variable_changer::Type::exponential: { variable *= factor; break; }
							case wheel_variable_changer::Type::addictable:  { variable += factor; break; }
							}
							var_if->update_input_string(std::to_string(variable));
							if (sen == Sensitivity::on_wheel)
								if (on_apply)
									on_apply(variable);
						}
					}
					else if (button_btn == 3 /*DOWN*/)
					{
						if (state == -1)
						{
							switch (type)
							{
							case wheel_variable_changer::Type::exponential: { variable /= factor; break; }
							case wheel_variable_changer::Type::addictable:  { variable -= factor; break; }
							}
							var_if->update_input_string(std::to_string(variable));
							if (sen == Sensitivity::on_wheel)
								if (on_apply)
									on_apply(variable);
						}
					}
				}
			}
		}
		else
		{
			is_hovered = false;
			wheel_field_hovered = false;
		}
		return false;
	}
};

using WheelVariableChanger = wheel_variable_changer;

#endif
