#pragma once
#ifndef SAFGUIF_L_CATH
#define SAFGUIF_L_CATH

#include <utility>

#include "../SAFGUIF/SAFGUIF.h"

// #include "../SAFC_InnerModules/include_all.h"

struct CAT_Piano : handleable_ui_part
{
	std::shared_ptr<cut_and_transpose> piano_transform;
	std::unique_ptr<single_text_line> min_cont, max_cont, transp;
	float calculated_height, calculated_width;
	float base_x_pos, base_y_pos, piano_height, key_width;
	bool focused;
	// Backward-compat aliases
	std::shared_ptr<cut_and_transpose>& PianoTransform = piano_transform;

	~CAT_Piano() override = default;

	CAT_Piano(float base_x_pos, float base_y_pos, float key_width, float piano_height, std::shared_ptr<cut_and_transpose> piano_transform)
	{
		this->base_x_pos = base_x_pos;
		this->base_y_pos = base_y_pos;
		this->key_width = key_width;
		this->piano_height = piano_height;
		this->calculated_height = piano_height * 4;
		this->calculated_width = key_width * (128 * 3);
		this->piano_transform = std::move(piano_transform);
		this->focused = false;
		System_White->set_new_pos(base_x_pos + key_width * (128 * 1.25f), base_y_pos - 0.75f * piano_height);
		this->min_cont.reset(System_White->create_one("_"));
		System_White->set_new_pos(base_x_pos - key_width * (128 * 1.25f), base_y_pos - 0.75f * piano_height);
		this->max_cont.reset(System_White->create_one("_"));
		this->transp.reset(System_White->create_one("_"));
		update_info();
	}

	constexpr static bool is_white_key(std::uint8_t key)
	{
		key %= 12;
		if (key < 5)
			return !(key & 1);
		else
			return (key & 1);
	}

	void update_info()
	{
		std::lock_guard locker(lock);
		if (!piano_transform)
			return;
		min_cont->safe_string_replace("Min: " + std::to_string(piano_transform->min_val));
		max_cont->safe_string_replace("Max: " + std::to_string(piano_transform->max_val));
		transp->safe_string_replace("Transp: " + std::to_string(piano_transform->transpose_val));
		transp->safe_change_position(base_x_pos + ((piano_transform->transpose_val >= 0) ? 1 : -1) * key_width * (128 * 1.25f), base_y_pos +
			0.75f * piano_height);
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		float x = base_x_pos - 128 * key_width, pixsz = internal_range / window_base_width;
		bool inside = false;

		min_cont->draw();
		max_cont->draw();
		transp->draw();
		if (!piano_transform)
			return;

		glLineWidth(0.5f * key_width / pixsz);
		glBegin(GL_LINES);
		for (int i = 0; i < 256; i++, x += key_width)
		{// Main piano
			inside = ((i >= (piano_transform->min_val)) && (i <= (piano_transform->max_val)));
			if (is_white_key(i))
			{
				glColor4f(1, 1, 1, (inside ? 0.9f : 0.25f));
				glVertex2f(x, base_y_pos - 1.25f * piano_height);
				glVertex2f(x, base_y_pos - 0.25f * piano_height);
				continue;
			}
			else
			{
				glColor4f(0, 0, 0, (inside ? 0.9f : 0.25f));
				glVertex2f(x, base_y_pos - 0.75f * piano_height);
				glVertex2f(x, base_y_pos - 0.25f * piano_height);
			}
			glColor4f(1, 1, 1, (inside ? 0.9f : 0.25f));
			glVertex2f(x, base_y_pos - 0.75f * piano_height);
			glVertex2f(x, base_y_pos - 1.25f * piano_height);
		}
		x = base_x_pos - (128 + piano_transform->transpose_val) * key_width;
		for (int i = 0; i < 256; i++, x += key_width)
		{// CAT Piano
			if (fabs(x - base_x_pos) >= 0.5f * calculated_width)
				continue;
			inside = ((i - (piano_transform->transpose_val) >= (piano_transform->min_val)) && (i - (piano_transform->transpose_val) <= (piano_transform->max_val)));
			if (is_white_key(i))
			{
				glColor4f(1, 1, 1, (inside ? 0.9f : 0.25f));
				glVertex2f(x, base_y_pos + 1.25f * piano_height);
				glVertex2f(x, base_y_pos + 0.25f * piano_height);
				continue;
			}
			else
			{
				glColor4f(0, 0, 0, (inside ? 0.9f : 0.25f));
				glVertex2f(x, base_y_pos + 0.75f * piano_height);
				glVertex2f(x, base_y_pos + 0.25f * piano_height);
			}
			glColor4f(1, 1, 1, (inside ? 0.9f : 0.25f));
			glVertex2f(x, base_y_pos + 1.25f * piano_height);
			glVertex2f(x, base_y_pos + 0.75f * piano_height);
		}
		glEnd();

		x = base_x_pos - (128 - piano_transform->min_val + 0.5f) * key_width;// Square
		glLineWidth(1);
		glColor4f(0, 1, 0, 0.75f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, base_y_pos - 1.5f * piano_height);
		glVertex2f(x, base_y_pos + 1.5f * piano_height);
		x += key_width * (piano_transform->max_val - piano_transform->min_val + 1);
		glVertex2f(x, base_y_pos + 1.5f * piano_height);
		glVertex2f(x, base_y_pos - 1.5f * piano_height);
		glEnd();
		if (!focused)
			return;
		glColor4f(1, 0.5f, 0, 1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(base_x_pos - 0.5f * calculated_width, base_y_pos - 0.5f * calculated_height);
		glVertex2f(base_x_pos - 0.5f * calculated_width, base_y_pos + 0.5f * calculated_height);
		glVertex2f(base_x_pos + 0.5f * calculated_width, base_y_pos + 0.5f * calculated_height);
		glVertex2f(base_x_pos + 0.5f * calculated_width, base_y_pos - 0.5f * calculated_height);
		glEnd();
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		base_x_pos += dx;
		base_y_pos += dy;
		this->max_cont->safe_move(dx, dy);
		this->min_cont->safe_move(dx, dy);
		this->transp->safe_move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		new_x -= base_x_pos;
		new_y -= base_y_pos;
		safe_move(new_x, new_y);
	}

	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
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

	void keyboard_handler(char ch) override
	{
		std::lock_guard locker(lock);
		if (focused)
		{
			if (!piano_transform)
				return;
			switch (ch)
			{
			case 'W':
			case 'w':
				if (piano_transform->transpose_val < 255)
				{
					piano_transform->transpose_val += 1;
					update_info();
				}
				break;
			case 'S':
			case 's':
				if (piano_transform->transpose_val > -255)
				{
					piano_transform->transpose_val -= 1;
					update_info();
				}
				break;
			case 'D':
			case 'd':
				if (piano_transform->min_val < 255)
				{
					piano_transform->min_val += 1;
					update_info();
				}
				break;
			case 'A':
			case 'a':
				if (piano_transform->min_val > 0)
				{
					piano_transform->min_val -= 1;
					update_info();
				}
				break;
			case 'E':
			case 'e':
				if (piano_transform->max_val < 255)
				{
					piano_transform->max_val += 1;
					update_info();
				}
				break;
			case 'Q':
			case 'q':
				if (piano_transform->max_val > 0)
				{
					piano_transform->max_val -= 1;
					update_info();
				}
				break;
			}
		}
	}

	void safe_string_replace(std::string) override
	{
		return;
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char, char) override
	{
		std::lock_guard locker(lock);
		mx -= base_x_pos;
		my -= base_y_pos;
		if (fabsf(mx) <= calculated_width * 0.5f && fabsf(my) <= calculated_height * 0.5f)
			focused = true;
		else focused = false;
		return false;
	}

	// Backward-compat method wrapper
	void UpdateInfo() { update_info(); }
};

#endif
