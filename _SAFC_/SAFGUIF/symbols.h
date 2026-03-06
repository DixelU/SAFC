#pragma once
#ifndef SAFGUIF_SYMBOLS
#define SAFGUIF_SYMBOLS

#include "header_utils.h"
#include "charmap.h"

struct dotted_symbol
{
	float x_pos, y_pos;
	float m_xu, m_yu;
	std::uint8_t R, G, B, A, line_width;
	std::string render_way;
	char point_placement[9];
	std::uint8_t point_placement_count;

	dotted_symbol(
		std::string render_way,
		float x_pos,
		float y_pos,
		float x_unit_size,
		float y_unit_size,
		std::uint8_t line_width = 2,
		std::uint8_t red = 255,
		std::uint8_t green = 255,
		std::uint8_t blue = 255,
		std::uint8_t alpha = 255)
	{
		if (!render_way.size())
			render_way = " ";

		this->render_way = std::move(render_way);
		this->x_pos = x_pos;
		this->y_pos = y_pos;
		this->m_xu = x_unit_size;
		this->m_yu = y_unit_size;
		this->line_width = line_width;
		this->R = red; this->G = green; this->B = blue; this->A = alpha;
		this->point_placement_count = 0;

		update_point_placement_positions();
	}

	dotted_symbol(char symbol, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width = 2, std::uint8_t red = 255, std::uint8_t green = 255, std::uint8_t blue = 255, std::uint8_t alpha = 255) :
		dotted_symbol(legacy_draw_map[symbol], x_pos, y_pos, x_unit_size, y_unit_size, line_width, red, green, blue, alpha) {}

	// Value-by-value RGBA constructor (replaces the old raw-pointer owning constructors)
	dotted_symbol(std::string render_way, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width, std::uint32_t rgba_color) :
		dotted_symbol(render_way, x_pos, y_pos, x_unit_size, y_unit_size, line_width, rgba_color >> 24, (rgba_color >> 16) & 0xFF, (rgba_color >> 8) & 0xFF, rgba_color & 0xFF) {}

	dotted_symbol(char symbol, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width, std::uint32_t rgba_color) :
		dotted_symbol(legacy_draw_map[symbol], x_pos, y_pos, x_unit_size, y_unit_size, line_width, rgba_color >> 24, (rgba_color >> 16) & 0xFF, (rgba_color >> 8) & 0xFF, rgba_color & 0xFF) {}

	virtual ~dotted_symbol() = default;

	inline static bool is_renderway_symb(char c)
	{
		return (c <= '9' && c >= '0' || c == ' ');
	}

	inline static bool is_number(char c)
	{
		return (c <= '9' && c >= '0');
	}

	// Offset of grid point [0..8] relative to (x_pos, y_pos).
	// Grid is a 3x3 numpad layout: index = (col) + 3*(row), col/row in {0,1,2}.
	inline float point_x(int index) const { return (index % 3 - 1) * m_xu; }
	inline float point_y(int index) const { return (index / 3 - 1) * m_yu; }

	inline float x_unit_size() const { return m_xu; }
	inline float y_unit_size() const { return m_yu; }

	void update_point_placement_positions()
	{
		point_placement_count = 0;

		if (render_way.size() > 1)
		{
			if (is_number(render_way[0]) && !is_number(render_way[1]))
				point_placement[point_placement_count++] = render_way[0];
			if (is_number(render_way.back()) && !is_number(render_way[render_way.size() - 2]))
				point_placement[point_placement_count++] = render_way.back();
			for (int i = 1; i < (int)render_way.size() - 1; ++i)
			{
				if (is_number(render_way[i]) && !is_number(render_way[i - 1]) && !is_number(render_way[i + 1]))
					point_placement[point_placement_count++] = render_way[i];
			}
		}
		else if (render_way.size() && is_number(render_way[0]))
		{
			point_placement[point_placement_count++] = render_way[0];
		}
	}

protected:
	struct draw_params { float vs; int rlen; };

	// Returns vertical shift and iteration length, handling '#'/'~' descender suffixes
	// without mutating render_way.
	draw_params compute_draw_params() const
	{
		if (render_way.size() > 1)
		{
			char back = render_way.back();
			if (back == '#')
				return { -m_yu, (int)render_way.size() - 1 };
			if (back == '~')
				return { -m_yu * 0.5f, (int)render_way.size() - 1 };
		}
		return { 0.f, (int)render_way.size() };
	}

public:
	virtual void draw()
	{
		if (render_way == " ")
			return;

		auto [vs, rlen] = compute_draw_params();

		glColor4ub(R, G, B, A);
		glLineWidth(line_width);
		glPointSize(line_width);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < rlen; i++)
		{
			if (render_way[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			auto index = render_way[i] - '1';
			glVertex2f(x_pos + point_x(index), y_pos + point_y(index) + vs);
		}
		glEnd();

		glBegin(GL_POINTS);
		for (int i = 0; i < point_placement_count; ++i)
		{
			auto index = point_placement[i] - '1';
			glVertex2f(x_pos + point_x(index), y_pos + point_y(index) + vs);
		}
		glEnd();
	}

	void safe_position_change(float new_x_pos, float new_y_pos)
	{
		safe_char_move(new_x_pos - x_pos, new_y_pos - y_pos);
	}

	void safe_char_move(float dx, float dy)
	{
		x_pos += dx; y_pos += dy;
	}

	// Value-by-value refill gradient (replaces raw-pointer version)
	virtual void refill_gradient(std::uint32_t rgba_color, std::uint32_t g_rgba_color, std::uint8_t base_color_point, std::uint8_t grad_color_point)
	{
		return;
	}

	virtual void refill_gradient(std::uint8_t red = 255, std::uint8_t green = 255, std::uint8_t blue = 255, std::uint8_t alpha = 255,
		std::uint8_t g_red = 255, std::uint8_t g_green = 255, std::uint8_t g_blue = 255, std::uint8_t g_alpha = 255,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8)
	{
		return;
	}
};

float font_height_to_width = 2.5;
namespace lfont_symbols_info
{

bool is_init = false;
GLuint current_font = 0;
HFONT selected_font = nullptr;
std::int32_t font_size = 15;

std::int32_t previous_font_size = font_size;
float previous_font_height_to_width = font_height_to_width;
std::string previous_font_name;

struct font_symb_infos_list_destructor { ~font_symb_infos_list_destructor() { glDeleteLists(current_font, 256); } };
font_symb_infos_list_destructor __font_destructor{};

void initialise_font(const std::string& font_name, bool force = false)
{
	if (!force && font_name == previous_font_name && previous_font_size == font_size &&
		(std::abs)(previous_font_height_to_width - font_height_to_width) < std::numeric_limits<float>::epsilon())
		return;

	previous_font_name = font_name;
	previous_font_size = font_size;
	previous_font_height_to_width = font_height_to_width;

	if (!is_init)
	{
		current_font = glGenLists(256);
		is_init = true;
	}

	auto height = font_size * (base_internal_range / internal_range);
	auto width = (font_size > 0) ? font_size * (base_internal_range / internal_range) / font_height_to_width : 0;

	selected_font = CreateFontA(
		height,
		width,
		0, 0,
		FW_NORMAL,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		NONANTIALIASED_QUALITY,
		FF_DONTCARE,
		font_name.c_str()
	);

	if (selected_font)
	{
		auto hdiobj = SelectObject(hDc, selected_font);
		auto status = wglUseFontBitmaps(hDc, 0, 255, current_font);
		SetMapMode(hDc, MM_TEXT);
	}
}

inline void call_list_on_char(char C)
{
	if (is_init)
	{
		const char PsChStr[2] = { C, 0 };
		glPushAttrib(GL_LIST_BIT);
		glListBase(current_font);
		glCallLists(1, GL_UNSIGNED_BYTE, (const char*)(PsChStr));
		glPopAttrib();
	}
}

inline void call_list_on_string(const std::string& S)
{
	if (is_init)
	{
		glPushAttrib(GL_LIST_BIT);
		glListBase(current_font);
		glCallLists(S.size(), GL_UNSIGNED_BYTE, S.c_str());
		glPopAttrib();
	}
}

const _MAT2 MT = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };

}

struct lfontsymbol : dotted_symbol
{
	char symb;
	GLYPHMETRICS gm;

	lfontsymbol(char symb, float cx_pos, float cy_pos, float x_unit_sz, float y_unit_sz, std::uint32_t rgba):
		dotted_symbol(" ", cx_pos, cy_pos, x_unit_sz, y_unit_sz, 1, rgba >> 24, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF)
	{
		gm.gmBlackBoxX = 0;
		gm.gmBlackBoxY = 0;
		gm.gmCellIncX = 0;
		gm.gmCellIncY = 0;
		gm.gmptGlyphOrigin.x = 0;
		gm.gmptGlyphOrigin.y = 0;

		this->symb = symb;
		reinit_glyph_metrics();
	}

	~lfontsymbol() override = default;

	void reinit_glyph_metrics()
	{
		auto result = GetGlyphOutline(hDc, symb, GGO_GRAY8_BITMAP, &gm, 0, NULL, &lfont_symbols_info::MT);
	}

	void draw() override
	{
		if (!lfont_symbols_info::selected_font || !hDc)
			return;

		float pixel_size = (internal_range * 2) / window_base_width;

		if (fabsf(x_pos) + font_height_to_width > pixel_size * wind_x / 2 ||
			fabsf(y_pos) + font_height_to_width > pixel_size * wind_y / 2)
			return;

		glColor4ub(R, G, B, A);
		glRasterPos2f(x_pos, y_pos - y_unit_size() * 0.5);
		lfont_symbols_info::call_list_on_char(symb);
	}
};

struct bi_colored_dotted_symbol : dotted_symbol
{
	std::uint8_t gR[9], gG[9], gB[9], gA[9];
	std::uint8_t _point_data;

	bi_colored_dotted_symbol(std::string render_way, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width = 2,
		std::uint8_t red = 255, std::uint8_t green = 255, std::uint8_t blue = 255, std::uint8_t alpha = 255,
		std::uint8_t g_red = 255, std::uint8_t g_green = 255, std::uint8_t g_blue = 255, std::uint8_t g_alpha = 255,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8) :
		dotted_symbol(render_way, x_pos, y_pos, x_unit_size, y_unit_size, line_width, 0u)
	{
		this->_point_data = (((base_color_point & 0xF) << 4) | (grad_color_point & 0xF));
		if (base_color_point == grad_color_point)
		{
			for (int i = 0; i < 9; i++)
			{
				gR[i] = red;
				gG[i] = green;
				gB[i] = blue;
				gA[i] = alpha;
			}
		}
		else
		{
			base_color_point--;
			grad_color_point--;
			refill_gradient(red, green, blue, alpha, g_red, g_green, g_blue, g_alpha, base_color_point, grad_color_point);
		}
	}
	bi_colored_dotted_symbol(char symbol, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width = 2,
		std::uint8_t red = 255, std::uint8_t green = 255, std::uint8_t blue = 255, std::uint8_t alpha = 255,
		std::uint8_t g_red = 255, std::uint8_t g_green = 255, std::uint8_t g_blue = 255, std::uint8_t g_alpha = 255,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8) :
		bi_colored_dotted_symbol(legacy_draw_map[symbol], x_pos, y_pos, x_unit_size, y_unit_size, line_width, red, green, blue, alpha, g_red, g_green, g_blue, g_alpha, base_color_point, grad_color_point) {}

	// Value-based constructors (replace old raw-pointer owning constructors)
	bi_colored_dotted_symbol(char symbol, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width,
		std::uint32_t rgba_color, std::uint32_t g_rgba_color,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8) :
		bi_colored_dotted_symbol(legacy_draw_map[symbol], x_pos, y_pos, x_unit_size, y_unit_size, line_width,
			rgba_color >> 24, (rgba_color >> 16) & 0xFF, (rgba_color >> 8) & 0xFF, rgba_color & 0xFF,
			g_rgba_color >> 24, (g_rgba_color >> 16) & 0xFF, (g_rgba_color >> 8) & 0xFF, g_rgba_color & 0xFF,
			base_color_point, grad_color_point) {}

	bi_colored_dotted_symbol(std::string render_way, float x_pos, float y_pos, float x_unit_size, float y_unit_size, std::uint8_t line_width,
		std::uint32_t rgba_color, std::uint32_t g_rgba_color,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8) :
		bi_colored_dotted_symbol(render_way, x_pos, y_pos, x_unit_size, y_unit_size, line_width,
			rgba_color >> 24, (rgba_color >> 16) & 0xFF, (rgba_color >> 8) & 0xFF, rgba_color & 0xFF,
			g_rgba_color >> 24, (g_rgba_color >> 16) & 0xFF, (g_rgba_color >> 8) & 0xFF, g_rgba_color & 0xFF,
			base_color_point, grad_color_point) {}

	~bi_colored_dotted_symbol() override = default;

	void refill_gradient(std::uint8_t red = 255, std::uint8_t green = 255, std::uint8_t blue = 255, std::uint8_t alpha = 255,
		std::uint8_t g_red = 255, std::uint8_t g_green = 255, std::uint8_t g_blue = 255, std::uint8_t g_alpha = 255,
		std::uint8_t base_color_point = 5, std::uint8_t grad_color_point = 8) override
	{
		float xbase = (((float)(base_color_point % 3)) - 1.f), ybase = (((float)(base_color_point / 3)) - 1.f),
			xgrad = (((float)(grad_color_point % 3)) - 1.f), ygrad = (((float)(grad_color_point / 3)) - 1.f);
		float ax = xgrad - xbase, ay = ygrad - ybase, t;
		float ial = 1.f / (ax * ax + ay * ay);

		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = std::clamp(red * t + (1.f - t) * g_red, 0.f, 255.f);
				gR[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}

		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = std::clamp(green * t + (1.f - t) * g_green, 0.f, 255.f);
				gG[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = std::clamp(blue * t + (1.f - t) * g_blue, 0.f, 255.f);
				gB[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = std::clamp(alpha * t + (1.f - t) * g_alpha, 0.f, 255.f);
				gA[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
	}

	void refill_gradient(std::uint32_t rgba_color, std::uint32_t g_rgba_color, std::uint8_t base_color_point, std::uint8_t grad_color_point) override
	{
		refill_gradient(rgba_color >> 24, (rgba_color >> 16) & 0xFF, (rgba_color >> 8) & 0xFF, rgba_color & 0xFF,
			g_rgba_color >> 24, (g_rgba_color >> 16) & 0xFF, (g_rgba_color >> 8) & 0xFF, g_rgba_color & 0xFF,
			base_color_point, grad_color_point);
	}

	void draw() override
	{
		if (render_way == " ")
			return;

		auto [vs, rlen] = compute_draw_params();

		glLineWidth(line_width);
		glPointSize(line_width);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < rlen; i++)
		{
			if (render_way[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			auto index = render_way[i] - '1';
			glColor4ub(gR[index], gG[index], gB[index], gA[index]);
			glVertex2f(x_pos + point_x(index), y_pos + point_y(index) + vs);
		}
		glEnd();

		glBegin(GL_POINTS);
		for (int i = 0; i < point_placement_count; ++i)
		{
			auto index = point_placement[i] - '1';
			glColor4ub(gR[index], gG[index], gB[index], gA[index]);
			glVertex2f(x_pos + point_x(index), y_pos + point_y(index) + vs);
		}
		glEnd();
	}
};

#endif
