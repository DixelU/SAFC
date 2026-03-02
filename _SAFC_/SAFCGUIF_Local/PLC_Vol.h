#pragma once
#ifndef SAFGUIF_L_PLCV
#define SAFGUIF_L_PLCV

#include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/include_all.h"

struct PLC_VolumeWorker : handleable_ui_part
{
	std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> plc_bb;
	std::pair<std::uint8_t, std::uint8_t> hovered_point;
	std::unique_ptr<single_text_line> stl_msg;
	float cx_pos, cy_pos, horizontal_sides_size, vertical_sides_size;
	float mouse_x, mouse_y, x_sq_sz, y_sq_sz;
	bool hovered, active_setting, re_put_mode;
	// Backward-compat aliases
	bool& RePutMode = re_put_mode;
	bool& ActiveSetting = active_setting;
	bool& Hovered = hovered;
	std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>>& PLC_bb = plc_bb;
	std::uint8_t xcp, ycp;
	std::uint8_t fpx, fpy;

	~PLC_VolumeWorker() override = default;

	PLC_VolumeWorker(float cx_pos, float cy_pos, float width, float height,
		std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> plc_bb = nullptr)
	{
		this->plc_bb = std::move(plc_bb);
		this->cx_pos = cx_pos;
		this->cy_pos = cy_pos;
		this->x_sq_sz = width / 256;
		this->y_sq_sz = height / 256;
		this->horizontal_sides_size = width;
		this->vertical_sides_size = height;
		this->re_put_mode = false;
		this->stl_msg = std::make_unique<single_text_line>(
			"_", cx_pos, cy_pos,
			System_White->x_unit_size, System_White->y_unit_size, System_White->space_width,
			2, 0xFFAFFFCF, std::optional<std::uint32_t>{0xAFFFAFCF}, (7 << 4) | 3);
		this->mouse_x = this->mouse_y = 0.f;
		this->hovered_point = std::pair<std::uint8_t, std::uint8_t>(0, 0);
		active_setting = hovered = fpx = fpy = xcp = ycp = false;
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		float begx = cx_pos - 0.5f * horizontal_sides_size, begy = cy_pos - 0.5f * vertical_sides_size;
		glColor4f(1, 1, 1, (hovered) ? 0.85f : 0.5f);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + vertical_sides_size);
		glVertex2f(begx + horizontal_sides_size, begy + vertical_sides_size);
		glVertex2f(begx + horizontal_sides_size, begy);
		glEnd();
		glColor4f(0.5f, 1, 0.5f, 0.05f);// showing "safe" for volumes square
		glBegin(GL_QUADS);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + 0.5f * vertical_sides_size);
		glVertex2f(begx + 0.5f * horizontal_sides_size, begy + 0.5f * vertical_sides_size);
		glVertex2f(begx + 0.5f * horizontal_sides_size, begy);
		glEnd();

		if (hovered)
		{
			glColor4f(1, 1, 1, 0.25f);
			glBegin(GL_LINES);
			glVertex2f(begx + x_sq_sz * xcp, begy);
			glVertex2f(begx + x_sq_sz * xcp, begy + vertical_sides_size);
			glVertex2f(begx, begy + y_sq_sz * ycp);
			glVertex2f(begx + horizontal_sides_size, begy + y_sq_sz * ycp);
			glVertex2f(begx + x_sq_sz * (xcp + 1), begy);
			glVertex2f(begx + x_sq_sz * (xcp + 1), begy + vertical_sides_size);
			glVertex2f(begx, begy + y_sq_sz * (ycp + 1));
			glVertex2f(begx + horizontal_sides_size, begy + y_sq_sz * (ycp + 1));
			glEnd();
		}
		if (plc_bb && plc_bb->map.size())
		{
			glLineWidth(3);
			glColor4f(1, 0.75f, 1, 0.5f);
			glBegin(GL_LINE_STRIP);
			glVertex2f(begx, begy + y_sq_sz * (plc_bb->at(0) + 0.5f));

			for (auto y = plc_bb->map.begin(); y != plc_bb->map.end(); y++)
				glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);

			glVertex2f(begx + 255.5f * x_sq_sz, begy + y_sq_sz * (plc_bb->at(255) + 0.5f));
			glEnd();
			glColor4f(1, 1, 1, 0.75f);
			glPointSize(5);
			glBegin(GL_POINTS);
			for (auto y = plc_bb->map.begin(); y != plc_bb->map.end(); y++)
				glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);
			glEnd();
		}
		if (active_setting)
		{
			std::map<std::uint8_t, std::uint8_t>::iterator y;
			glBegin(GL_LINES);
			glVertex2f(begx + (fpx + 0.5f) * x_sq_sz, begy + (fpy + 0.5f) * y_sq_sz);
			glVertex2f(begx + (xcp + 0.5f) * x_sq_sz, begy + (ycp + 0.5f) * y_sq_sz);
			if (fpx < xcp)
			{
				y = plc_bb->map.upper_bound(xcp);
				if (y != plc_bb->map.end())
				{
					glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);
					glVertex2f(begx + (xcp + 0.5f) * x_sq_sz, begy + (ycp + 0.5f) * y_sq_sz);
				}
				y = plc_bb->map.lower_bound(fpx);
				if (y != plc_bb->map.end() && y != plc_bb->map.begin())
				{
					--y;
					glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);
					glVertex2f(begx + (fpx + 0.5f) * x_sq_sz, begy + (fpy + 0.5f) * y_sq_sz);
				}
			}
			else {
				y = plc_bb->map.upper_bound(fpx);
				if (y != plc_bb->map.end())
				{
					glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);
					glVertex2f(begx + (fpx + 0.5f) * x_sq_sz, begy + (fpy + 0.5f) * y_sq_sz);
				}
				y = plc_bb->map.lower_bound(xcp);
				if (y != plc_bb->map.end() && y != plc_bb->map.begin())
				{
					--y;
					glVertex2f(begx + (y->first + 0.5f) * x_sq_sz, begy + (y->second + 0.5f) * y_sq_sz);
					glVertex2f(begx + (xcp + 0.5f) * x_sq_sz, begy + (ycp + 0.5f) * y_sq_sz);
				}
			}
			glEnd();
		}
		stl_msg->draw();
	}

	void update_info()
	{
		std::lock_guard locker(lock);
		std::int16_t tx = (std::int16_t)(128. + floor(255 * (mouse_x - cx_pos) / horizontal_sides_size)),
			ty = (std::int16_t)(128. + floor(255 * (mouse_y - cy_pos) / vertical_sides_size));
		if (tx < 0 || tx > 255 || ty < 0 || ty > 255)
		{
			if (hovered)
			{
				stl_msg->safe_string_replace("-:-");
				hovered = false;
			}
		}
		else
		{
			hovered = true;
			stl_msg->safe_string_replace(std::to_string(xcp = (std::uint8_t)tx) + ":" + std::to_string(ycp = (std::uint8_t)ty));
		}
	}

	void make_map_more_simple()
	{
		std::lock_guard locker(lock);
		if (plc_bb)
		{
			std::uint8_t tf, ts;
			for (auto y = plc_bb->map.begin(); y != plc_bb->map.end();)
			{
				tf = y->first;
				ts = y->second;
				y = plc_bb->map.erase(y);
				if (plc_bb->at(tf) != ts)
					plc_bb->map[tf] = ts;
			}
		}
	}

	// Legacy alias
	void _MakeMapMoreSimple() { make_map_more_simple(); }

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		cx_pos += dx;
		cy_pos += dy;
		this->stl_msg->safe_move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		new_x -= cx_pos;
		new_y -= cy_pos;
		safe_move(new_x, new_y);
	}

	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		float cw = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & arg))
			) * horizontal_sides_size,
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * vertical_sides_size;
		safe_change_position(new_x + cw, new_y + ch);
	}

	void keyboard_handler(char) override {}

	void safe_string_replace(std::string) override {}

	void re_put_from_a_to_b(std::uint8_t a, std::uint8_t b, std::uint8_t val_a, std::uint8_t val_b)
	{
		std::lock_guard locker(lock);
		if (plc_bb)
		{
			if (b < a)
			{
				std::swap(a, b);
				std::swap(val_a, val_b);
			}
			auto it_a = plc_bb->map.lower_bound(a), it_b = plc_bb->map.upper_bound(b);
			while (it_a != it_b)
				it_a = plc_bb->map.erase(it_a);

			plc_bb->insert(a, val_a);
			plc_bb->insert(b, val_b);
		}
	}

	// Legacy alias
	void RePutFromAtoB(std::uint8_t a, std::uint8_t b, std::uint8_t va, std::uint8_t vb) { re_put_from_a_to_b(a, b, va, vb); }

	void just_put_new_value(std::uint8_t a, std::uint8_t val_a)
	{
		std::lock_guard locker(lock);
		if (plc_bb)
			plc_bb->insert(a, val_a);
	}

	// Legacy alias
	void JustPutNewValue(std::uint8_t a, std::uint8_t v) { just_put_new_value(a, v); }

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);
		this->mouse_x = mx;
		this->mouse_y = my;
		update_info();
		if (hovered)
		{
			if (button_btn)
			{
				if (button_btn == -1)
				{
					if (this->active_setting)
					{
						if (state == 1)
						{
							active_setting = false;
							re_put_from_a_to_b(fpx, xcp, fpy, ycp);
						}
					}
					else
					{
						if (this->re_put_mode)
						{
							if (state == 1)
							{
								active_setting = true;
								fpx = xcp;
								fpy = ycp;
							}
						}
						else
						{
							if (state == 1)
								just_put_new_value(xcp, ycp);
						}
					}
				}
				else
				{
					// Removing some point
				}
			}
		}
		return false;
	}
};
#endif // !SAFGUIF_L_PLCV
