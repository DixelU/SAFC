#include "stdafx.h"
#include <ctime>
#include <random>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <list>
#include <Windows.h>
#include <iostream>

#include "glut.h"

#define BYTE unsigned char
#define PI 3.14159265358979323846264338328
#define ANGLE 2.5
#define DLENARROW 0.9
#define RES 5
#define ANGTORAD(a) (0.0174532925*(a))
#define RADTOANG(a) (57.2957795785*(a))
#define WINDSIZE 720

#define RANDFLOAT(range)  ((0 - range) + ((float)rand()/((float)RAND_MAX/(2*range)))) // from -range to range
#define RANDSIGN ((rand()&0x100)?(1):(-1))
#define GLCOLOR(UINT) glColor4f((float)((UINT & 0xFF000000) >> 24) / 256, (float)((UINT & 0xFF0000) >> 16) / 256, (float)((UINT & 0xFF00) >> 8) / 256, (float)(UINT & 0xFF) / 256)

bool ANIMATION_IS_ACTIVE = 0, FIRSTBOOT = 1;
using namespace std;

default_random_engine ___eng;
uniform_int_distribution<int> ___dis(0, 32768);
string wayto,dirto;
#define DRANDFLOAT(range)  ( (0 - range) + ( (float)___dis(___eng) )/( 16384. / range ) )
#define SLOWDPROG(a,b,fract) ((a + (fract - 1)*b)/fract)

string StringToDottedString(string Inp);
string FloatToDottedString(float F);
void NOP() {}
void NIL() {}

const string ASCII[] = {
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//NULLSYMB
	" ",//space
	"85 2",//!
	"74 96",//"
	"81 92 64",//#
	"974631 82",//$
	"74587 36523 91",//%
	"139714635",//&
	"85",//'
	"842",//(
	"862",//)
	"82 73 91 46",//*
	"46 82",//+
	"21",//,
	"46",//-
	"2",//.
	"81",// /
	"97139",//0
	"48213",//1
	"796413",//2
	"7965631",//3
	"74693",//4
	"974631",//5
	"9741364",//6
	"7952",//7
	"7931746",//8
	"1369746",//9
	"8 2",//:
	"8 12",//;
	"943",//<
	"46 13",//=
	"761",//>
	"7965 2",//?
	"317962486",//@
	"1793641",//A
	"17954135",//B
	"9713",//C
	"178621",//D
	"9745413",//E
	"974541",//F
	"971365",//G
	"174639",//H
	"798231",//I
	"79621",//J
	"95471453",//K
	"713",//L
	"17593",//M
	"1739",//N
	"97139",//O
	"17964",//P
	"179621 53",//Q
	"17964 53",//R
	"974631",//S
	"79 82",//T
	"712693",//U
	"729",//V
	"71539",//W
	"73 91",//X
	"75952",//Y
	"7913 46",//Z
	"9823",//[
	"72",// \//
	"7821",//]
	"486",//^
	"13",//_
	"75",//`
	"793146",//A
	"71364",//B
	"9713",//C
	"93146",//D
	"317964",//E
	"289 56",//F
	"139746",//G
	"71 463",//H
	"52 8",//I
	"521 8",//J
	"7159 53",//K
	"782",//L
	"17 482 593",//M
	"17 4893",//N
	"17931",//O
	"17964",//P
	"39746",//Q
	"17 489",//R
	"974631",//S
	"823 56",//T
	"712693",//U
	"729",//V
	"7125 239",//W
	"73 91",//X
	"759 15",//Y
	"7913",//Z
	"85452",//{
	"82",//|
	"85652",//}
	"48 59",//~
	"71937 97 31",// 
};

int TIMESEED() {
	SYSTEMTIME t;
	GetLocalTime(&t);
	return t.wMilliseconds + (t.wSecond * 1000) + t.wMinute * 60000;
}

struct __FLOATINGSEMITRASNPARENTTHINGS {
	float _cx, _cy, _cdx, _cdy, _dang, _cxi, _cyi, _cai;
	float _rad, _ang;
	int _angleamount;
	__FLOATINGSEMITRASNPARENTTHINGS(float x = 0, float y = 0, float size = 1, float angle = 0, float sides = 3) {
		_cx = x; _cy = y; _rad = size; _ang = angle; _angleamount = sides;
		_cdx = RANDFLOAT(0.1);
		_cdy = RANDFLOAT(0.1);
		_dang = RANDFLOAT(15);
		_cxi = _cyi = _cai = 0;
	}
	void Process(float dt = 0.1) {
		_ang += (_dang + (_cai = SLOWDPROG(RANDFLOAT(15) * 10, _cai, 64)))*dt;//smooth interuption
		_cx += (_cdx + (_cxi = SLOWDPROG(RANDFLOAT(0.1) * 10, _cxi, 64)))*dt;
		_cy += (_cdy + (_cyi = SLOWDPROG(RANDFLOAT(0.1) * 10, _cyi, 64)))*dt;
		if (sqrt(_cx*_cx + _cy * _cy) > 1.5 * RES) {
			_cx = 0 - _cx;
			_cy = 0 - _cy;
		}
	}
	void Draw() const {
		glBegin(GL_POLYGON);
		for (float i = 0 + _ang; i < 360. + _ang; i += 360. / _angleamount)
			glVertex2f(_rad*cos(ANGTORAD(i + _ang)) + _cx, _rad*sin(ANGTORAD(i + _ang)) + _cy);
		glEnd();
	}
};
struct __FLOATDCONTAINER {
	float _x, _y;
	__FLOATDCONTAINER(float x = 0, float y = 0) { _x = x; _y = y; }
	void Set(float x, float y) { _x = x; _y = y; }
};
struct __WAVE {
	float _hgt;
	float _freq, _offset;
	float _y, _wdth;
	float _doffset, _dhgt;
	float _hgti, _offi;
	__WAVE(float height = 1, float Frequency = 1, float Offset = 0, float Yposition = 0, float WaveWidth = 5) {
		_freq = Frequency; _offset = Offset; _y = Yposition; _wdth = WaveWidth; _hgt = height;
		_doffset = RANDFLOAT(0.1);
		_dhgt = 0;
		_hgti = _offi = 0;
	}
	void Process(float dt = 0.1) {
		_hgt += (_dhgt + (_hgti = SLOWDPROG(RANDFLOAT(0.05), _hgti, 32)))*dt;//becvuas ima lazye fuck
		_offset += (_doffset + (_offi = SLOWDPROG(RANDFLOAT(0.1), _offi, 16)))*dt;
	}
	void Draw() const {
		glLineWidth(_wdth);
		glBegin(GL_LINE_STRIP);
		for (float i = -RES; i < RES; i += 0.01) {
			glVertex2f(i, _hgt*sin(_freq*i + _offset) + _y);
		}
		glEnd();
		glLineWidth(1);
	}
};
struct __DOTTEDSYMBOLS {
	float _x, _y, _xus, _yus;//center (5)
	string _renderway;
	vector<char> _pointplacements;
	__FLOATDCONTAINER _relatedpoints[9];
	__DOTTEDSYMBOLS(string RenderWay = "", float x = 0, float y = 0, float xUnitSize = 0.15, float yUnitSize = 0.25) {
		_renderway = RenderWay; _x = x; _y = y; _xus = xUnitSize; _yus = yUnitSize;
		for (int i = -1; i <= 1; i++) {
			for (int j = -1; j <= 1; j++) {
				_relatedpoints[(i + 1) * 3 + (j + 1)].Set(_xus*(j), _yus*(i));
			}
		}
		if (RenderWay.size() > 1) {
			if (RenderWay[0] >= '0' && RenderWay[0] <= '9' && RenderWay[1] == ' ')_pointplacements.push_back(RenderWay[0]);
			if (RenderWay[RenderWay.size() - 1] >= '0' && RenderWay[RenderWay.size() - 1] <= '9' && RenderWay[RenderWay.size() - 2] == ' ')_pointplacements.push_back(RenderWay[RenderWay.size() - 1]);
			for (int i = 1; i < RenderWay.size()-1; ++i) {
				if (RenderWay[i] >= '0' && RenderWay[i] <= '9'  && RenderWay[i - 1] == ' ' && RenderWay[i + 1] == ' ')
					_pointplacements.push_back(RenderWay[i]);
			}
		}
		else if (RenderWay.size() && RenderWay[0] >= '0' && RenderWay[0] <= '9') {
			_pointplacements.push_back(RenderWay[0]);
		}
	}
	void Draw() {
		glBegin(GL_LINE_STRIP);
		BYTE IO;
		for (int i = 0; i < _renderway.length(); i++) {
			if (_renderway[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = _renderway[i] - '1';
			glVertex2f(_x + _relatedpoints[IO]._x, _y + _relatedpoints[IO]._y);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < _pointplacements.size(); ++i) {
			//cout << (int)_pointplacements[i] << endl;
			glVertex2f(_x + _relatedpoints[_pointplacements[i]-'1']._x, _y + _relatedpoints[_pointplacements[i] - '1']._y);
		}
		glEnd();
	}
};
struct __BUTTON {
	__DOTTEDSYMBOLS *text;
	unsigned int _COLORBCKG;
	unsigned int _HOVEREDCOLORBCKG;
	unsigned int _COLORTXT;
	unsigned int _HOVEREDCOLORTXT;
	unsigned char _CLINEWIDTH;
	unsigned char _HOVEREDCLINEWIDTH;
	float _x, _y;
	float _h, _w, _tmargin, _bmargin;
	unsigned int _textlen;
	void(*__OnClick)();
	bool _HD;//hovered
	__BUTTON(void) {}
	__BUTTON(void(*OnClick)(), string FormSymbStr = "", float xPos = 0, float yPos = 0, float heigth = 0, float width = 0, float BoxMargin = 0.05,
		float TMargin = 0.050, float SymbW = 0.15, float SymbH = 0.25, float CLINEWIDTH = 1, unsigned int BckgColorRGBA = 0x3F3F3F7F,
		unsigned int TxtColorRGBA = 0xFFFFFFEE) {
		_HD = false;
		float OffsetIncrementor;
		vector<string> FSS;
		string T;
		FSS.clear();
		T.clear();
		__OnClick = OnClick;
		_x = xPos; _y = yPos;
		_h = max(SymbH, heigth);
		_CLINEWIDTH = _HOVEREDCLINEWIDTH = CLINEWIDTH;
		_COLORBCKG = _HOVEREDCOLORBCKG = BckgColorRGBA;
		_COLORTXT = _HOVEREDCOLORTXT = TxtColorRGBA;
		if (FormSymbStr == "")FormSymbStr = "5";
		_tmargin = TMargin; _bmargin = BoxMargin;
		for (int i = 0; i < FormSymbStr.length(); i++) {
			if ((FormSymbStr[i] >= '1' && FormSymbStr[i] <= '9') || (FormSymbStr[i] == ' '))T.push_back(FormSymbStr[i]);
			else {
				FSS.push_back(T);
				T.clear();
			}
		}
		if (!FSS.empty())FSS.push_back(T);
		_w = max(FSS.size()*(SymbW + TMargin), width);
		text = new __DOTTEDSYMBOLS[FSS.size()];
		_textlen = FSS.size();
		OffsetIncrementor = (_w - FSS.size()*(SymbW + TMargin))*0.5 + 0.5*SymbW;
		for (int i = 0; i < FSS.size(); i++) {
			text[i] = __DOTTEDSYMBOLS(FSS[i], _x + OffsetIncrementor, _y - 0.5*_h, SymbW / 2, SymbH / 2);
			OffsetIncrementor += (SymbW + TMargin);
		}
	}
	void HoverSettings(unsigned int BckgColor = 0x2222226F, unsigned int TxtColor = 0xFFFFFFFF, unsigned int TxtLineWidth = 2) {
		_HOVEREDCOLORBCKG = BckgColor; _HOVEREDCOLORTXT = TxtColor; _HOVEREDCLINEWIDTH = TxtLineWidth;
	}
	void Draw() {
		//glColor4f(1, 1, 1, 0.5);
		if (!_HD)GLCOLOR(_COLORBCKG);
		else GLCOLOR(_HOVEREDCOLORBCKG);
		glBegin(GL_QUADS);
		glVertex2f(_x - _bmargin, _y + _bmargin);
		glVertex2f(_x + _w + _bmargin, _y + _bmargin);
		glVertex2f(_x + _w + _bmargin, _y - _h - _bmargin);
		glVertex2f(_x - _bmargin, _y - _h - _bmargin);
		//printf("%f %f %f %f\n", _x, _x + _w, _y, _y - _h);
		//glVertex2f(_x     , _y     );
		glEnd();
		if (!_HD) {
			glLineWidth(_CLINEWIDTH);
			glPointSize(_CLINEWIDTH);
		}
		else {
			glLineWidth(_HOVEREDCLINEWIDTH);
			glPointSize(_HOVEREDCLINEWIDTH);
		}
		if (!_HD)GLCOLOR(_COLORTXT);
		else GLCOLOR(_HOVEREDCOLORTXT);
		for (int i = 0; i < _textlen; i++) {
			text[i].Draw();
		}
	}
};
struct __BUTTONHANDLER {
	vector<__BUTTON> _allbutts;
	vector<string> _buttname;
	float CMX, CMY;//Current internal cursor position
	void AddButton(__BUTTON B,string ButtonName) {
		_allbutts.push_back(B);
		_buttname.push_back(ButtonName);
	}
	void MouseMove(int ix, int iy) {
		bool HOVERED = 0;
		float x, y, wh = glutGet(GLUT_WINDOW_HEIGHT), ww = glutGet(GLUT_WINDOW_WIDTH);
		x = ((float)(ix - ww * 0.5)) / (0.5*(ww / RES));
		y = ((float)(0 - iy + wh * 0.5)) / (0.5*(wh / RES));
		for (int i = 0; i < _allbutts.size(); i++) {
			_allbutts[i]._HD =
				(x >= _allbutts[i]._x - _allbutts[i]._bmargin) &&
				(x <= _allbutts[i]._x + _allbutts[i]._w + _allbutts[i]._bmargin) &&
				(y >= _allbutts[i]._y - _allbutts[i]._h - _allbutts[i]._bmargin) &&
				(y <= _allbutts[i]._y + _allbutts[i]._bmargin);
			if (_allbutts[i]._HD)HOVERED = 1;
			//cout << _allbutts[i]._HD << endl;
		}
		if (HOVERED)glutSetCursor(GLUT_CURSOR_HELP);
		else glutSetCursor(GLUT_CURSOR_INHERIT);
		CMX = x; CMY = y;
	}
	void MouseClick(int ix, int iy) {
		float x, y, wh = glutGet(GLUT_WINDOW_HEIGHT), ww = glutGet(GLUT_WINDOW_WIDTH);
		x = ((float)(ix - ww * 0.5)) / (0.5*(ww / RES));
		y = ((float)(0 - iy + wh * 0.5)) / (0.5*(wh / RES));
		for (int i = 0; i < _allbutts.size(); i++) {
			if ((x >= _allbutts[i]._x - _allbutts[i]._bmargin) &&
				(x <= _allbutts[i]._x + _allbutts[i]._w + _allbutts[i]._bmargin) &&
				(y >= _allbutts[i]._y - _allbutts[i]._h - _allbutts[i]._bmargin) &&
				(y <= _allbutts[i]._y + _allbutts[i]._bmargin)) {
				_allbutts[i].__OnClick();
			}
		}
	}
	void DrawAll() {
		for (int i = 0; i < _allbutts.size(); i++) {
			_allbutts[i].Draw();
		}
	}
	__BUTTON& operator[](int n) {
		return _allbutts[n];
	}
};
struct __TEXTLINE {
	vector<__DOTTEDSYMBOLS> _text;
	unsigned int _TXTCOL;
	float _x, _y;
	int _chwd;
	__TEXTLINE() { _TXTCOL = 0; _x = 0; _y = 0; _text.clear(); }
	__TEXTLINE(string DottedString, float Xpos, float Ypos,
		float SymbH, float SymbW, float SymbMargin = 0.05, int SymbLineWidth = 1, unsigned int TextRGBA = 0xFFFFFFFF) {
		vector<string> FSS;
		string T;
		float OffsetIncrementor = 0;
		FSS.clear();
		T.clear();
		_x = Xpos; _y = Ypos;
		_chwd = SymbLineWidth;
		_TXTCOL = TextRGBA;
		if (DottedString == "")DottedString = "5";
		for (int i = 0; i < DottedString.length(); i++) {
			if ((DottedString[i] >= '1' && DottedString[i] <= '9') || (DottedString[i] == ' '))T.push_back(DottedString[i]);
			else {
				FSS.push_back(T);
				T.clear();
			}
		}
		if (T.size() > 0)FSS.push_back(T);
		for (int i = 0; i < FSS.size(); i++) {
			_text.push_back(__DOTTEDSYMBOLS(FSS[i], _x + OffsetIncrementor + SymbW * 0.5, _y - 0.5*SymbH, SymbW *0.5, SymbH *0.5));
			OffsetIncrementor += (SymbW + SymbMargin);
		}
	}
	void Draw() {
		GLCOLOR(_TXTCOL);
		glLineWidth(_chwd);
		glPointSize(_chwd);
		for (int i = 0; i < _text.size(); i++) {
			_text[i].Draw();
		}
	}
};
struct __TEXTFRAME {
	float _h, _w;
	float _x, _y;
	unsigned int _BCKGCOLOR;
	map<string,__TEXTLINE> _lines;
	__TEXTFRAME(float Xpos = 0, float Ypos = 0, float Heigth = 0, float Width = 0, unsigned int BackgroundRGBA = 0x000000AF) {
		_h = Heigth; _w = Width; _x = Xpos; _y = Ypos; _BCKGCOLOR = BackgroundRGBA;
	}
	void AddNewLine(string Name,string Text, float Left, float Top, float FontHeight, int TextLineWidth = 1, unsigned int Color = 0xFFFFFFFF) {
		Text = StringToDottedString(Text);
		_lines.insert(pair<string,__TEXTLINE>(
			Name,
			__TEXTLINE(Text, _x + Left, _y - Top, FontHeight, FontHeight*0.66, FontHeight*0.1, TextLineWidth, Color)
			)
		);
	}
	void AddModifiedLine(string Name,string ReplaceLine, __TEXTLINE NonEmptyTextline, float TMargin=0.05 ) {
		ReplaceLine = StringToDottedString(ReplaceLine);
		_lines.insert(pair<string, __TEXTLINE>(
			Name,
			__TEXTLINE(
				ReplaceLine, NonEmptyTextline._x, NonEmptyTextline._y,
				NonEmptyTextline._text[0]._yus * 2, NonEmptyTextline._text[0]._xus * 2,
				TMargin, NonEmptyTextline._chwd, NonEmptyTextline._TXTCOL
			)
			)
		);
	}
	void RemoveNamedLine(string Name) {_lines.erase(Name);}
	__TEXTLINE& GetNamedLine(string Name) {
		return (*(_lines.find(Name))).second;
	}
	void Draw() {
		map<string,__TEXTLINE>::iterator Y = _lines.begin();
		GLCOLOR(_BCKGCOLOR);
		glBegin(GL_QUADS);
		glVertex2f(_x, _y);
		glVertex2f(_x + _w, _y);
		glVertex2f(_x + _w, _y - _h);
		glVertex2f(_x, _y - _h);
		glEnd();
		for (int i = 0; i < _lines.size(); i++,Y++) {
			(*Y).second.Draw();
		}
	}
};
struct __INPUTALERT {
	#define OTEXTLINECENTER(STR,Y,H,W,SM,LW,COL) __TEXTLINE(StringToDottedString(STR), (0 - 0.5*( W + SM )*STR.length()), Y, H, W, SM, LW,COL)
	#define TEXTLINECENTER(STR,Y,H,LW,COL) __TEXTLINE(StringToDottedString(STR), (0 - 0.5*( H*0.9 )*STR.length()), Y, H, H*0.6, H*0.3, LW,COL)
	__TEXTLINE _Header;//FontHeight, FontHeight*0.66, FontHeight*0.1,
	__TEXTLINE _Commentary;
	__TEXTLINE _INPUTAREA;
	__TEXTLINE _AdditionalCommentary;
	string INPUTVAL;
	void(*_OnSubmit)();
	bool NumsOnly;
	__INPUTALERT(void(*OnSubmit)() = NIL, string Header = "", string DefaultInput = "", bool NumbersOnly = 0) {
		INPUTVAL = DefaultInput;
		NumsOnly = NumbersOnly;
		_OnSubmit = OnSubmit;
		_INPUTAREA = TEXTLINECENTER(INPUTVAL, 0.5, 1, 5, 0xFFFFFFFF);
		DefaultInput = "PRESS ENTER TO SUMBIT OR ESC TO CANCEL";
		_AdditionalCommentary = TEXTLINECENTER(DefaultInput, 1.80, 0.2, 2, 0xFF7F00FF);
		_Header = TEXTLINECENTER(Header, 1.5, 0.7, 4, 0xAAAAAAFF);
		DefaultInput = (NumbersOnly ? "NUMBERS ONLY" : "PLAIN TEXT");
		_Commentary = TEXTLINECENTER(DefaultInput, -1, 0.25, 2, 0x007FFFFF);
	}
	void AddNewSymbol(char C) {
		INPUTVAL.push_back(C);
		_INPUTAREA = TEXTLINECENTER(INPUTVAL, 0.5, 1, 5, 0xFFFFFFFF);
	}
	void RemoveOneSymbol() {
		if (INPUTVAL.length()) {
			INPUTVAL.pop_back();
			_INPUTAREA = TEXTLINECENTER(INPUTVAL, 0.5, 1, 5, 0xFFFFFFFF);
		}
	}
	void Draw() {
		glColor4f(0, 0, 0, 0.75);
		glBegin(GL_QUADS);
		glVertex2f(RES, RES);
		glVertex2f(-RES, RES);
		glVertex2f(-RES, -RES);
		glVertex2f(RES, -RES);
		glEnd();
		_Header.Draw();
		_Commentary.Draw();
		_INPUTAREA.Draw();
		_AdditionalCommentary.Draw();
	}
	#undef TEXTLINECENTER
	#undef OTEXTLINECENTER
};
struct __MAINFRAME {
	float _w, _h;
	__BUTTONHANDLER _butts;
	__INPUTALERT _input;
	__TEXTFRAME _text;
	unsigned int _BACKGROUND;
	bool InputIsActive;
	__MAINFRAME(float width = RES * 2, float heigth = RES * 2, unsigned int BackgroundRGBA = 0x0000003F) { _w = width; _h = heigth; InputIsActive = 0; _BACKGROUND = BackgroundRGBA; }
	void AddButton(void(*OnClick)(),string ButtonName, string FormSymbStr = "", float xPos = 0, float yPos = 0, float heigth = 0, float width = 0, float BoxMargin = 0.05,
		float TMargin = 0.050, float SymbW = 0.15, float SymbH = 0.25, float CLINEWIDTH = 1, unsigned int BckgColorRGBA = 0x3F3F3F7F,
		unsigned int TxtColorRGBA = 0xFFFFFFEE) {
		_butts.AddButton(__BUTTON(OnClick, FormSymbStr, xPos, yPos, heigth, width, BoxMargin, TMargin, SymbW, SymbH, CLINEWIDTH, BckgColorRGBA, TxtColorRGBA), ButtonName);
	}
	void TopButtonHoverSettings(unsigned int BackgroundRGBA, unsigned int TextRGBA, int TextLineWidth) {
		_butts[_butts._allbutts.size() - 1].HoverSettings(BackgroundRGBA, TextRGBA, TextLineWidth);
	}
	void InitNewTextFrame(float Xpos, float Ypos, float Height, float Width, unsigned int BackgroundRGBA) {
		_text = __TEXTFRAME(Xpos, Ypos, Height, Width, BackgroundRGBA);
	}
	void AddTextToFrame(string LineLabel,string Text, float RelLeft, float RelTop, float FontHeight, int TextLineWidth, unsigned int BackgroundRGBA) {
		_text.AddNewLine(LineLabel,Text, RelLeft, RelTop, FontHeight, TextLineWidth, BackgroundRGBA);
	}
	void MouseClick(int ix, int iy) {if (!InputIsActive)_butts.MouseClick(ix, iy);}
	void MouseMove(int ix, int iy) { if (!InputIsActive)_butts.MouseMove(ix, iy); else _butts.MouseMove(0, 0);}
	void AddModifiedLine(string LineLabel,string ReplaceLine, __TEXTLINE NonEmptyTextline, float TMargin = 0.05) {
		_text.AddModifiedLine(LineLabel,ReplaceLine, NonEmptyTextline, TMargin);
	}
	void ChangeButtonLabel(string ButtonName, string NewButtonLabel) {
		for (int i = 0; i < _butts._allbutts.size(); i++) {
			if (ButtonName == _butts._buttname[i]) {
				NewButtonLabel = StringToDottedString(NewButtonLabel);
				unsigned int a= _butts[i]._HOVEREDCLINEWIDTH, b= _butts[i]._HOVEREDCOLORBCKG, c= _butts[i]._HOVEREDCOLORTXT;
				_butts[i] = __BUTTON(_butts[i].__OnClick, NewButtonLabel, _butts[i]._x, _butts[i]._y, _butts[i]._h, _butts[i]._w,
					_butts[i]._bmargin, _butts[i]._tmargin, _butts[i].text[0]._xus*2, _butts[i].text[0]._yus * 2, 
					_butts[i]._CLINEWIDTH, _butts[i]._COLORBCKG, _butts[i]._COLORTXT
					);
				_butts[i]._HOVEREDCLINEWIDTH = a; _butts[i]._HOVEREDCOLORBCKG = b; _butts[i]._HOVEREDCOLORTXT = c;
				break;
			}
		}
	}
	__BUTTON& GetAccessToButton(string ButtonName) {
		for (int i = 0; i < _butts._allbutts.size(); i++) {
			if (ButtonName == _butts._buttname[i]) {
				return _butts[i];
			}
		}
	}
	void RemoveNamedButton(string ButtonName) {
		for (int i = 0; i < _butts._allbutts.size(); i++) {
			if (ButtonName == _butts._buttname[i]) {
				_butts._allbutts.erase(_butts._allbutts.begin() + i);
				_butts._buttname.erase(_butts._buttname.begin() + i);
				return;
			}
		}
	}
	void RemoveNamedLine(string Name) {	_text.RemoveNamedLine(Name);}
	__TEXTLINE& GetNamedLine(string Name) {	return _text.GetNamedLine(Name);}
	void KeyboardActiivity(char CH) {
		if (InputIsActive) {
			if (CH == 13) {//ENTER
				_input._OnSubmit();
			}
			else if (CH == 27) {//ESC
				InputIsActive = !InputIsActive;
			}
			else if (CH == 8) {//BACKSPACE
				_input.RemoveOneSymbol();
			}
			else if(CH>31 && CH<128) {
				if (_input.NumsOnly) {
					if (CH >= '0' && CH <= '9')_input.AddNewSymbol(CH);
				}
				else _input.AddNewSymbol(CH);
			}
		}
	}
	void TriggerAnInput(__INPUTALERT InputAlert) {
		InputIsActive = 1;
		_input = InputAlert;
	}
	void TerminateInput() {
		InputIsActive = 0;
	}
	string GetStringFromInput() {
		if (InputIsActive) {
			return _input.INPUTVAL;
		}
		else return "";
	}
	void Draw() {
		GLCOLOR(_BACKGROUND);
		glBegin(GL_QUADS);
		glVertex2f(0 - 0.5*_w, 0.5*_h);
		glVertex2f(0.5*_w, 0.5*_h);
		glVertex2f(0.5*_w, 0 - 0.5*_h);
		glVertex2f(0 - 0.5*_w, 0 - 0.5*_h);
		glEnd();
		_text.Draw();
		_butts.DrawAll();
		if (InputIsActive)_input.Draw();
	}
};
struct __GLOBALFRAME {
	vector<__FLOATINGSEMITRASNPARENTTHINGS> _particles;
	unsigned int _pCOLOR;
	vector<__WAVE> _waves;
	unsigned int _wCOLOR;
	__MAINFRAME _stuffs;
	map<string, string> USERDATA;
	__GLOBALFRAME(int ParticleCount, unsigned int ParticleColorRGBA, int WaveCount, unsigned int WaveColorRGBA) {
		_pCOLOR = ParticleColorRGBA;
		_wCOLOR = WaveColorRGBA;
		for (int i = 0; i < ParticleCount; i++) {
			_particles.push_back(__FLOATINGSEMITRASNPARENTTHINGS(RANDFLOAT(RES), RANDFLOAT(RES),
				RANDFLOAT(1)*0.25, RANDFLOAT(360), rand() % 7 + 3));
		}
		for (int i = 0; i < WaveCount; i++) {
			_waves.push_back(__WAVE(RANDFLOAT(1)*0.5, RANDFLOAT(1)*0.5, RANDFLOAT(PI), RANDFLOAT(RES - 1), RANDFLOAT(25) + 5));
		}
		_stuffs = __MAINFRAME();
	}
	void AddButton(void(*OnClick)(),string ButtonName, string Text = "", float xPos = 0, float yPos = 0, float heigth = 0, float width = 0, float BoxMargin = 0.05,
		float TMargin = 0.050, float SymbW = 0.15, float SymbH = 0.25, float CLINEWIDTH = 1, unsigned int BckgColorRGBA = 0x3F3F3F7F,
		unsigned int TxtColorRGBA = 0xFFFFFFEE) {
		_stuffs.AddButton(OnClick, ButtonName, StringToDottedString(Text), xPos, yPos, heigth, width, BoxMargin, TMargin, SymbW, SymbH, CLINEWIDTH, BckgColorRGBA, TxtColorRGBA);
	}
	void InitNewTextFrame(float Xpos, float Ypos, float Height, int Width, unsigned int BackgroundRGBA) {
		_stuffs.InitNewTextFrame(Xpos, Ypos, Height, Width, BackgroundRGBA);
	}
	void AddTextToFrame(string Label,string Text, float RelLeft, float RelTop, float FontHeight, float TextLineWidth, unsigned int BackgroundRGBA) {
		_stuffs.AddTextToFrame(Label,Text, RelLeft, RelTop, FontHeight, TextLineWidth, BackgroundRGBA);
	}
	void AddModifiedTextLine(string Label,string ReplaceLine, __TEXTLINE NonEmptyTextline, float TMargin = 0.05) {
		_stuffs.AddModifiedLine(Label,ReplaceLine, NonEmptyTextline, TMargin);
	}
	void RemoveNamedLine(string Name) {	_stuffs.RemoveNamedLine(Name);}
	__TEXTLINE GetNamedLine(string Name) {return _stuffs.GetNamedLine(Name);}
	void MouseClick(int ix, int iy) { _stuffs.MouseClick(ix, iy); }
	void MouseMove(int ix, int iy) { _stuffs.MouseMove(ix, iy); }
	void KeyboardActiivity(char CH) { _stuffs.KeyboardActiivity(CH); }
	void TriggerAnInput(__INPUTALERT InputAlert) { _stuffs.TriggerAnInput(InputAlert); }
	void TerminateInput() { _stuffs.TerminateInput(); }
	void TopButtonHoverSettings(unsigned int BackgroundRGBA, unsigned int TextRGBA, int TextLineWidth) {
		_stuffs.TopButtonHoverSettings(BackgroundRGBA, TextRGBA, TextLineWidth);
	}
	void ChangeButtonLabel(string ButtonName, string NewButtonLabel) {
		_stuffs.ChangeButtonLabel(ButtonName, NewButtonLabel);
	}
	__BUTTON& GetAccessToButton(string ButtonName) {
		return _stuffs.GetAccessToButton(ButtonName);
	}
	void RemoveNamedButton(string ButtonName) {
		_stuffs.RemoveNamedButton(ButtonName);
	}
	string GetStringFromInput() {return _stuffs.GetStringFromInput();}
	void Draw() {
		for (int i = 0; i < _particles.size(); i++) {
			GLCOLOR(_pCOLOR);
			_particles[i].Process();
			_particles[i].Draw();
		}
		for (int i = 0; i < _waves.size(); i++) {
			GLCOLOR(_wCOLOR);
			_waves[i].Process();
			_waves[i].Draw();
		}
		_stuffs.Draw();
	}
};
/////////////////////////////////////////////
/////////ACTUAL FRAMEWORK USAGE//////////////
/////////////////////////////////////////////

__GLOBALFRAME GF(50, 0x7FFF7F7F, 7, 0x3FCFFF7F);

struct INTERNALINFO {
	list<string> FILES;
	list<unsigned int> OFFSETS;
	string SaveTo;
	short SelectedFileID;
	unsigned short PPQ;
	unsigned int NewTempo;
	bool ProgORD, RemRem,Log;
	void Init() {SelectedFileID = PPQ = NewTempo = ProgORD = RemRem = Log = 0;}
	void InsertNewFile(string FileLink) {
		FILES.push_back(FileLink);
		OFFSETS.push_back(0);
	}
	unsigned int GetOffsetInIth(unsigned int I) {
		list<unsigned int>::iterator Y = OFFSETS.begin();
		advance(Y, I);
		return *Y;
	}
	void ChangeOffsetInIth(unsigned int I, unsigned int NewOffset=0) {
		list<unsigned int>::iterator Y = OFFSETS.begin();
		advance(Y, I);
		(*Y) = NewOffset;
	}
	void RemoveIthFile(unsigned int I) {
		list<unsigned int>::iterator Y = OFFSETS.begin();
		list<string>::iterator U = FILES.begin();
		advance(Y, I);
		advance(U, I);
		FILES.erase(U);
		OFFSETS.erase(Y);
	}
};
INTERNALINFO IF;
////OLD STUFFS
void CreateMess(const char abc[], const char header[]) {
	MessageBoxA(0, abc, header, MB_OK);
}

UINT ALERTCLICKCOUNTER;
void onCloseAlert() {
	++ALERTCLICKCOUNTER;
	if (ALERTCLICKCOUNTER > 1) {
		GF.RemoveNamedButton("ALERT");
		GF.RemoveNamedButton("ALERTTIP");
	}

}
void CreateAlert(string AlertText) {
	ALERTCLICKCOUNTER = 0;
	GF.AddButton(onCloseAlert, "ALERT", AlertText, -5, 5, 10, 10, 0.5, 0.1, 0.25, 0.375, 3, 0xFF00003F, 0xFFFFFFFF);
	GF.AddButton(NOP, "ALERTTIP", "CLICK ANYWHERE TO CLOSE", -5, 3, 0, 10, 0.25, 0.05, 0.15, 0.25, 2, 0xFFFF007F, 0xFF);
}

vector<string> MOFD(char* Title) {
	OPENFILENAME ofn;       // common dialog box structure
	char szFile[50000];       // buffer for file name
	HWND hwnd;              // owner window
	HANDLE hf;              // file handle
	vector<string> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000);
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "MIDI Files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	if (GetOpenFileName(&ofn)) {
		string Link = "", Gen = "";
		int i = 0, counter = 0;
		for (; i < 600 && szFile[i] != '\0'; i++) {
			Link.push_back(szFile[i]);
		}
		for (; i < 49998;) {
			counter++;
			Gen = "";
			for (; i < 49998 && szFile[i] != '\0'; i++) {
				Gen.push_back(szFile[i]);
			}
			i++;
			if (szFile[i] == '\0') {
				if (counter == 1) InpLinks.push_back(Link);
				else InpLinks.push_back(Link + "\\" + Gen);
				break;
			}
			else {
				if (Gen != "")InpLinks.push_back(Link + "\\" + Gen);
			}
		}
		return InpLinks;
	}
	else {
		switch (CommDlgExtendedError()) {
			case CDERR_DIALOGFAILURE:		 CreateAlert("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:		 CreateAlert("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	 CreateAlert("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	 CreateAlert("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	 CreateAlert("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	 CreateAlert("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	 CreateAlert("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	 CreateAlert("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		 CreateAlert("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:			 CreateAlert("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		 CreateAlert("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		 CreateAlert("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	 CreateAlert("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	 CreateAlert("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	 CreateAlert("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return vector<string>{""};
	}
}
string OFD(char* Title) {
	char filename[MAX_PATH];
	OPENFILENAME ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = "MIDI Files(*.mid)\0*.mid\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = Title;
	ofn.Flags = OFN_PATHMUSTEXIST;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if (GetOpenFileNameA(&ofn)) return string(filename);
	else{
		switch (CommDlgExtendedError())	{
			case CDERR_DIALOGFAILURE:		 CreateAlert("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:		 CreateAlert("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	 CreateAlert("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	 CreateAlert("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	 CreateAlert("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	 CreateAlert("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	 CreateAlert("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	 CreateAlert("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		 CreateAlert("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:			 CreateAlert("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		 CreateAlert("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		 CreateAlert("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	 CreateAlert("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	 CreateAlert("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	 CreateAlert("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return "";
	}
}
/////

/*void OnEnterPress() {
	__TEXTLINE TL = GF.GetNamedLine("FirstText");
	GF.RemoveNamedLine("FirstText");
	GF.AddModifiedTextLine("FirstText",GF.GetStringFromInput(),TL);
	GF.TerminateInput();
}
void onButtClick() {
	GF.TriggerAnInput(__INPUTALERT(OnEnterPress, "INPUT FRAME", "5", false));
}*/

#define INLISTHEIGTH 0.125
#define INLISTTMARGIN 0.075
#define INLISTBMARGIN 0.05
#define INLISTADDITIONALDELTA 0.025
#define INLISTTOPSTRINGSTART 4.25

__FLOATDCONTAINER GetCurMousePos() {
	return __FLOATDCONTAINER(GF._stuffs._butts.CMX, GF._stuffs._butts.CMY);
}
void TrackListClick() {
	__FLOATDCONTAINER CMP(GetCurMousePos());
	float ay = ((INLISTTOPSTRINGSTART - CMP._y) / ((INLISTHEIGTH + INLISTBMARGIN*2 + INLISTADDITIONALDELTA)))+0.33;
	__BUTTON	*nhbtn = &GF.GetAccessToButton("B" + to_string((int)ceil(ay))),
				*ohbtn = &GF.GetAccessToButton("B" + to_string(IF.SelectedFileID));
	unsigned int CT, BCG;
	cout << (int)ceil(ay) << " " << IF.SelectedFileID << endl;
	BCG = nhbtn->_HOVEREDCOLORBCKG;
	CT = nhbtn->_HOVEREDCOLORTXT;
	nhbtn->_HOVEREDCOLORBCKG = nhbtn->_COLORBCKG;
	nhbtn->_HOVEREDCOLORTXT = nhbtn->_COLORTXT;
	nhbtn->_COLORBCKG = BCG;
	nhbtn->_COLORTXT = CT;
	if (IF.SelectedFileID>0) {
		BCG = ohbtn->_HOVEREDCOLORBCKG;
		CT = ohbtn->_HOVEREDCOLORTXT;
		ohbtn->_HOVEREDCOLORBCKG = ohbtn->_COLORBCKG;
		ohbtn->_HOVEREDCOLORTXT = ohbtn->_COLORTXT;
		ohbtn->_COLORBCKG = BCG;
		ohbtn->_COLORTXT = CT;
	}
	IF.SelectedFileID = (int)ceil(ay);
}
void onAdd() {
	vector<string> Files=MOFD("Adding MIDIs to the list\0");
	string BT;
	if (!(Files.size() <= 1 && Files[0] == "")){
		float ButtOffset;
		short Flag;
		for (int i = 0; i < Files.size(); i++) {
			ButtOffset = (INLISTHEIGTH + INLISTBMARGIN*2 + INLISTADDITIONALDELTA)*IF.FILES.size();
			IF.InsertNewFile(Files[i]); 
			BT = Files[i];
			Flag = 0;
			for (int q = BT.size() - 1; q >= 0; q--) {
				if (BT[q] == '\\')Flag++;
				if (Flag >= 2 && BT[q] == '\\') {
					Flag = q;
					break;
				}
			}
			BT.erase(0, Flag);
			if (BT.size() > 45)BT.erase(45, BT.size() - 45);
			if (INLISTTOPSTRINGSTART - ButtOffset >= -4.15) {
				GF.AddButton(TrackListClick, "B" + to_string((int)IF.FILES.size()), BT, -INLISTTOPSTRINGSTART, INLISTTOPSTRINGSTART - ButtOffset,0,0,
					INLISTBMARGIN, INLISTTMARGIN, INLISTTMARGIN, INLISTHEIGTH,
					2,0xCFFCFF1F,0xCFFFCFCF);
				GF.TopButtonHoverSettings(0xCFFFCFCF, 0x000F00CF, 2);
			}
		}
	}
}

void onEnterInOffsetInputAlert() {
	string InputString = GF.GetStringFromInput();
	if (InputString == "")InputString = "0";
	else if (InputString.size() > 8)InputString = "999999999";
	IF.ChangeOffsetInIth(IF.SelectedFileID-1, stoi(InputString));
	GF.TerminateInput();
}
void onOffset() {
	if(IF.SelectedFileID <= IF.FILES.size() && IF.SelectedFileID>0)
		GF.TriggerAnInput(__INPUTALERT(onEnterInOffsetInputAlert, "OFFSET", to_string((int)IF.GetOffsetInIth(IF.SelectedFileID-1)), true));
}

void onRem() {
	if (IF.SelectedFileID <= IF.FILES.size() && IF.SelectedFileID>0) {
		list<string>::iterator Y;
		float ButtOffset;
		short Flag;
		string BT,BN;
		for (int i = 1; i <= IF.FILES.size(); i++) {
			BN = "B" + to_string(i);
			GF.RemoveNamedButton(BN);
		}
		IF.RemoveIthFile(IF.SelectedFileID-1);
		IF.SelectedFileID = 0;
		if (!IF.FILES.empty()) {
			Y = IF.FILES.begin();
			for (int i = 0; i < IF.FILES.size(); i++, Y++) {
				if (Y == IF.FILES.end())break;
				ButtOffset = (INLISTHEIGTH + INLISTBMARGIN * 2 + INLISTADDITIONALDELTA)*i;
				BT = *Y;
				Flag = 0;
				for (int q = BT.size() - 1; q >= 0; q--) {
					if (BT[q] == '\\')Flag++;
					if (Flag >= 2 && BT[q] == '\\') {
						Flag = q;
						break;
					}
				}
				BT.erase(0, Flag);
				if (BT.size() > 45)BT.erase(45, BT.size() - 45);
				if (INLISTTOPSTRINGSTART - ButtOffset >= -4.15) {
					GF.AddButton(TrackListClick, "B" + to_string(i+1), BT, -INLISTTOPSTRINGSTART, INLISTTOPSTRINGSTART - ButtOffset, 0, 0,
						INLISTBMARGIN, INLISTTMARGIN, INLISTTMARGIN, INLISTHEIGTH,
						2, 0xCFFCFF1F, 0xCFFFCFCF);
					GF.TopButtonHoverSettings(0xCFFFCFCF, 0x000F00CF, 2);
				}
			}
		}

	}
	else cout << "overflow\n";
}

void onEnterInPPQ() {
	IF.PPQ = stoi(GF.GetStringFromInput());
	GF.TerminateInput();
}
void onPPQ() {
	GF.TriggerAnInput(__INPUTALERT(onEnterInPPQ,"PPQ",to_string(IF.PPQ),1));
}

void onEnterTempo() {
	IF.NewTempo = stoi(GF.GetStringFromInput());
	GF.TerminateInput();
}
void onSetTempo() {
	GF.TriggerAnInput(__INPUTALERT(onEnterTempo, "TEMPO", to_string(IF.NewTempo), 1));
}

void onPrgOrd() {
	IF.ProgORD = !IF.ProgORD;
	if (IF.ProgORD) {
		GF.ChangeButtonLabel("pord", "PIANO ONLY");
	}
	else {
		GF.ChangeButtonLabel("pord", "ANY");
	}
}

void onRemnantsSCH() {
	IF.RemRem = !IF.RemRem;
	if (IF.RemRem) {
		GF.ChangeButtonLabel("rems", "REMS: ON");
	}
	else {
		GF.ChangeButtonLabel("rems", "REMS: OFF");
	}
}

void onSaveTo() {
	string Filepath = OFD("Save to dialog");
	if (Filepath != "") {
		int pos = Filepath.rfind(".mid");
		if (pos != Filepath.size() - 4)Filepath += ".mid";
		IF.SaveTo = Filepath;
	}
	cout << IF.SaveTo <<endl;
}


void onStart() {
	string params = "";
	if (IF.Log)params += "\\c ";
	if (IF.ProgORD)params += "\\p ";
	if (IF.RemRem)params += "\\r";
	if (IF.NewTempo)params += "\\t" + to_string(IF.NewTempo)+" ";
	if (IF.PPQ)params += "\\f" + to_string(IF.PPQ)+" ";
	if (IF.SaveTo.size())params += "\"S2:" + IF.SaveTo + "\" ";
	if (IF.FILES.size()) {
		list<string>::iterator Y = IF.FILES.begin();
		list<unsigned int>::iterator H = IF.OFFSETS.begin();
		for (int i = 0; i < IF.FILES.size(); i++,Y++,H++) {
			params += "\"" + ((*H) ? (to_string(*H) + "#") : "") + *Y + "\" ";
		}
		int abc = static_cast<int>(reinterpret_cast<uintptr_t>(ShellExecuteA(NULL, "open", (dirto + "\\SAFC.exe").c_str(), params.c_str(), wayto.c_str(), SW_SHOWNORMAL)));
		
		if (abc == 0)								CreateAlert("SAFC: 0");
		else if (abc == ERROR_FILE_NOT_FOUND)		CreateAlert("SAFC: ERROR_FILE_NOT_FOUND");
		else if (abc == ERROR_PATH_NOT_FOUND)		CreateAlert("SAFC: ERROR_PATH_NOT_FOUND");
		else if (abc == ERROR_BAD_FORMAT)			CreateAlert("SAFC: ERROR_BAD_FORMAT");
		else if (abc == SE_ERR_ACCESSDENIED)		CreateAlert("SAFC: SE_ERR_ACCESSDENIED");
		else if (abc == SE_ERR_ASSOCINCOMPLETE)		CreateAlert("SAFC: SE_ERR_ASSOCINCOMPLETE");
		else if (abc == SE_ERR_DDEBUSY)				CreateAlert("SAFC: SE_ERR_DDEBUSY");
		else if (abc == SE_ERR_DDEFAIL)				CreateAlert("SAFC: SE_ERR_DDEFAIL");
		else if (abc == SE_ERR_DDETIMEOUT)			CreateAlert("SAFC: SE_ERR_DDETIMEOUT");
		else if (abc == SE_ERR_DLLNOTFOUND)			CreateAlert("SAFC: SE_ERR_DLLNOTFOUND");
		else if (abc == SE_ERR_FNF)					CreateAlert("SAFC: SE_ERR_FNF");
		else if (abc == SE_ERR_NOASSOC)				CreateAlert("SAFC: SE_ERR_NOASSOC");
		else if (abc == SE_ERR_OOM)					CreateAlert("SAFC: SE_ERR_OOM");
		else if (abc == SE_ERR_PNF)					CreateAlert("SAFC: SE_ERR_PNF");
		else if (abc == SE_ERR_SHARE)				CreateAlert("SAFC: SE_ERR_SHARE");
	}
}

void INIT() {
	IF.Init();
	GF.InitNewTextFrame(-RES + 0.5, RES - 0.5, RES * 2 - 1, RES * 2 - 1, 0x000F033F);
	GF.AddButton(onAdd, "add", "Add", 2.75, 4.25, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x00FF7F3F, 0x00FF7FFF);
	GF.TopButtonHoverSettings(0x3FFF3F48, 0x00FF00FF, 2);
	GF.AddButton(onRem, "rem", "REMOVE", 2.75, 3.75, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0xFF003F3F, 0xFF7F10FF);
	GF.TopButtonHoverSettings(0xFF003F48, 0xFF7F48FF, 2);
	GF.AddButton(onOffset, "cho", "SET OFFSET", 2.75, 3.25, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x7FFF003F, 0x00CF10FF);
	GF.TopButtonHoverSettings(0x7FFF0048, 0x00CF3CFF, 2);
	GF.AddButton(onPPQ, "ppq", "SET PPQ", 2.75, 2.75, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x3FFF3F3F, 0xAFAF30CF);
	GF.TopButtonHoverSettings(0x3FFF3F48, 0xBFBF3CCF, 2);
	GF.AddButton(onSetTempo, "tempo", "SET TEMPO", 2.75, 2.25, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x7FFF003F, 0x7FFF00CF);
	GF.TopButtonHoverSettings(0x7FFF004F, 0xAFFF7F8F, 2);
	GF.AddButton(onPrgOrd, "pord", ((IF.ProgORD)? "PIANO ONLY" : "ANY"), 2.75, 1.75, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x7FFF7F3F, 0xFFFFFF7F);
	GF.TopButtonHoverSettings(0x7FFF7F4F, 0xFFFFFF8F, 2);
	GF.AddButton(onRemnantsSCH, "rems", "REMS: OFF", 2.75, 1.25, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x00FF7F3F, 0x00FF7FFF);
	GF.TopButtonHoverSettings(0x3FFF7F4F, 0x3FFFFF8F, 2);


	GF.AddButton(onStart, "start", "START", 2.75, -3.6, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x007FFF3F, 0x3FAFFF7F);
	GF.TopButtonHoverSettings(0x3F7FFF4F, 0x7FCFFF9F, 2);
	GF.AddButton(onSaveTo, "save", "SAVE TO", 2.75, -4.1, 0, 1.5, 0.125, 0.05, 0.1, 0.15, 2, 0x00FF7F3F, 0x3FFFCF7F);
	GF.TopButtonHoverSettings(0x3FFF7F4F, 0x7FCFFF9F, 2);

}

/////////////////////////////////////////////
/////////////////////////////////////////////
/////////////////////////////////////////////
string FloatToDottedString(float F) {
	string temp = to_string(F), out = "";
	for (int i = 0; i < temp.length(); i++) {
		if (temp[i] <= 127)out += ASCII[temp[i]];
		else out += ASCII[1];
		if (i != temp.length() - 1)out += "+";
	}
	return out;
}
string StringToDottedString(string Inp) {
	string out = "";
	for (int i = 0; i < Inp.length(); i++) {
		if (Inp[i] <= 127)out += ASCII[Inp[i]];
		else out += ASCII[1];
		if (i != Inp.length() - 1)out += "+";
	}
	return out;
}
void onTimer(int v);
void mDisplay() {
	glClear(GL_COLOR_BUFFER_BIT);
	if (FIRSTBOOT) {
		INIT();
		FIRSTBOOT = 0;
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;
		onTimer(0);
	}
	GF.Draw();
	glutSwapBuffers();
}
void mInit() {
	//glClearColor(0, DRANDFLOAT(0.05) + 0.05, DRANDFLOAT(0.2) + 0.2, 1);
	float Q = RANDFLOAT(0.05);
	glClearColor(0.1 + Q, 0.1 + Q, 0.1 + Q, 1);
	glMatrixMode(GL_PROJECTION);
	glPointSize(1);
	glLoadIdentity();
	gluOrtho2D(0 - RES, RES, 0 - RES, RES);
}

void onTimer(int v) {
	glutTimerFunc(33, onTimer, 0);
	if (ANIMATION_IS_ACTIVE) {
		mDisplay();
	}
}

void mKey(BYTE k, int x, int y) {
	std::cout << (int)k << '\n';
	if (k == 255) { ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE; }
	GF.KeyboardActiivity(k);
}

void mMotion(int x, int y) {
	//cout << x << ' ' << y << endl;
	GF.MouseMove(x, y);
}
void mClick(int button, int state, int x, int y) {
	if (state == GLUT_DOWN)GF.MouseClick(x, y);
}


int main(int argc, char ** argv) {
	srand(TIMESEED());
	if (1)ShowWindow(GetConsoleWindow(), SW_HIDE);
	else ShowWindow(GetConsoleWindow(), SW_SHOW);
	dirto = wayto = argv[0];
	while (dirto.back() != '\\')dirto.pop_back();
	dirto.pop_back();
	cout << dirto << endl;
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowSize(720, 720);
	glutInitWindowPosition(50, 50);
	glutCreateWindow("Green sea");

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);//_MINUS_SRC_ALPHA
	glEnable(GL_BLEND);//GL_POLYGON_SMOOTH
	//glEnable(GL_POLYGON_SMOOTH);
	glEnable(GL_LINE_SMOOTH);//GL_POLYGON_SMOOTH
	glEnable(GL_POINT_SMOOTH); 
	glShadeModel(GL_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_LINEAR);
	glutMouseFunc(mClick);
	glutPassiveMotionFunc(mMotion);
	//glutMotionFunc(mMotion);
	glutKeyboardFunc(mKey);
	glutDisplayFunc(mDisplay);
	mInit();
	glutMainLoop();
	return 0;
}