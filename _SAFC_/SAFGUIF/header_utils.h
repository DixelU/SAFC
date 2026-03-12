#pragma once
#ifndef SAFGUIF_HEADER
#define SAFGUIF_HEADER

#include <GL/freeglut.h>

#include "../consts.h"

#include <Windows.h>
#include <string>
#include <vector>

constexpr int base_internal_range = 200;

float internal_range = base_internal_range, mouse_x_position = 0.f, mouse_y_position = 0.f;

const char* window_title = "SAFC\0";
std::wstring default_reg_path = L"Software\\SAFC\\";

std::string default_font_name = "Arial";
std::wstring saved_midi_device_name = L"";
bool is_fonted = true;

//#define ROT_ANGLE 0.7
#define TRY_CATCH(code,msg) try{code}catch(...){std::cout<<msg<<std::endl;}

float dumb_rotation_angle = 0.f;
//#define RANGE 200
constexpr int window_base_width = 720;
constexpr int window_base_height = 720;

#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE __attribute__((always_inline))
#endif

constexpr float angle_to_radians(float a)
{
	return 0.0174532925f * a;
}

inline float rotation_angle()
{
	return angle_to_radians(dumb_rotation_angle);
}

inline float rand_float(float range)
{
	return ((0 - range) + ((float)rand() / ((float)RAND_MAX / (2 * range))));
}

inline float rand_sign()
{
	return ((rand() & 1) ? -1.f : 1.f);
}

inline float __slowdprog(float a, float b, float progressrate)
{
	return ((a + (progressrate - 1) * b) / progressrate);
}

inline void __glcolor(std::uint32_t uINT)
{
	glColor4ub(((uINT & 0xFF000000) >> 24), ((uINT & 0xFF0000) >> 16), ((uINT & 0xFF00) >> 8), (uINT & 0xFF));
}

template<typename T>
inline void discard(T) {}

float wind_x = window_base_width, wind_y = window_base_height;

bool animation_is_active = false,
	 firstboot = true,
	 drag_over = false,
	 april_fool = false,
	 shift_held = false,
	 month_beginning = false;

std::uint32_t timer_v = 0;
std::int16_t years_old = -1;


HWND hWnd;
HDC hDc;
auto hand_cursor = ::LoadCursor(NULL, IDC_HAND), all_direct_cursor = ::LoadCursor(NULL, IDC_CROSS), nwse_cursor = ::LoadCursor(NULL, IDC_SIZENWSE);
//const float singlepixwidth = (float)RANGE / WINDXSIZE;

bool check_autoupdates = true;

struct simple_player;
struct midi_editor;
std::shared_ptr<simple_player> player;
std::shared_ptr<midi_editor> editor;

void absolute_to_actual_coords(int ix, int iy, float& x, float& y);
void inline rotate_view(float& x, float& y);

int collect_time_data()
{
	SYSTEMTIME t;
	GetLocalTime(&t);

	if (t.wMonth == 4 && t.wDay == 1)
		april_fool = true;
	if (t.wMonth == 8 && t.wDay == 31)
		years_old = t.wYear - 2018;
	if (t.wDay == 1)
		month_beginning = true;

	return t.wMilliseconds + (t.wSecond * 1000) + t.wMinute * 60000;
}

template<unsigned nx, unsigned ny>
unsigned point_in_poly(float (&vertx)[nx], float (&verty)[ny], float testx, float testy) requires (nx == ny)
{
	constexpr unsigned nvert = (nx + ny) >> 1;
	float minx = *vertx, miny = *verty, maxx = *vertx, maxy = *verty;
	for (unsigned i = 0; i < nvert; ++i)
	{
		minx = (std::min)(minx, vertx[i]);
		miny = (std::min)(miny, verty[i]);
		maxx = (std::max)(maxx, vertx[i]);
		maxy = (std::max)(maxy, verty[i]);
	}

	if (testx < minx || testx > maxx || testy < miny || testy > maxy)
		return 0;

	unsigned i, j, c = 0;
	for (i = 0, j = nvert - 1; i < nvert; j = i++)
	{
		if (((verty[i] > testy) != (verty[j] > testy)) &&
			(testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
			c ^= 1;
	}
	return c;
}

void throw_alert_error(std::string&& AlertText);
void throw_alert_warning(std::string&& AlertText);
void add_files(const std::vector<std::wstring>& Filenames);
#pragma warning(disable : 4996)

#endif // !SAFGUIF_HU
