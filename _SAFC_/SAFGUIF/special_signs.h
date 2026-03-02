#pragma once
#ifndef SAFGUIF_SPECIALSIGNS
#define SAFGUIF_SPECIALSIGNS

#include <functional>

#include "header_utils.h"
#include "handleable_ui_part.h"

constexpr auto HTSQ2 = (1.85);

struct special_signs
{
	static void draw_ok(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t = 0)
	{
		__glcolor(rgba_color);
		glLineWidth(ceil(sz_param / 2));
		glBegin(GL_LINE_STRIP);
		glVertex2f(x - sz_param * 0.766f, y + sz_param * 0.916f);
		glVertex2f(x - sz_param * 0.1f, y + sz_param * 0.25f);
		glVertex2f(x + sz_param * 0.9f, y + sz_param * 1.25f);
		glEnd();
		glPointSize(ceil(sz_param / 2));
		glBegin(GL_POINTS);
		glVertex2f(x - sz_param * 0.1f, y + sz_param * 0.25f);
		glEnd();
	}
	static void draw_ex_triangle(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t secondary_rgba_color)
	{
		__glcolor(rgba_color);
		glBegin(GL_TRIANGLES);
		glVertex2f(x, y + HTSQ2 * sz_param);
		glVertex2f(x - sz_param, y);
		glVertex2f(x + sz_param, y);
		glEnd();
		__glcolor(secondary_rgba_color);
		glLineWidth(ceil(sz_param / 8));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + HTSQ2 * sz_param);
		glVertex2f(x - sz_param, y);
		glVertex2f(x + sz_param, y);
		glEnd();
		glLineWidth(ceil(sz_param / 4));
		glBegin(GL_LINES);
		glVertex2f(x, y + sz_param * 0.6f);
		glVertex2f(x, y + sz_param * 1.40f);
		glVertex2f(x, y + sz_param * 0.2f);
		glVertex2f(x, y + sz_param * 0.4f);
		glEnd();
	}
	static void draw_file_sign(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t secondary_rgba_color)
	{
		__glcolor(rgba_color);
		glLineWidth(ceil(sz_param / 5));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + sz_param);
		glVertex2f(x - sz_param, y);
		glVertex2f(x - sz_param, y - sz_param);
		glVertex2f(x + sz_param, y - sz_param);
		glVertex2f(x + sz_param, y + sz_param);
		glEnd();
		glBegin(GL_LINES);
		glVertex2f(x, y + sz_param);
		glVertex2f(x, y);
		glVertex2f(x, y);
		glVertex2f(x - sz_param, y);
		glEnd();
		glPointSize(ceil(sz_param / 5));
		glBegin(GL_POINTS);
		glVertex2f(x, y);
		glVertex2f(x, y + sz_param);
		glVertex2f(x - sz_param, y);
		glVertex2f(x - sz_param, y - sz_param);
		glVertex2f(x + sz_param, y - sz_param);
		glVertex2f(x + sz_param, y + sz_param);
		glEnd();
	}
	static void draw_a_circle(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t secondary_rgba_color)
	{
		__glcolor(secondary_rgba_color);
		glBegin(GL_POLYGON);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(sz_param * 1.25f * (cos(angle_to_radians(a))) + x, sz_param * 1.25f * (sin(angle_to_radians(a))) + y + sz_param * 0.75f);
		glEnd();
		__glcolor(rgba_color);
		glLineWidth(ceil(sz_param / 10));
		glBegin(GL_LINE_LOOP);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(sz_param * 1.25f * (cos(angle_to_radians(a))) + x, sz_param * 1.25f * (sin(angle_to_radians(a))) + y + sz_param * 0.75f);
		glEnd();
	}
	static void draw_no(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t = 0)
	{
		__glcolor(rgba_color);
		glLineWidth(ceil(sz_param / 2));
		glBegin(GL_LINES);
		glVertex2f(x - sz_param * 0.5f, y + sz_param * 0.25f);
		glVertex2f(x + sz_param * 0.5f, y + sz_param * 1.25f);
		glVertex2f(x + sz_param * 0.5f, y + sz_param * 0.25f);
		glVertex2f(x - sz_param * 0.5f, y + sz_param * 1.25f);
		glEnd();
	}
	static void draw_wait(float x, float y, float sz_param, std::uint32_t rgba_color, std::uint32_t total_stages)
	{
		float start = ((float)((timer_v % total_stages) * 360)) / (float)(total_stages), t;
		std::uint8_t R = (rgba_color >> 24), G = (rgba_color >> 16) & 0xFF, B = (rgba_color >> 8) & 0xFF, A = (rgba_color) & 0xFF;
		glLineWidth(ceil(sz_param / 1.5f));
		glBegin(GL_LINES);
		for (float a = 0; a < 360.5f; a += (180.f / (total_stages)))
		{
			t = a / 360.f;
			glColor4ub(
				(GLubyte)(255 * t + R * (1 - t)),
				(GLubyte)(255 * t + G * (1 - t)),
				(GLubyte)(255 * t + B * (1 - t)),
				(GLubyte)(255 * t + A * (1 - t))
			);
			glVertex2f(sz_param * 1.25f * (cos(angle_to_radians(a + start))) + x, sz_param * 1.25f * (sin(angle_to_radians(a + start))) + y + sz_param * 0.75f);
		}
		glEnd();
	}

	// Backward-compat PascalCase aliases for static draw functions
	static void DrawOk(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_ok(x, y, s, r1, r2); }
	static void DrawExTriangle(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_ex_triangle(x, y, s, r1, r2); }
	static void DrawNo(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_no(x, y, s, r1, r2); }
	static void DrawWait(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_wait(x, y, s, r1, r2); }
	static void DrawACircle(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_a_circle(x, y, s, r1, r2); }
	static void DrawFileSign(float x, float y, float s, std::uint32_t r1, std::uint32_t r2) { draw_file_sign(x, y, s, r1, r2); }
};

struct special_sign_handler : handleable_ui_part
{
	float x, y, sz_param;
	std::uint32_t frgba, srgba;
	std::function<void(float, float, float, std::uint32_t, std::uint32_t)> draw_func;

	~special_sign_handler() override = default;

	special_sign_handler(std::function<void(float, float, float, std::uint32_t, std::uint32_t)> draw_func,
		float x, float y, float sz_param, std::uint32_t frgba, std::uint32_t srgba)
	{
		this->draw_func = std::move(draw_func);
		this->x = x;
		this->y = y;
		this->sz_param = sz_param;
		this->frgba = frgba;
		this->srgba = srgba;
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		if (draw_func)
			draw_func(x, y, sz_param, frgba, srgba);
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x += dx;
		y += dy;
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		x = new_x;
		y = new_y;
	}

	void replace_draw_func(std::function<void(float, float, float, std::uint32_t, std::uint32_t)> new_draw_func)
	{
		std::lock_guard locker(lock);
		draw_func = std::move(new_draw_func);
	}

	void safe_change_position_argumented(std::uint8_t, float, float) override {}
	void keyboard_handler(char) override {}
	void safe_string_replace(std::string) override {}
	[[nodiscard]] bool mouse_handler(float, float, char, char) override { return false; }
};

// Backward compat aliases
using SpecialSigns = special_signs;
using SpecialSignHandler = special_sign_handler;

#endif // !SAFGUIF_SPECIALSIGNS
