#pragma once
#ifndef SAFGUIF_HEADER
#define SAFGUIF_HEADER

#include <GL/glew.h>
#include <GL/freeglut.h>

#include "consts.h"

#include <Windows.h>
#include <string>
#include <vector>

//#define NULL nullptr

typedef unsigned char BYTE;
typedef bool BIT;

#define BEG_RANGE 200
FLOAT RANGE = BEG_RANGE, MXPOS = 0.f, MYPOS = 0.f;

const char* WINDOWTITLE = "SAFC\0";
std::wstring RegPath = L"Software\\SAFC\\";

std::string FONTNAME = "Arial";
BIT is_fonted = 0;

//#define ROT_ANGLE 0.7
#define TRY_CATCH(code,msg) try{code}catch(...){std::cout<<msg<<std::endl;}

float ROT_ANGLE = 0.f;
#define ROT_RAD ANGTORAD(ROT_ANGLE)
//#define RANGE 200
#define WINDXSIZE 720
#define WINDYSIZE 720

#define ANGTORAD(a) (0.0174532925f*(a))
#define RANDFLOAT(range)  ((0 - range) + ((float)rand()/((float)RAND_MAX/(2*range))))
#define RANDSGN ((rand()&1)?-1:1)
#define SLOWDPROG(a,b,progressrate) ((a + (fract - 1)*b)/progressrate)

#define GLCOLOR(uINT) glColor4ub(((uINT & 0xFF000000) >> 24), ((uINT & 0xFF0000) >> 16), ((uINT & 0xFF00) >> 8), (uINT & 0xFF))

float WindX = WINDXSIZE, WindY = WINDYSIZE;

BIT ANIMATION_IS_ACTIVE = 0, FIRSTBOOT = 1, DRAG_OVER = 0, APRIL_FOOL = 0, SHIFT_HELD = 0;
DWORD TimerV = 0;
HWND hWnd;
HDC hDc;
auto HandCursor = ::LoadCursor(NULL, IDC_HAND), AllDirectCursor = ::LoadCursor(NULL, IDC_CROSS);///AAAAAAAAAAA
//const float singlepixwidth = (float)RANGE / WINDXSIZE;

BIT AutoUpdatesCheck = true;

void absoluteToActualCoords(int ix, int iy, float& x, float& y);
void inline rotate(float& x, float& y);
int TIMESEED() {
	SYSTEMTIME t;
	GetLocalTime(&t);
	if (t.wMonth == 4 && t.wDay == 1)APRIL_FOOL = 1;
	return t.wMilliseconds + (t.wSecond * 1000) + t.wMinute * 60000;
}
void ThrowAlert_Error(std::string AlertText);
void ThrowAlert_Warning(std::string AlertText);
void AddFiles(std::vector<std::wstring> Filenames);
#pragma warning(disable : 4996)

#define MD_CASE(value) case (value): case ((value|1))
#define MT_CASE(value) MD_CASE(value): MD_CASE((value|2))
#define MO_CASE(value) MT_CASE(value): MT_CASE((value|4))
#define MH_CASE(value) MO_CASE(value): MO_CASE((value|8))

#endif // !SAFGUIF_HU
