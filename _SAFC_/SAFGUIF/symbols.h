#pragma once
#ifndef SAFGUIF_SYMBOLS 
#define SAFGUIF_SYMBOLS

#include "header_utils.h"
#include "charmap.h"

struct Coords
{
	float x, y;
	void Set(float newx, float newy)
	{
		x = newx; y = newy;
	}
};

struct DottedSymbol
{
	float Xpos, Ypos;
	std::uint8_t R, G, B, A, LineWidth;
	Coords Points[9];
	std::string RenderWay;
	std::vector<char> PointPlacement;
	DottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth = 2, std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255)
	{
		if (!RenderWay.size())RenderWay = " ";
		this->RenderWay = std::move(RenderWay);
		this->Xpos = Xpos;
		this->LineWidth = LineWidth;
		this->Ypos = Ypos;
		this->R = Red; this->G = Green; this->B = Blue; this->A = Alpha;
		for (int x = -1; x <= 1; x++)
			for (int y = -1; y <= 1; y++)
				Points[x + 1 + 3 * (y + 1)].Set(XUnitSize * x, YUnitSize * y);
		UpdatePointPlacementPositions();
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth = 2, std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255) :
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha) {}

	DottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth, std::uint32_t* RGBAColor) :
		DottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF)
	{
		delete RGBAColor;
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth, std::uint32_t* RGBAColor) :
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF)
	{
		delete RGBAColor;
	}
	virtual ~DottedSymbol() {}
	inline static bool IsRenderwaySymb(CHAR C)
	{
		return (C <= '9' && C >= '0' || C == ' ');
	}
	inline static bool IsNumber(CHAR C)
	{
		return (C <= '9' && C >= '0');
	}

	void UpdatePointPlacementPositions()
	{
		PointPlacement.clear();
		if (RenderWay.size() > 1)
		{
			if (IsNumber(RenderWay[0]) && !IsNumber(RenderWay[1]))
				PointPlacement.push_back(RenderWay[0]);
			if (IsNumber(RenderWay.back()) && !IsNumber(RenderWay[RenderWay.size() - 2]))PointPlacement.push_back(RenderWay.back());
			for (int i = 1; i < RenderWay.size() - 1; ++i)
			{
				if (IsNumber(RenderWay[i]) && !IsNumber(RenderWay[i - 1]) && !IsNumber(RenderWay[i + 1]))
					PointPlacement.push_back(RenderWay[i]);
			}
		}
		else if (RenderWay.size() && IsNumber(RenderWay[0]))
		{
			PointPlacement.push_back(RenderWay[0]);
		}
	}
	void virtual Draw()
	{
		if (RenderWay == " ")return;
		float VerticalShift = 0.f;
		CHAR BACK = 0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~')
		{
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK)
			{
			case '#':
				VerticalShift = Points[0].y - Points[3].y;
				break;
			case '~':
				VerticalShift = (Points[0].y - Points[3].y) / 2;
				break;
			}
		}
		if (is_fonted)
			VerticalShift = 0;
		glColor4ub(R, G, B, A);
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		std::uint8_t IO;
		for (int i = 0; i < RenderWay.length(); i++)
		{
			if (RenderWay[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalShift);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i)
			glVertex2f(Xpos + Points[PointPlacement[i] - '1'].x, Ypos + Points[PointPlacement[i] - '1'].y + VerticalShift);
		glEnd();
		if (BACK)
			RenderWay.back() = BACK;
	}
	void SafePositionChange(float NewXPos, float NewYPos)
	{
		SafeCharMove(NewXPos - Xpos, NewYPos - Ypos);
	}
	void SafeCharMove(float dx, float dy)
	{
		Xpos += dx; Ypos += dy;
	}
	inline float _XUnitSize() const
	{
		return Points[1].x - Points[0].x;
	}
	inline float _YUnitSize() const
	{
		return Points[3].y - Points[0].y;
	}
	void virtual RefillGradient(std::uint32_t* RGBAColor, std::uint32_t* gRGBAColor, std::uint8_t BaseColorPoint, std::uint8_t GradColorPoint)
	{
		return;
	}
	void virtual RefillGradient(std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255,
		std::uint8_t gRed = 255, std::uint8_t gGreen = 255, std::uint8_t gBlue = 255, std::uint8_t gAlpha = 255,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8)
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

		//if (!force)
		//	InitialiseFont(FontName, true);
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
	const _MAT2 MT = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };;
}

struct lFontSymbol : DottedSymbol
{
	char Symb;
	GLYPHMETRICS GM;
	lFontSymbol(char Symb, float CXpos, float CYpos, float XUnitSize, float YUnitSize, std::uint32_t RGBA) :
		DottedSymbol(" ", CXpos, CYpos, XUnitSize, YUnitSize, 1, RGBA >> 24, (RGBA >> 16) & 0xFF, (RGBA >> 8) & 0xFF, RGBA & 0xFF)
	{
		GM.gmBlackBoxX = 0;
		GM.gmBlackBoxY = 0;
		GM.gmCellIncX = 0;
		GM.gmCellIncY = 0;
		GM.gmptGlyphOrigin.x = 0;
		GM.gmptGlyphOrigin.y = 0;

		this->Symb = Symb;
		ReinitGlyphMetrics();
	}
	~lFontSymbol() override = default;
	void ReinitGlyphMetrics()
	{
		//SelectObject(hDc, lFontSymbolsInfo::SelectedFont);

		/*auto gmdata = reinterpret_cast<std::uint8_t*>(&GM);
		std::cout << "Symb: " << Symb << std::endl;
		std::cout << "ReinitGlyphMetrics b4: ";
		for (size_t i = 0; i < sizeof(GM); ++i)
			std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)gmdata[i];
		std::cout << std::endl;*/

		auto result = GetGlyphOutline(hDc, Symb, GGO_GRAY8_BITMAP, &GM, 0, NULL, &lFontSymbolsInfo::MT);

		/*std::cout << "ReinitGlyphMetrics ar: ";
		for (size_t i = 0; i < sizeof(GM); ++i)
			std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)gmdata[i];
		std::cout << std::dec << std::setw(0) << std::setfill(' ') << std::endl;
		std::cout << "res: " << result << std::endl;

		if (result == GDI_ERROR)
			throw "idkman";*/
	}
	void Draw() override
	{
		if (!lFontSymbolsInfo::SelectedFont || !hDc)
			return;

		float PixelSize = (internal_range * 2) / window_base_width;

		if (fabsf(Xpos) + lFONT_HEIGHT_TO_WIDTH > PixelSize * WindX / 2 ||
			fabsf(Ypos) + lFONT_HEIGHT_TO_WIDTH > PixelSize * WindY / 2)
			return;

		glColor4ub(R, G, B, A);
		glRasterPos2f(Xpos, Ypos - _YUnitSize() * 0.5);
		lFontSymbolsInfo::CallListOnChar(Symb);
	}
};

struct BiColoredDottedSymbol : DottedSymbol 
{
	std::uint8_t gR[9], gG[9], gB[9], gA[9];
	std::uint8_t _PointData;
	BiColoredDottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth = 2,
		std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255,
		std::uint8_t gRed = 255, std::uint8_t gGreen = 255, std::uint8_t gBlue = 255, std::uint8_t gAlpha = 255,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8) :
		DottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, new std::uint32_t(0)) {
		this->_PointData = (((BaseColorPoint & 0xF) << 4) | (GradColorPoint & 0xF));
		if (BaseColorPoint == GradColorPoint)
		{
			for (int i = 0; i < 9; i++)
			{
				gR[i] = Red;
				gG[i] = Green;
				gB[i] = Blue;
				gA[i] = Alpha;
			}
		}
		else
		{
			BaseColorPoint--;
			GradColorPoint--;
			RefillGradient(Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint);
		}
	}
	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth = 2,
		std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255,
		std::uint8_t gRed = 255, std::uint8_t gGreen = 255, std::uint8_t gBlue = 255, std::uint8_t gAlpha = 255,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint) {}

	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth,
		std::uint32_t* RGBAColor, std::uint32_t* gRGBAColor,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	BiColoredDottedSymbol(std::string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, std::uint8_t LineWidth,
		std::uint32_t* RGBAColor, std::uint32_t* gRGBAColor,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8) : BiColoredDottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	~BiColoredDottedSymbol() override = default;
	void RefillGradient(std::uint8_t Red = 255, std::uint8_t Green = 255, std::uint8_t Blue = 255, std::uint8_t Alpha = 255,
		std::uint8_t gRed = 255, std::uint8_t gGreen = 255, std::uint8_t gBlue = 255, std::uint8_t gAlpha = 255,
		std::uint8_t BaseColorPoint = 5, std::uint8_t GradColorPoint = 8) override
	{
		float xbase = (((float)(BaseColorPoint % 3)) - 1.f), ybase = (((float)(BaseColorPoint / 3)) - 1.f),
			xgrad = (((float)(GradColorPoint % 3)) - 1.f), ygrad = (((float)(GradColorPoint / 3)) - 1.f);
		float ax = xgrad - xbase, ay = ygrad - ybase, t;
		float ial = 1.f / (ax * ax + ay * ay);
		///R
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++) 
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Red * t + (1.f - t) * gRed);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gR[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///G
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Green * t + (1.f - t) * gGreen);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gG[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///B
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Blue * t + (1.f - t) * gBlue);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gB[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
		///A
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Alpha * t + (1.f - t) * gAlpha);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gA[(x + 1) + (3 * (y + 1))] = std::round(t);
			}
		}
	}
	void RefillGradient(std::uint32_t* RGBAColor, std::uint32_t* gRGBAColor, std::uint8_t BaseColorPoint, std::uint8_t GradColorPoint) override 
	{
		RefillGradient(*RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint);
		delete RGBAColor;
		delete gRGBAColor;
	}
	void Draw() override
	{
		if (RenderWay == " ")
			return;
		float VerticalShift = 0.;
		CHAR BACK = 0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~') 
		{
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK)
			{
			case '#':
				VerticalShift = Points[0].y - Points[3].y;
				break;
			case '~':
				VerticalShift = (Points[0].y - Points[3].y) / 2;
				break;
			}
		}
		if (is_fonted)
			VerticalShift = 0;
		std::uint8_t IO;
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < RenderWay.length(); i++)
		{
			if (RenderWay[i] == ' ')
			{
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			//printf("%x %x %x %x\n", gR[IO], gG[IO], gB[IO], gA[IO]);
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalShift);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i)
		{
			IO = PointPlacement[i] - '1';
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalShift);
		}
		glEnd();
		if (BACK)
			RenderWay.back() = BACK;
	}
};

#endif