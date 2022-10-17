#pragma once
#ifndef SAFGUIF_SPECIALSIGNS
#define SAFGUIF_SPECIALSIGNS

#include "header_utils.h"
#include "handleable_ui_part.h"

constexpr auto HTSQ2 = (1.85);

struct SpecialSigns 
{
	static void DrawOK(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t NOARGUMENT = 0)
	{
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 2));
		glBegin(GL_LINE_STRIP);
		glVertex2f(x - SZParam * 0.766f, y + SZParam * 0.916f);
		glVertex2f(x - SZParam * 0.1f, y + SZParam * 0.25f);
		glVertex2f(x + SZParam * 0.9f, y + SZParam * 1.25f);
		glEnd();
		glPointSize(ceil(SZParam / 2));
		glBegin(GL_POINTS);
		glVertex2f(x - SZParam * 0.1f, y + SZParam * 0.25f);
		glEnd();
	}
	static void DrawExTriangle(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t SecondaryRGBAColor)
	{
		GLCOLOR(RGBAColor);
		glBegin(GL_TRIANGLES);
		glVertex2f(x, y + HTSQ2 * SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x + SZParam, y);
		glEnd();
		GLCOLOR(SecondaryRGBAColor);
		glLineWidth(ceil(SZParam / 8));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + HTSQ2 * SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x + SZParam, y);
		glEnd();
		glLineWidth(ceil(SZParam / 4));
		glBegin(GL_LINES);
		glVertex2f(x, y + SZParam * 0.6f);
		glVertex2f(x, y + SZParam * 1.40f);
		glVertex2f(x, y + SZParam * 0.2f);
		glVertex2f(x, y + SZParam * 0.4f);
		glEnd();
	}
	static void DrawFileSign(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t SecondaryRGBAColor)
	{
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 5));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x - SZParam, y - SZParam);
		glVertex2f(x + SZParam, y - SZParam);
		glVertex2f(x + SZParam, y + SZParam);
		glEnd();
		glBegin(GL_LINES);
		glVertex2f(x, y + SZParam);
		glVertex2f(x, y);
		glVertex2f(x, y);
		glVertex2f(x - SZParam, y);
		glEnd();
		glPointSize(ceil(SZParam / 5));
		glBegin(GL_POINTS);
		glVertex2f(x, y);
		glVertex2f(x, y + SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x - SZParam, y - SZParam);
		glVertex2f(x + SZParam, y - SZParam);
		glVertex2f(x + SZParam, y + SZParam);
		glEnd();
	}
	static void DrawACircle(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t SecondaryRGBAColor)
	{
		GLCOLOR(SecondaryRGBAColor);
		glBegin(GL_POLYGON);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(SZParam * 1.25f * (cos(angle_to_radians(a))) + x, SZParam * 1.25f * (sin(angle_to_radians(a))) + y + SZParam * 0.75f);
		glEnd();
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 10));
		glBegin(GL_LINE_LOOP);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(SZParam * 1.25f * (cos(angle_to_radians(a))) + x, SZParam * 1.25f * (sin(angle_to_radians(a))) + y + SZParam * 0.75f);
		glEnd();
	}
	static void DrawNo(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t NOARGUMENT = 0)
	{
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 2));
		glBegin(GL_LINES);
		glVertex2f(x - SZParam * 0.5f, y + SZParam * 0.25f);
		glVertex2f(x + SZParam * 0.5f, y + SZParam * 1.25f);
		glVertex2f(x + SZParam * 0.5f, y + SZParam * 0.25f);
		glVertex2f(x - SZParam * 0.5f, y + SZParam * 1.25f);
		glEnd();
	}
	static void DrawWait(float x, float y, float SZParam, std::uint32_t RGBAColor, std::uint32_t TotalStages)
	{
		float Start = ((float)((TimerV % TotalStages) * 360)) / (float)(TotalStages), t;
		std::uint8_t R = (RGBAColor >> 24), G = (RGBAColor >> 16) & 0xFF, B = (RGBAColor >> 8) & 0xFF, A = (RGBAColor) & 0xFF;
		//printf("%x\n", CurStage_TotalStages);
		glLineWidth(ceil(SZParam / 1.5f));
		glBegin(GL_LINES);
		for (float a = 0; a < 360.5f; a += (180.f / (TotalStages)))
		{
			t = a / 360.f;
			glColor4ub(
				255 * (t)+R * (1 - t),
				255 * (t)+G * (1 - t),
				255 * (t)+B * (1 - t),
				255 * (t)+A * (1 - t)
				);
			glVertex2f(SZParam * 1.25f * (cos(angle_to_radians(a + Start))) + x, SZParam * 1.25f * (sin(angle_to_radians(a + Start))) + y + SZParam * 0.75f);
		}
		glEnd();
	}
};

struct SpecialSignHandler : HandleableUIPart
{
	float x, y, SZParam;
	std::uint32_t FRGBA, SRGBA;
	void(*DrawFunc)(float, float, float, std::uint32_t, std::uint32_t);
	SpecialSignHandler(void(*DrawFunc)(float, float, float, std::uint32_t, std::uint32_t), float x, float y, float SZParam, std::uint32_t FRGBA, std::uint32_t SRGBA)
	{
		this->DrawFunc = DrawFunc;
		this->x = x;
		this->y = y;
		this->SZParam = SZParam;
		this->FRGBA = FRGBA;
		this->SRGBA = SRGBA;
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (this->DrawFunc)
			this->DrawFunc(x, y, SZParam, FRGBA, SRGBA);
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		x += dx;
		y += dy;
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		x = NewX;
		y = NewY;
	}
	void _ReplaceVoidFunc(void(*NewDrawFunc)(float, float, float, std::uint32_t, std::uint32_t))
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->DrawFunc = NewDrawFunc;
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{

	}
	void KeyboardHandler(CHAR CH) override
	{
		return;
	}
	void SafeStringReplace(std::string Meaningless) override
	{
		return;
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		return 0;
	}
};

#endif // !SAFGUIF_SPECIALSIGNS
