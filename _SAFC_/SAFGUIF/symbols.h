#pragma once
#ifndef SAFGUIF_SYMBOLS 
#define SAFGUIF_SYMBOLS

#include "header_utils.h"
#include "charmap.h"

struct Coords {
	float x, y;
	void Set(float newx, float newy) {
		x = newx; y = newy;
	}
};

struct DottedSymbol {
	float Xpos, Ypos;
	BYTE R, G, B, A, LineWidth;
	Coords Points[9];
	std::string RenderWay;
	std::vector<char> PointPlacement;
	DottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2, BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255) {
		if (!RenderWay.size())RenderWay = " ";
		this->RenderWay = RenderWay;
		this->Xpos = Xpos;
		this->LineWidth = LineWidth;
		this->Ypos = Ypos;
		this->R = Red; this->G = Green; this->B = Blue; this->A = Alpha;
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				Points[x + 1 + 3 * (y + 1)].Set(XUnitSize * x, YUnitSize * y);
			}
		}
		UpdatePointPlacementPositions();
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2, BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255) :
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha) {}

	DottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth, DWORD* RGBAColor) :
		DottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF) {
		delete RGBAColor;
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth, DWORD* RGBAColor) :
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF) {
		delete RGBAColor;
	}
	inline static BIT IsRenderwaySymb(CHAR C) {
		return (C <= '9' && C >= '0' || C == ' ');
	}
	inline static BIT IsNumber(CHAR C) {
		return (C <= '9' && C >= '0');
	}

	void UpdatePointPlacementPositions() {
		PointPlacement.clear();
		if (RenderWay.size() > 1) {
			if (IsNumber(RenderWay[0]) && !IsNumber(RenderWay[1]))
				PointPlacement.push_back(RenderWay[0]);
			if (IsNumber(RenderWay.back()) && !IsNumber(RenderWay[RenderWay.size() - 2]))PointPlacement.push_back(RenderWay.back());
			for (int i = 1; i < RenderWay.size() - 1; ++i) {
				if (IsNumber(RenderWay[i]) && !IsNumber(RenderWay[i - 1]) && !IsNumber(RenderWay[i + 1]))
					PointPlacement.push_back(RenderWay[i]);
			}
		}
		else if (RenderWay.size() && IsNumber(RenderWay[0])) {
			PointPlacement.push_back(RenderWay[0]);
		}
	}
	void virtual Draw() {
		if (RenderWay == " ")return;
		float VerticalFlag = 0.f;
		CHAR BACK = 0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~') {
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK) {
			case '#':
				VerticalFlag = Points->y - Points[3].y;
				break;
			case '~':
				VerticalFlag = (Points->y - Points[3].y) / 2;
				break;
			}
		}
		if (is_fonted)
			VerticalFlag = 0;
		glColor4ub(R, G, B, A);
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		BYTE IO;
		for (int i = 0; i < RenderWay.length(); i++) {
			if (RenderWay[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i)
			glVertex2f(Xpos + Points[PointPlacement[i] - '1'].x, Ypos + Points[PointPlacement[i] - '1'].y + VerticalFlag);
		glEnd();
		if (BACK)
			RenderWay.back() = BACK;
	}
	void SafePositionChange(float NewXPos, float NewYPos) {
		SafeCharMove(NewXPos - Xpos, NewYPos - Ypos);
	}
	void SafeCharMove(float dx, float dy) {
		Xpos += dx; Ypos += dy;
	}
	inline float _XUnitSize() const {
		return Points[1].x - Points[0].x;
	}
	inline float _YUnitSize() const {
		return Points[3].y - Points[0].y;
	}
	void virtual RefillGradient(DWORD* RGBAColor, DWORD* gRGBAColor, BYTE BaseColorPoint, BYTE GradColorPoint) {
		return;
	}
	void virtual RefillGradient(BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) {
		return;
	}
};

FLOAT lFONT_HEIGHT_TO_WIDTH = 2.5;
namespace lFontSymbolsInfo {
	bool IsInitialised = false;
	GLuint CurrentFont = 0;
	HFONT SelectedFont;
	INT32 Size = 16;
	struct lFontSymbInfosListDestructor {
		bool abc;
		~lFontSymbInfosListDestructor() {
			glDeleteLists(CurrentFont, 256);
		}
	};
	lFontSymbInfosListDestructor __wFSILD = { 0 };
	void InitialiseFont(std::string FontName) {
		if (!IsInitialised) {
			CurrentFont = glGenLists(256);
			IsInitialised = true;
		}
		wglUseFontBitmaps(hDc, 0, 255, CurrentFont);
		SelectedFont = CreateFontA(
			Size * (BEG_RANGE / RANGE),
			(Size > 0) ? Size * (BEG_RANGE / RANGE) / lFONT_HEIGHT_TO_WIDTH : 0,
			0, 0,
			FW_NORMAL,
			FALSE,
			FALSE,
			FALSE,
			DEFAULT_CHARSET,
			OUT_TT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY,
			FF_DONTCARE | DEFAULT_PITCH,
			FontName.c_str()
			);
		if (SelectedFont)
			SelectObject(hDc, SelectedFont);
	}
	inline void CallListOnChar(char C) {
		if (IsInitialised) {
			const char PsChStr[2] = { C,0 };
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
			glCallLists(1, GL_UNSIGNED_BYTE, (const char*)(PsChStr));
			glPopAttrib();
		}
	}
	inline void CallListOnString(const std::string& S) {
		if (IsInitialised) {
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
			glCallLists(1, GL_UNSIGNED_BYTE, S.c_str());
			glPopAttrib();
		}
	}
	const _MAT2 MT = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };;
}
struct lFontSymbol : DottedSymbol {
	char Symb;
	GLYPHMETRICS GM = { 0 };
	lFontSymbol(char Symb, float CXpos, float CYpos, float XUnitSize, float YUnitSize, DWORD RGBA) :
		DottedSymbol(" ", CXpos, CYpos, XUnitSize, YUnitSize, 1, RGBA >> 24, (RGBA >> 16) & 0xFF, (RGBA >> 8) & 0xFF, RGBA & 0xFF) {
		this->Symb = Symb;
	}
	void Draw() override {
		float PixelSize = (RANGE * 2) / WINDXSIZE;
		float rotX = Xpos, rotY = Ypos;
		//rotate(rotX, rotY);
		if (fabsf(rotX) + 2.5 > PixelSize * WindX / 2 || fabsf(rotY) + 2.5 > PixelSize * WindY / 2)
			return;
		SelectObject(hDc, lFontSymbolsInfo::SelectedFont);
		GetGlyphOutline(hDc, Symb, GGO_METRICS, &GM, 0, NULL, &lFontSymbolsInfo::MT);
		glColor4ub(R, G, B, A);
		glRasterPos2f(Xpos - PixelSize * GM.gmBlackBoxX * 0.5, Ypos - _YUnitSize() * 0.5);
		lFontSymbolsInfo::CallListOnChar(Symb);
	}
};

struct BiColoredDottedSymbol : DottedSymbol {
	BYTE gR[9], gG[9], gB[9], gA[9];
	BYTE _PointData;
	BiColoredDottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2,
		BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) :
		DottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, new DWORD(0)) {
		this->_PointData = (((BaseColorPoint & 0xF) << 4) | (GradColorPoint & 0xF));
		if (BaseColorPoint == GradColorPoint) {
			for (int i = 0; i < 9; i++) {
				gR[i] = Red;
				gG[i] = Green;
				gB[i] = Blue;
				gA[i] = Alpha;
			}
		}
		else {
			BaseColorPoint--;
			GradColorPoint--;
			RefillGradient(Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint);
		}
	}
	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2,
		BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint) {}

	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth,
		DWORD* RGBAColor, DWORD* gRGBAColor,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	BiColoredDottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth,
		DWORD* RGBAColor, DWORD* gRGBAColor,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	void RefillGradient(BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) override {
		float xbase = (((float)(BaseColorPoint % 3)) - 1.f), ybase = (((float)(BaseColorPoint / 3)) - 1.f),
			xgrad = (((float)(GradColorPoint % 3)) - 1.f), ygrad = (((float)(GradColorPoint / 3)) - 1.f);
		float ax = xgrad - xbase, ay = ygrad - ybase, t;
		float ial = 1.f / (ax * ax + ay * ay);
		///R
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Red * t + (1.f - t) * gRed);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gR[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///G
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Green * t + (1.f - t) * gGreen);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gG[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///B
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Blue * t + (1.f - t) * gBlue);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gB[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///A
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Alpha * t + (1.f - t) * gAlpha);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gA[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
	}
	void RefillGradient(DWORD* RGBAColor, DWORD* gRGBAColor, BYTE BaseColorPoint, BYTE GradColorPoint) override {
		RefillGradient(*RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint);
		delete RGBAColor;
		delete gRGBAColor;
	}
	void Draw() override {
		if (RenderWay == " ")return;
		float VerticalFlag = 0.;
		CHAR BACK = 0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~') {
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK) {
			case '#':
				VerticalFlag = Points->y - Points[3].y;
				break;
			case '~':
				VerticalFlag = (Points->y - Points[3].y) / 2;
				break;
			}
		}
		if (is_fonted)
			VerticalFlag = 0;
		BYTE IO;
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < RenderWay.length(); i++) {
			if (RenderWay[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			//printf("%x %x %x %x\n", gR[IO], gG[IO], gB[IO], gA[IO]);
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i) {
			IO = PointPlacement[i] - '1';
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		if (BACK)
			RenderWay.back() = BACK;
	}
};

#endif