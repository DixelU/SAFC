#pragma once
#ifndef SAFGUIF_HEADER
#define SAFGUIF_HEADER

//#include "../glew.h"
//#include "../freeglut.h"

//#include "consts.h"
#include <chrono>

#ifdef _MSC_VER
#include <Windows.h>
#endif
#include <string>
#include <vector>

constexpr int base_internal_range = 200;
float internal_range = base_internal_range, mouse_x_position = 0.f, mouse_y_position = 0.f;

#ifdef _MSC_VER
using std_unicode_string = std::wstring;
#else
using std_unicode_string = std::string;
#endif

const char* window_title = "SAFC\0";
std_unicode_string default_reg_path =
#ifdef _MSC_VER
	L"Software\\SAFC\\";
#else
	"";
#endif

std::string default_font_name = "Arial";
bool is_fonted = 
#ifdef _MSC_VER
	true;
#else
	false;
#endif

//#define ROT_ANGLE 0.7
#define TRY_CATCH(code,msg) try{code}catch(...){std::cout<<msg<<std::endl;}

//float dumb_rotation_angle = 0.f; // wtf
//#define RANGE 200
//constexpr int window_base_width = 720;
//constexpr int window_base_height = 720;

#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE __attribute__((always_inline))
#endif

constexpr float angle_to_radians(float a)
{
	return 0.0174532925f * a;
}

/*
inline float rotation_angle()
{
	return angle_to_radians(dumb_rotation_angle);
}
*/

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

/*
inline void __glcolor(std::uint32_t uINT)
{
	glColor4ub(((uINT & 0xFF000000) >> 24), ((uINT & 0xFF0000) >> 16), ((uINT & 0xFF00) >> 8), (uINT & 0xFF));
}
*/

/*
float WindX = window_base_width, WindY = window_base_height;

bool ANIMATION_IS_ACTIVE = 0, 
	 FIRSTBOOT = 1,
	 DRAG_OVER = 0, 
	 APRIL_FOOL = 0,
	 SHIFT_HELD = 0;


std::uint32_t TimerV = 0;
std::int16_t YearsOld = -1;
*/

#ifdef _MSC_VER
HWND hWnd;
HDC hDc;
auto HandCursor = ::LoadCursor(NULL, IDC_HAND), AllDirectCursor = ::LoadCursor(NULL, IDC_CROSS), NWSECursor = ::LoadCursor(NULL, IDC_SIZENWSE);///AAAAAAAAAAA
#endif

//const float singlepixwidth = (float)RANGE / WINDXSIZE;

//bool check_autoupdates = true;

void absoluteToActualCoords(int ix, int iy, float& x, float& y);
void inline rotate(float& x, float& y);

/*
size_t TimeCheckAndReturnTimeseed()
{
	auto now = std::chrono::system_clock::now();
	auto tt = std::chrono::system_clock::to_time_t(now);
	auto utc_tm = *gmtime(&tt);
	auto local_tm = *localtime(&tt);

	if (local_tm.tm_mon == 4 && local_tm.tm_mday == 1)
		APRIL_FOOL = 1;
	if (local_tm.tm_mon == 8 && local_tm.tm_mday == 31)
	{
		YearsOld = local_tm.tm_year - 2018;
	}
	return now.time_since_epoch().count();
}
*/

template<typename OnDestroyFunctor>
class OnDestroyExecutor
{
public:
	OnDestroyExecutor(OnDestroyFunctor&& func) : _f(std::move(func)) {}
	virtual ~OnDestroyExecutor() { _f(); }
private:
	OnDestroyFunctor _f;
};

template<typename OnDestroyFunctor>
OnDestroyExecutor<OnDestroyFunctor> makeOnDestroyExecutor(OnDestroyFunctor&& func)
{
	return OnDestroyExecutor<OnDestroyFunctor>(std::move(func));
}

void ThrowAlert_Error(std::string&& AlertText);
void ThrowAlert_Warning(std::string&& AlertText);
void AddFiles(const std::vector<std_unicode_string>& Filenames);
#pragma warning(disable : 4996)

#define MD_CASE(value) case (value): case ((value|1))
#define MT_CASE(value) MD_CASE(value): MD_CASE((value|2))
#define MO_CASE(value) MT_CASE(value): MT_CASE((value|4))
#define MH_CASE(value) MO_CASE(value): MO_CASE((value|8))

#endif // !SAFGUIF_HU
