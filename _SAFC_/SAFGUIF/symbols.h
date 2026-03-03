#pragma once
#ifndef SAFGUIF_SYMBOLS
#define SAFGUIF_SYMBOLS

#include "header_utils.h"
#include "charmap.h"

struct coords
{
	float x, y;
	void set(float newx, float newy)
	{
		x = newx; y = newy;
	}
};

struct dotted_symbol
{
	float x_pos, y_pos;
	std::uint8_t R, G, B, A, line_width;
	coords points[9];
	std::string render_way;
	std::vector<char> point_placement;

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
		this->line_width = line_width;
		this->y_pos = y_pos;
		this->R = red; this->G = green; this->B = blue; this->A = alpha;

		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
				points[x + 1 + 3 * (y + 1)].set(x_unit_size * x, y_unit_size * y);
		}

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

	void update_point_placement_positions()
	{
		point_placement.clear();

		if (render_way.size() > 1)
		{
			if (is_number(render_way[0]) && !is_number(render_way[1]))
				point_placement.push_back(render_way[0]);
			if (is_number(render_way.back()) && !is_number(render_way[render_way.size() - 2]))
				point_placement.push_back(render_way.back());
			for (int i = 1; i < (int)render_way.size() - 1; ++i)
			{
				if (is_number(render_way[i]) && !is_number(render_way[i - 1]) && !is_number(render_way[i + 1]))
					point_placement.push_back(render_way[i]);
			}
		}
		else if (render_way.size() && is_number(render_way[0]))
		{
			point_placement.push_back(render_way[0]);
		}
	}

	virtual void draw()
	{
		if (render_way == " ")
			return;

		float vertical_shift = 0.f;
		char back = 0;

		if (render_way.back() == '#' || render_way.back() == '~')
		{
			back = render_way.back();
			render_way.back() = ' ';
			switch (back)
			{
				case '#':
					vertical_shift = points[0].y - points[3].y;
					break;
				case '~':
					vertical_shift = (points[0].y - points[3].y) / 2;
					break;
			}
		}

		glColor4ub(R, G, B, A);
		glLineWidth(line_width);
		glPointSize(line_width);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < (int)render_way.length(); i++)
		{
			if (render_way[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			auto index = render_way[i] - '1';
			glVertex2f(x_pos + points[index].x, y_pos + points[index].y + vertical_shift);
		}
		glEnd();

		glBegin(GL_POINTS);
		for (int i = 0; i < (int)point_placement.size(); ++i)
			glVertex2f(x_pos + points[point_placement[i] - '1'].x, y_pos + points[point_placement[i] - '1'].y + vertical_shift);
		glEnd();

		if (back)
			render_way.back() = back;
	}

	void safe_position_change(float new_x_pos, float new_y_pos)
	{
		safe_char_move(new_x_pos - x_pos, new_y_pos - y_pos);
	}

	void safe_char_move(float dx, float dy)
	{
		x_pos += dx; y_pos += dy;
	}

	inline float x_unit_size() const
	{
		return points[1].x - points[0].x;
	}

	inline float y_unit_size() const
	{
		return points[3].y - points[0].y;
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

float lFONT_HEIGHT_TO_WIDTH = 2.5;
namespace lFontSymbolsInfo
{
	bool IsInitialised = false;
	GLuint CurrentFont = 0;
	HFONT SelectedFont = nullptr;
	std::int32_t Size = 15;

	std::int32_t PrevSize = Size;
	float Prev_lFONT_HEIGHT_TO_WIDTH = lFONT_HEIGHT_TO_WIDTH;
	std::string PrevFontName;

	struct lFontSymbInfosListDestructor
	{
		~lFontSymbInfosListDestructor()
		{
			glDeleteLists(CurrentFont, 256);
		}
	};

	lFontSymbInfosListDestructor __wFSILD{};

	void InitialiseFont(const std::string& FontName, bool force = false)
	{
		if (!force && FontName == PrevFontName && PrevSize == Size &&
			(std::abs)(Prev_lFONT_HEIGHT_TO_WIDTH - lFONT_HEIGHT_TO_WIDTH) < std::numeric_limits<float>::epsilon())
			return;

		PrevFontName = FontName;
		PrevSize = Size;
		Prev_lFONT_HEIGHT_TO_WIDTH = lFONT_HEIGHT_TO_WIDTH;

		if (!IsInitialised)
		{
			CurrentFont = glGenLists(256);
			IsInitialised = true;
		}

		auto height = Size * (base_internal_range / internal_range);
		auto width = (Size > 0) ? Size * (base_internal_range / internal_range) / lFONT_HEIGHT_TO_WIDTH : 0;

		SelectedFont = CreateFontA(
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
			FontName.c_str()
		);

		if (SelectedFont)
		{
			auto hdiobj = SelectObject(hDc, SelectedFont);
			auto status = wglUseFontBitmaps(hDc, 0, 255, CurrentFont);
			SetMapMode(hDc, MM_TEXT);
		}
	}

	inline void CallListOnChar(char C)
	{
		if (IsInitialised)
		{
			const char PsChStr[2] = { C, 0 };
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
			glCallLists(1, GL_UNSIGNED_BYTE, (const char*)(PsChStr));
			glPopAttrib();
		}
	}

	inline void CallListOnString(const std::string& S)
	{
		if (IsInitialised)
		{
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
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
		auto result = GetGlyphOutline(hDc, symb, GGO_GRAY8_BITMAP, &gm, 0, NULL, &lFontSymbolsInfo::MT);
	}

	void draw() override
	{
		if (!lFontSymbolsInfo::SelectedFont || !hDc)
			return;

		float pixel_size = (internal_range * 2) / window_base_width;

		if (fabsf(x_pos) + lFONT_HEIGHT_TO_WIDTH > pixel_size * wind_x / 2 ||
			fabsf(y_pos) + lFONT_HEIGHT_TO_WIDTH > pixel_size * wind_y / 2)
			return;

		glColor4ub(R, G, B, A);
		glRasterPos2f(x_pos, y_pos - y_unit_size() * 0.5);
		lFontSymbolsInfo::CallListOnChar(symb);
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

		float vertical_shift = 0.f;
		char back = 0;

		if (render_way.back() == '#' || render_way.back() == '~')
		{
			back = render_way.back();
			render_way.back() = ' ';
			switch (back)
			{
			case '#':
				vertical_shift = points[0].y - points[3].y;
				break;
			case '~':
				vertical_shift = (points[0].y - points[3].y) / 2;
				break;
			}
		}

		glLineWidth(line_width);
		glPointSize(line_width);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < (int)render_way.length(); i++)
		{
			if (render_way[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}

			auto index = render_way[i] - '1';
			glColor4ub(gR[index], gG[index], gB[index], gA[index]);
			glVertex2f(x_pos + points[index].x, y_pos + points[index].y + vertical_shift);
		}
		glEnd();

		glBegin(GL_POINTS);
		for (int i = 0; i < (int)point_placement.size(); ++i)
		{
			auto index = point_placement[i] - '1';
			glColor4ub(gR[index], gG[index], gB[index], gA[index]);
			glVertex2f(x_pos + points[index].x, y_pos + points[index].y + vertical_shift);
		}
		glEnd();

		if (back)
			render_way.back() = back;
	}
};

#endif
