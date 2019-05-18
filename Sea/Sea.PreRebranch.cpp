#include "stdafx.h"
#include <ctime>
#include <random>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <cmath>
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
#define DRANDFLOAT(range)  ( (0 - range) + ( (float)___dis(___eng) )/( 16384. / range ) )
#define SLOWDPROG(a,b,fract) ((a + (fract - 1)*b)/fract)

string StringToDottedString(string Inp);
string FloatToDottedString(float F);
void NOP() {}
void NIL() {}

const string ASCII[] = {
	" ",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"713973197",//NULLSYMB
	"5",//space
	"85 12",//!
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
	"521",//,
	"46",//-
	"12",//.
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
	"78 12",//:
	"78 521",//;
	"943",//<
	"46 13",//=
	"761",//>
	"7965 12",//?
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
	"97139 53",//Q
	"17964 53",//R
	"974631",//S
	"79 82",//T
	"712693",//U
	"729",//V
	"71539",//W
	"73 91",//X
	"75952",//Y
	"7913",//Z
	"9823",//[
	"72",// \//
	"7821",//]
	"486",//^
	"13",//_
	"75",//`
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
	"97139 53",//Q
	"17964 53",//R
	"974631",//S
	"79 82",//T
	"712693",//U
	"729",//V
	"71539",//W
	"73 91",//X
	"75952",//Y
	"7913",//Z
	"85452",//{
	"82",//|
	"85652",//}
	"156",//~
	"71937 97 31",// 
};

struct __FLOATINGSEMITRASNPARENTTHINGS {
	float _cx, _cy, _cdx, _cdy, _dang, _cxi, _cyi, _cai;
	float _rad, _ang;
	int _angleamount;
	__FLOATINGSEMITRASNPARENTTHINGS(float x = 0, float y = 0, float size = 1, float angle = 0, float sides = 3) {
		_cx = x; _cy = y; _rad = size; _ang = angle; _angleamount = sides;
		_cdx = DRANDFLOAT(0.1);
		_cdy = DRANDFLOAT(0.1);
		_dang = DRANDFLOAT(15);
		_cxi = _cyi = _cai = 0;
	}
	void Process(float dt = 0.1) {
		_ang += (_dang + (_cai = SLOWDPROG(DRANDFLOAT(15) * 10, _cai, 64)))*dt;//smooth interuption
		_cx += (_cdx + (_cxi = SLOWDPROG(DRANDFLOAT(0.1) * 10, _cxi, 64)))*dt;
		_cy += (_cdy + (_cyi = SLOWDPROG(DRANDFLOAT(0.1) * 10, _cyi, 64)))*dt;
		if (sqrt(_cx*_cx + _cy*_cy) > 1.5 * RES) {
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
		_doffset = DRANDFLOAT(0.1);
		_dhgt = 0;
		_hgti = _offi = 0;
	}
	void Process(float dt = 0.1) {
		_hgt += (_dhgt + (_hgti = SLOWDPROG(DRANDFLOAT(0.05), _hgti, 32)))*dt;//becvuas ima lazye fuck
		_offset += (_doffset + (_offi = SLOWDPROG(DRANDFLOAT(0.1), _offi, 16)))*dt;
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
	__FLOATDCONTAINER _relatedpoints[9];
	__DOTTEDSYMBOLS(string RenderWay = "", float x = 0, float y = 0, float xUnitSize = 0.15, float yUnitSize = 0.25) {
		_renderway = RenderWay; _x = x; _y = y; _xus = xUnitSize; _yus = yUnitSize;
		for (int i = -1; i <= 1; i++) {
			for (int j = -1; j <= 1; j++) {
				_relatedpoints[(i + 1) * 3 + (j + 1)].Set(_xus*(j), _yus*(i));
			}
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
	}
};

struct __BUTTON {
	__DOTTEDSYMBOLS *text;
	bool _HD;//hovered
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
		if (!_HD)glColor4f((float)((_COLORBCKG & 0xFF000000) >> 24) / 256
			, (float)((_COLORBCKG & 0xFF0000) >> 16) / 256
			, (float)((_COLORBCKG & 0xFF00) >> 8) / 256
			, (float)(_COLORBCKG & 0xFF) / 256
		);
		else glColor4f((float)((_HOVEREDCOLORBCKG & 0xFF000000) >> 24) / 256
			, (float)((_HOVEREDCOLORBCKG & 0xFF0000) >> 16) / 256
			, (float)((_HOVEREDCOLORBCKG & 0xFF00) >> 8) / 256
			, (float)(_HOVEREDCOLORBCKG & 0xFF) / 256
		);
		glBegin(GL_QUADS);
		glVertex2f(_x - _bmargin, _y + _bmargin);
		glVertex2f(_x + _w + _bmargin, _y + _bmargin);
		glVertex2f(_x + _w + _bmargin, _y - _h - _bmargin);
		glVertex2f(_x - _bmargin, _y - _h - _bmargin);
		//printf("%f %f %f %f\n", _x, _x + _w, _y, _y - _h);
		//glVertex2f(_x     , _y     );
		glEnd();
		if (!_HD) glLineWidth(_CLINEWIDTH);
		else glLineWidth(_HOVEREDCLINEWIDTH);
		if (!_HD)glColor4f((float)((_COLORTXT & 0xFF000000) >> 24) / 256
			, (float)((_COLORTXT & 0xFF0000) >> 16) / 256
			, (float)((_COLORTXT & 0xFF00) >> 8) / 256
			, (float)(_COLORTXT & 0xFF) / 256
		);
		else glColor4f((float)((_HOVEREDCOLORTXT & 0xFF000000) >> 24) / 256
			, (float)((_HOVEREDCOLORTXT & 0xFF0000) >> 16) / 256
			, (float)((_HOVEREDCOLORTXT & 0xFF00) >> 8) / 256
			, (float)(_HOVEREDCOLORTXT & 0xFF) / 256
		);
		for (int i = 0; i < _textlen; i++) {
			text[i].Draw();
		}
	}
};

struct __BUTTONHANDLER {
	vector<__BUTTON> _allbutts;
	void AddButton(__BUTTON B) {
		_allbutts.push_back(B);
	}
	void MouseMove(int ix, int iy) {
		bool HOVERED = 0;
		float x, y, wh = glutGet(GLUT_WINDOW_HEIGHT), ww = glutGet(GLUT_WINDOW_WIDTH);
		x = ((float)(ix - ww*0.5)) / (0.5*(ww / RES));
		y = ((float)(0 - iy + wh*0.5)) / (0.5*(wh / RES));
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
		//cout << x << " " << y<<endl;
	}
	void MouseClick(int ix, int iy) {
		float x, y, wh = glutGet(GLUT_WINDOW_HEIGHT), ww = glutGet(GLUT_WINDOW_WIDTH);
		x = ((float)(ix - ww*0.5)) / (0.5*(ww / RES));
		y = ((float)(0 - iy + wh*0.5)) / (0.5*(wh / RES));
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
	__TEXTLINE() { _TXTCOL = 0; _x = 0; _y = 0; _text.clear(); }
	__TEXTLINE(string DottedString, float Xpos, float Ypos,
		float SymbH, float SymbW, float SymbMargin = 0.05, unsigned int TextRGBA = 0xFFFFFFFF) {
		vector<string> FSS;
		string T;
		float OffsetIncrementor = 0;
		FSS.clear();
		T.clear();
		_x = Xpos; _y = Ypos;
		_TXTCOL = TextRGBA;
		for (int i = 0; i < DottedString.length(); i++) {
			if ((DottedString[i] >= '1' && DottedString[i] <= '9') || (DottedString[i] == ' '))T.push_back(DottedString[i]);
			else {
				FSS.push_back(T);
				T.clear();
			}
		}
		for (int i = 0; i < FSS.size(); i++) {
			_text.push_back(__DOTTEDSYMBOLS(FSS[i], _x + OffsetIncrementor, _y - 0.5*SymbH, SymbW *0.5, SymbH *0.5));
			OffsetIncrementor += (SymbW + SymbMargin);
		}
	}
	void Draw() {
		glColor4f((float)((_TXTCOL & 0xFF000000) >> 24) / 256
			, (float)((_TXTCOL & 0xFF0000) >> 16) / 256
			, (float)((_TXTCOL & 0xFF00) >> 8) / 256
			, (float)(_TXTCOL & 0xFF) / 256
		);
		for (int i = 0; i < _text.size(); i++) {
			_text[i].Draw();
		}
	}
};
struct __TEXTFRAME {
	float _h, _w;
	float _x, _y;
	unsigned int _BCKGCOLOR;
	vector<__TEXTLINE> _lines;
	__TEXTFRAME(float Xpos = 0, float Ypos = 0, float Heigth = 0, float Width = 0, unsigned int BackgroundRGBA = 0x000000AF) {
		_h = Heigth; _w = Width; _x = Xpos; _y = Ypos; _BCKGCOLOR = BackgroundRGBA;
	}
	void AddNewLine(string Text, float Left, float Top, float FontHeight, unsigned int Color = 0xFFFFFFFF) {
		Text = StringToDottedString(Text);
		_lines.push_back(__TEXTLINE(Text, _x + Left, _y - Top, FontHeight, FontHeight*0.66, FontHeight*0.1, Color));
	}
	void Draw() {
		glColor4f((float)((_BCKGCOLOR & 0xFF000000) >> 24) / 256
			, (float)((_BCKGCOLOR & 0xFF0000) >> 16) / 256
			, (float)((_BCKGCOLOR & 0xFF00) >> 8) / 256
			, (float)(_BCKGCOLOR & 0xFF) / 256
		);
		glBegin(GL_QUADS);
		glVertex2f(_x, _y);
		glVertex2f(_x + _w, _y);
		glVertex2f(_x + _w, _y - _h);
		glVertex2f(_x, _y - _h);
		glEnd();
		for (int i = 0; i < _lines.size(); i++) {
			_lines[i].Draw();
		}
	}
};
struct __INPUTALERT {
	__TEXTLINE _Header;
	__TEXTLINE _Commentary;
	__TEXTLINE _INPUTAREA;
	__TEXTLINE _AdditionalCommentary;
	string INPUTVAL;
	bool NumsOnly;
	void(*_OnSubmit)();
	__INPUTALERT(void(*OnSubmit)() = NIL, string Header = "", string DefaultInput = "", bool NumbersOnly = 0) {
		INPUTVAL = DefaultInput;
		NumsOnly = NumbersOnly;
		_OnSubmit = OnSubmit;
		_INPUTAREA = __TEXTLINE(StringToDottedString(INPUTVAL), (0 - (0.4)*INPUTVAL.length()), 0, 1, 0.75);
		DefaultInput = "Press enter to submit value or esc to cancel";
		_AdditionalCommentary = __TEXTLINE(StringToDottedString(DefaultInput), (0 - (0.4)*DefaultInput.length()), 1.25, 0.25, 0.15);
		_Header = __TEXTLINE(StringToDottedString(Header), (0 - (0.4)*Header.length()), 1.5, 1, 0.75);
		DefaultInput = (NumbersOnly ? "Numbers only" : "Plain text");
		_Commentary = __TEXTLINE(StringToDottedString(DefaultInput), (0 - (0.4)*DefaultInput.length()), -1, 0.50, 0.25);
	}
	void AddNewSymbol(char C) {
		INPUTVAL.push_back(C);
		_INPUTAREA = __TEXTLINE(StringToDottedString(INPUTVAL), (0 - (0.4)*INPUTVAL.length()), 0, 1, 0.75);
	}
	void RemoveOneSymbol() {
		INPUTVAL.pop_back();
		_INPUTAREA = __TEXTLINE(StringToDottedString(INPUTVAL), (0 - (0.4)*INPUTVAL.length()), 0, 1, 0.75);
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
};
struct __MAINFRAME {
	float _w, _h;
	bool InputIsActive;
	__BUTTONHANDLER _butts;
	__INPUTALERT _input;
	__TEXTFRAME _text;
	unsigned int _BACKGROUND;
	__MAINFRAME(float width = RES * 2, float heigth = RES * 2, unsigned int BackgroundRGBA = 0x0000003F) { _w = width; _h = heigth; InputIsActive = 0; _BACKGROUND = BackgroundRGBA; }
	void AddButton(void(*OnClick)(), string FormSymbStr = "", float xPos = 0, float yPos = 0, float heigth = 0, float width = 0, float BoxMargin = 0.05,
		float TMargin = 0.050, float SymbW = 0.15, float SymbH = 0.25, float CLINEWIDTH = 1, unsigned int BckgColorRGBA = 0x3F3F3F7F,
		unsigned int TxtColorRGBA = 0xFFFFFFEE) {
		_butts.AddButton(__BUTTON(OnClick, FormSymbStr, xPos, yPos, heigth, width, BoxMargin, TMargin, SymbW, SymbH, CLINEWIDTH, BckgColorRGBA, TxtColorRGBA));
	}
	void InitNewTextFrame(float Xpos, float Ypos, float Height, float Width, unsigned int BackgroundRGBA) {
		_text = __TEXTFRAME(Xpos, Ypos, Height, Width, BackgroundRGBA);
	}
	void AddTextToFrame(string Text, float RelLeft, float RelTop, float FontHeight, unsigned int BackgroundRGBA) {
		_text.AddNewLine(Text, RelLeft, RelTop, FontHeight, BackgroundRGBA);
	}
	void MouseClick(int ix, int iy) {
		if (!InputIsActive)_butts.MouseClick(ix, iy);
	}
	void MouseMove(int ix, int iy) {
		if (!InputIsActive)_butts.MouseClick(ix, iy);
	}
	void KeyboardActiivity(char CH) {
		if (InputIsActive) {
			if (CH == 10) {//ENTER
				_input._OnSubmit();
			}
			else if (CH == 27) {//ESC
				InputIsActive = !InputIsActive;
			}
			else if (CH == 8) {//BACKSPACE
				_input.RemoveOneSymbol();
			}
			else {
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
		_input.Draw();
	}
};
struct __GLOBALFRAME {
	vector<__FLOATINGSEMITRASNPARENTTHINGS> _particles;
	vector<__WAVE> _waves;
	__MAINFRAME _stuffs;
	map<string, string> USERDATA;
	__GLOBALFRAME(int ParticleCount, int WaveCount) {
		for (int i = 0; i < ParticleCount; i++) {
			_particles.push_back(__FLOATINGSEMITRASNPARENTTHINGS(DRANDFLOAT(RES), DRANDFLOAT(RES),
				DRANDFLOAT(1)*0.25, DRANDFLOAT(360), rand() % 7 + 3));
		}
		for (int i = 0; i < WaveCount; i++) {
			_waves.push_back(__WAVE(DRANDFLOAT(1)*0.5, DRANDFLOAT(1)*0.5, DRANDFLOAT(PI), RANDFLOAT(RES - 1), DRANDFLOAT(25) + 5));
		}
		_stuffs = __MAINFRAME();
	}
	void AddButton(void(*OnClick)(), string FormSymbStr = "", float xPos = 0, float yPos = 0, float heigth = 0, float width = 0, float BoxMargin = 0.05,
		float TMargin = 0.050, float SymbW = 0.15, float SymbH = 0.25, float CLINEWIDTH = 1, unsigned int BckgColorRGBA = 0x3F3F3F7F,
		unsigned int TxtColorRGBA = 0xFFFFFFEE) {
		_stuffs.AddButton(OnClick, FormSymbStr, xPos, yPos, heigth, width, BoxMargin, TMargin, SymbW, SymbH, CLINEWIDTH, BckgColorRGBA, TxtColorRGBA);
	}
	void InitNewTextFrame(float Xpos, float Ypos, float Height, float Width, unsigned int BackgroundRGBA) {
		_stuffs.InitNewTextFrame(Xpos, Ypos, Height, Width, BackgroundRGBA);
	}
	void AddTextToFrame(string Text, float RelLeft, float RelTop, float FontHeight, unsigned int BackgroundRGBA) {
		_stuffs.AddTextToFrame(Text, RelLeft, RelTop, FontHeight, BackgroundRGBA);
	}
	void MouseClick(int ix, int iy) {_stuffs.MouseClick(ix, iy);}
	void MouseMove(int ix, int iy) {_stuffs.MouseMove(ix, iy);}
	void KeyboardActiivity(char CH) { _stuffs.KeyboardActiivity(CH); }
	void TriggerAnInput(__INPUTALERT InputAlert) { _stuffs.TriggerAnInput(InputAlert); }
	void Draw() {
		for (int i = 0; i < _particles.size(); i++) {
			_particles[i].Process();
			_particles[i].Draw();
		}
		for (int i = 0; i < _waves.size(); i++) {
			_waves[i].Process();
			_waves[i].Draw();
		}
		_stuffs.Draw();
	}
};

__BUTTONHANDLER B;
__FLOATDCONTAINER B1 = __FLOATDCONTAINER(0, 0);
__FLOATINGSEMITRASNPARENTTHINGS *fst;
__WAVE *waves;

void Clck() {
	glClearColor(DRANDFLOAT(0.2) + 0.2, DRANDFLOAT(0.05) + 0.1, DRANDFLOAT(0.2) + 0.2, 1);
}

void INIT() {
	B.AddButton(__BUTTON(Clck, StringToDottedString("Randomise"), -1, 1, 0, 2, 0.25));
	B[0].HoverSettings(0x0000003F, 0xFFFFFFCF, 3);
	B.AddButton(__BUTTON(NOP, "", -1.5, -1, 0, 0, 0.2));
	B.AddButton(__BUTTON(Clck, StringToDottedString("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"), -4.95, 0.125, 0, 2, 0, 0.075, 0.2, 0.3, 1, 0, 0xFFFFFFFF));
	B[2].HoverSettings(0, 0xFFFFFFFF, 1);


	//RD = __DOTTEDSYMBOLS("", (RDit._x = SLOWDPROG(DRANDFLOAT(0.1), RDit._x, 64)), (RDit._y = SLOWDPROG(DRANDFLOAT(0.1), RDit._y, 64)) - 1, 0.2, 0.30);
	fst = new __FLOATINGSEMITRASNPARENTTHINGS[36];
	for (int i = 0; i < 36; i++) {
		fst[i] = __FLOATINGSEMITRASNPARENTTHINGS(DRANDFLOAT(RES), DRANDFLOAT(RES),
			DRANDFLOAT(1)*0.25, DRANDFLOAT(360), rand() % 7 + 3);
	}
	waves = new __WAVE[5];
	for (int i = 0; i < 5; i++) {
		waves[i] = __WAVE(DRANDFLOAT(1)*0.5, DRANDFLOAT(1)*0.5, DRANDFLOAT(PI), RANDFLOAT(RES - 1), DRANDFLOAT(25) + 5);
	}
}
string FloatToDottedString(float F) {
	string temp = to_string(F), out = "";
	for (int i = 0; i < temp.length(); i++) {
		if (temp[i] <= 127)out += ASCII[temp[i]];
		else out += ASCII[0];
		if (i != temp.length() - 1)out += "+";
	}
	return out;
}
string StringToDottedString(string Inp) {
	string out = "";
	for (int i = 0; i < Inp.length(); i++) {
		if (Inp[i] <= 127)out += ASCII[Inp[i]];
		else out += ASCII[0];
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


	bool hoveredstatesave = B[1]._HD;
	B1.Set(SLOWDPROG(DRANDFLOAT(0.1), B1._x, 64), SLOWDPROG(DRANDFLOAT(0.1), B1._y, 64));
	B[1] = __BUTTON(NOP, "248+" +
		FloatToDottedString(B[1]._x + 0.5*B[1]._w + B1._x) + "+0+" +
		FloatToDottedString(B[1]._y - 0.5*B[1]._h + B1._y) + "+268",
		B[1]._x + B1._x, B[1]._y + B1._y, B[1]._h, B[1]._w, B[1]._bmargin);
	B[1]._CLINEWIDTH = 2;
	B[1]._HOVEREDCLINEWIDTH = 3;
	B[1]._HOVEREDCOLORTXT = 0xFFFFFFFF;
	B[1]._HOVEREDCOLORBCKG = 0x00007F3F;
	B[1]._HD = hoveredstatesave;
	/*
	RD = __DOTTEDSYMBOLS("",
	RD._x + (RDit._x = SLOWDPROG(DRANDFLOAT(0.1), RDit._x, 64)),
	RD._y + (RDit._y = SLOWDPROG(DRANDFLOAT(0.1), RDit._y, 64)), 0.2, 0.30);
	RD._renderway = SYMBOLS[((int)(RD._x + RD._y))%10];*/
	/* RD._renderway.clear();
	for (int i = rand() & 0x3 + 2; i >= 0;i--)RD._renderway.push_back((rand() & 0x7) ? '1' + (rand() % 8) : ' ');
	*/

	glLineWidth(1);
	for (int i = 0; i < 36; i++) {
		if (i < 5) {
			glColor4f(0.25, 0.85, 1, 0.66);
			waves[i].Process();
			waves[i].Draw();
		}
		glColor4f(1, 1, 1, 0.46);
		fst[i].Process();
		fst[i].Draw();
	}
	B.DrawAll();
	glutSwapBuffers();
	//cout << (abc = RANDFLOAT(0.5)) << " " << (aabc += abc*0.1) << endl;

	//glFlush();
}
void mInit() {
	glClearColor(DRANDFLOAT(0.2) + 0.2, DRANDFLOAT(0.05) + 0.1, DRANDFLOAT(0.2) + 0.2, 1);
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
	if (k == 255) {
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;

	}
	else if (k == 27)exit(1);
}

void mMotion(int x, int y) {
	//cout << x << " " << y << endl;
	B.MouseMove(x, y);
}
void mClick(int button, int state, int x, int y) {
	if (state == GLUT_DOWN)B.MouseClick(x, y);
}

int main(int argc, char ** argv) {
	if (0)ShowWindow(GetConsoleWindow(), SW_HIDE);
	srand(clock());
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowSize(720, 720);
	glutCreateWindow("Sea. OpenGL SAFC Launcher");

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);//GL_POLYGON_SMOOTH
	glEnable(GL_LINE_SMOOTH);//GL_POLYGON_SMOOTH
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