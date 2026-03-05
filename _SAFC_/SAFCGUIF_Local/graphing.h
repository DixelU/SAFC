#pragma once
#ifndef SAFGUIF_L_G
#define SAFGUIF_L_G

#include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/SAFC_IM.h"

using local_fp_type = float;
constexpr float __epsilon = std::numeric_limits<float>::min() * 10;

template<typename ordered_map_type = std::map<int, int>>
struct Graphing : handleable_ui_part
{
	float cx_pos, cy_pos;
	float my_pos, mx_pos;
	local_fp_type horizontal_scaling, central_point;
	local_fp_type assigned_x_selection_by_key;
	ordered_map_type* graph;
	std::unique_ptr<single_text_line> stl_info;
	local_fp_type width, target_height, scale_coef, shift;
	bool auto_adjusting, is_hovered;
	std::uint32_t color, nearest_line_color, point_color, selection_color;

	Graphing(float cx_pos,
		float cy_pos,
		float width,
		float target_height,
		float scale_coef,
		bool auto_adjusting,
		std::uint32_t color,
		std::uint32_t text_color,
		std::uint32_t nearest_line_color,
		std::uint32_t point_color,
		std::uint32_t selection_color,
		ordered_map_type* graph,
		single_text_line_settings& stls,
		bool initial_enabled):
			cx_pos(cx_pos),
			cy_pos(cy_pos),
			width(width),
			target_height(target_height),
			scale_coef(scale_coef),
			color(color),
			auto_adjusting(auto_adjusting),
			graph(graph), 
			nearest_line_color(nearest_line_color),
			point_color(point_color),
			selection_color(selection_color),
			horizontal_scaling(1),
			central_point(0),
			is_hovered(false),
			my_pos(0),
			mx_pos(0),
			shift(0),
			assigned_x_selection_by_key(0)

	{
		this->enabled = initial_enabled;
		stls.rgba_color = text_color;
		stls.set_new_pos(cx_pos, cy_pos - 0.5f * target_height + 5.f);
		stl_info.reset(stls.create_one("_"));
	}

	~Graphing() override = default;

	void reset()
	{
		std::lock_guard locker(lock);

		horizontal_scaling = 1;
		central_point = 0;
		is_hovered = false;
	}

	void draw() override
	{
		std::lock_guard locker(lock);

		if (enabled && graph && graph->size())
		{
			local_fp_type begin = graph->begin()->first;
			local_fp_type end = graph->rbegin()->first;
			local_fp_type max_value = -1e31f, min_value = 1e31f;
			local_fp_type prev_value = 0;
			local_fp_type t_prev_value = prev_value;
			bool is_last_loop_complete = false, is_first = true;
			__glcolor(color);
			glBegin(GL_LINE_STRIP);
			for (auto t : *graph)
			{
				is_last_loop_complete = false;
				local_fp_type cur_pos = t.first;
				local_fp_type cur_value = t.second;
				local_fp_type cur_y = scale_coef * (cur_value) + shift;
				local_fp_type cur_x = ((cur_pos - begin) / (end - begin) - 0.5f + central_point) * horizontal_scaling;
				t_prev_value = std::clamp(prev_value, cy_pos - 0.5f * target_height, cy_pos + 0.5f * target_height);
				prev_value = cy_pos - 0.5f * target_height + cur_y * target_height;
				if (cur_x < -0.5f)
					continue;
				if (cur_x > 0.5f)
					break;

				if (is_first)
				{
					glVertex2f(cx_pos - 0.5f * width, t_prev_value);
					is_first = false;
				}

				if (max_value < cur_y)
					max_value = cur_y;
				if (min_value > cur_y)
					min_value = cur_y;
				glVertex2f(cx_pos + cur_x * width, t_prev_value);
				glVertex2f(cx_pos + cur_x * width, prev_value);
				is_last_loop_complete = true;
			}
			glVertex2f(cx_pos + 0.5f * width, (is_last_loop_complete) ? prev_value : t_prev_value);
			glEnd();
			if (assigned_x_selection_by_key > 0.5f)
			{
				__glcolor(selection_color);
				local_fp_type last_pos_x = ((assigned_x_selection_by_key - begin) / (end - begin) - 0.5f + central_point) * horizontal_scaling;
				if (last_pos_x >= -0.5f) {
					last_pos_x = ((last_pos_x < 0.5f) ? last_pos_x : 0.5f) * width;
					glBegin(GL_POLYGON);
					glVertex2f(cx_pos - 0.5f * width, cy_pos + target_height * 0.5f);
					glVertex2f(cx_pos + last_pos_x, cy_pos + target_height * 0.5f);
					glVertex2f(cx_pos + last_pos_x, cy_pos - target_height * 0.5f);
					glVertex2f(cx_pos - 0.5f * width, cy_pos - target_height * 0.5f);
					glEnd();
				}
			}
			if (auto_adjusting)
			{
				if (min_value != max_value) {
					shift = (shift - min_value);
					float new_scale_coef = std::max(scale_coef / (max_value - min_value), __epsilon * 4.f);
					if (!isnan(new_scale_coef))
						scale_coef = new_scale_coef;
				}
			}
			if (is_hovered)
			{
				local_fp_type cur_x = (((mx_pos - cx_pos) / width) / horizontal_scaling - central_point + 0.5f) * (end - begin) + begin;
				glLineWidth(1);
				__glcolor(color);
				glBegin(GL_LINE_LOOP);
				glVertex2f(cx_pos - 0.5f * width, cy_pos + target_height * 0.5f);
				glVertex2f(cx_pos + 0.5f * width, cy_pos + target_height * 0.5f);
				glVertex2f(cx_pos + 0.5f * width, cy_pos - target_height * 0.5f);
				glVertex2f(cx_pos - 0.5f * width, cy_pos - target_height * 0.5f);
				glEnd();

				glBegin(GL_LINES);
				glVertex2f(mx_pos, cy_pos - target_height * 0.5f);
				glVertex2f(mx_pos, cy_pos + target_height * 0.5f);
				glEnd();
				auto equal_u_bound = graph->upper_bound(cur_x);
				auto lesser_one = equal_u_bound;
				if (equal_u_bound != graph->begin())
					lesser_one--;
				local_fp_type last_pos_x = ((lesser_one->first - begin) / (end - begin) - 0.5f + central_point) * horizontal_scaling;
				local_fp_type last_pos_y = cy_pos - 0.5f * target_height + (scale_coef * lesser_one->second + shift) * target_height;
				if (abs(last_pos_x) <= 0.5f)
				{
					last_pos_x = last_pos_x * width + cx_pos;
					__glcolor(nearest_line_color);
					glLineWidth(1);
					glBegin(GL_LINES);
					glVertex2f(last_pos_x, cy_pos - target_height * 0.5f);
					glVertex2f(last_pos_x, cy_pos + target_height * 0.5f);
					glEnd();

					__glcolor(point_color);
					glPointSize(3);
					glBegin(GL_POINTS);
					glVertex2f(last_pos_x, last_pos_y);
					glEnd();
				}
				stl_info->safe_string_replace(std::to_string(lesser_one->first) + " : " + std::to_string(lesser_one->second));
			}
			else
				if (stl_info->current_text != "-:-")
					stl_info->safe_string_replace("-:-");
		}
		else if (enabled)
		{
			if (stl_info->current_text != "NULL graph")
				stl_info->safe_string_replace("NULL graph");
		}
		else
		{
			glColor4f(0.f, 0.f, 0.f, 0.15f);
			glBegin(GL_POLYGON);
			glVertex2f(cx_pos - 0.5f * width, cy_pos + target_height * 0.5f);
			glVertex2f(cx_pos + 0.5f * width, cy_pos + target_height * 0.5f);
			glVertex2f(cx_pos + 0.5f * width, cy_pos - target_height * 0.5f);
			glVertex2f(cx_pos - 0.5f * width, cy_pos - target_height * 0.5f);
			glEnd();
			if (stl_info->current_text != "graph disabled")
				stl_info->safe_string_replace("graph disabled");
		}

		stl_info->draw();
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);

		cx_pos += dx;
		cy_pos += dy;

		stl_info->safe_move(dx, dy);
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
			) * width,
			ch = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & arg))
				) * target_height;

		safe_change_position(new_x + cw, new_y + ch);
	}

	void keyboard_handler(char ch) override
	{
		if (!is_hovered || !enabled)
			return;

		std::lock_guard locker(lock);

		switch (ch)
		{
			case 'W':
			case 'w':
				horizontal_scaling *= 1.1f;
				if (horizontal_scaling < 1)
					horizontal_scaling = 1;
				break;
			case 'S':
			case 's':
				horizontal_scaling /= 1.1f;
				if (horizontal_scaling < 1)
					horizontal_scaling = 1;
				break;
			case 'D':
			case 'd':
				central_point -= 0.05f / horizontal_scaling;
				break;
			case 'A':
			case 'a':
				central_point += 0.05f / horizontal_scaling;
				break;
			case 'r':
			case 'R':
				central_point = 0;
				horizontal_scaling = 1;
				scale_coef = 0.001f;
				shift = 0;
				break;
		}
	}

	void safe_string_replace(std::string) override
	{
		return;
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char, char) override
	{
		std::lock_guard locker(lock);

		if (abs(mx - cx_pos) <= 0.5f * width && abs(my - cy_pos) <= 0.5f * target_height)
			is_hovered = true;
		else
			is_hovered = false;

		mx_pos = mx;
		my_pos = my;

		return false;
	}
};

#endif
