#pragma once
#ifndef SAFGUIF_HEADER
#define SAFGUIF_HEADER

// #include <GL/glew.h>
#include <GL/freeglut.h>

#include "consts.h"

#include <Windows.h>
#include <string>
#include <vector>

constexpr int base_internal_range = 200;

float internal_range = base_internal_range, mouse_x_position = 0.f, mouse_y_position = 0.f;

const char* window_title = "SAFC\0";
std::wstring default_reg_path = L"Software\\SAFC\\";

std::string default_font_name = "Arial";
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


float WindX = window_base_width, WindY = window_base_height;

bool ANIMATION_IS_ACTIVE = 0, 
	 FIRSTBOOT = 1,
	 DRAG_OVER = 0, 
	 APRIL_FOOL = 0,
	 SHIFT_HELD = 0;

std::uint32_t TimerV = 0;
std::int16_t YearsOld = -1;

HWND hWnd;
HDC hDc;
auto HandCursor = ::LoadCursor(NULL, IDC_HAND), AllDirectCursor = ::LoadCursor(NULL, IDC_CROSS), NWSECursor = ::LoadCursor(NULL, IDC_SIZENWSE);///AAAAAAAAAAA

//const float singlepixwidth = (float)RANGE / WINDXSIZE;

bool check_autoupdates = true;

void absoluteToActualCoords(int ix, int iy, float& x, float& y);
void inline rotate(float& x, float& y);

int TIMESEED()
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	if (t.wMonth == 4 && t.wDay == 1)
		APRIL_FOOL = 1;
	if (t.wMonth == 8 && t.wDay == 31)
	{
		YearsOld = t.wYear - 2018;
	}
	return t.wMilliseconds + (t.wSecond * 1000) + t.wMinute * 60000;
}

void ThrowAlert_Error(std::string&& AlertText);
void ThrowAlert_Warning(std::string&& AlertText);
void AddFiles(const std::vector<std::wstring>& Filenames);
#pragma warning(disable : 4996)

#define MD_CASE(value) case (value): case ((value|1))
#define MT_CASE(value) MD_CASE(value): MD_CASE((value|2))
#define MO_CASE(value) MT_CASE(value): MT_CASE((value|4))
#define MH_CASE(value) MO_CASE(value): MO_CASE((value|8))

#endif // !SAFGUIF_HU
