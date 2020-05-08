#pragma once
#ifndef SAFGUIF_SPECIALSIGNS
#define SAFGUIF_SPECIALSIGNS

#include "header_utils.h"
#include "handleable_ui_part.h"


#define HTSQ2 (1.85)
struct SpecialSigns {
	static void DrawOK(float x, float y, float SZParam, DWORD RGBAColor, DWORD NOARGUMENT = 0) {
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
	static void DrawExTriangle(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
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
	static void DrawFileSign(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
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
	static void DrawACircle(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
		GLCOLOR(SecondaryRGBAColor);
		glBegin(GL_POLYGON);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(SZParam * 1.25f * (cos(ANGTORAD(a))) + x, SZParam * 1.25f * (sin(ANGTORAD(a))) + y + SZParam * 0.75f);
		glEnd();
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 10));
		glBegin(GL_LINE_LOOP);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(SZParam * 1.25f * (cos(ANGTORAD(a))) + x, SZParam * 1.25f * (sin(ANGTORAD(a))) + y + SZParam * 0.75f);
		glEnd();
	}
	static void DrawNo(float x, float y, float SZParam, DWORD RGBAColor, DWORD NOARGUMENT = 0) {
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 2));
		glBegin(GL_LINES);
		glVertex2f(x - SZParam * 0.5f, y + SZParam * 0.25f);
		glVertex2f(x + SZParam * 0.5f, y + SZParam * 1.25f);
		glVertex2f(x + SZParam * 0.5f, y + SZParam * 0.25f);
		glVertex2f(x - SZParam * 0.5f, y + SZParam * 1.25f);
		glEnd();
	}
	static void DrawWait(float x, float y, float SZParam, DWORD RGBAColor, DWORD TotalStages) {
		float Start = ((float)((TimerV % TotalStages) * 360)) / (float)(TotalStages), t;
		BYTE R = (RGBAColor >> 24), G = (RGBAColor >> 16) & 0xFF, B = (RGBAColor >> 8) & 0xFF, A = (RGBAColor) & 0xFF;
		//printf("%x\n", CurStage_TotalStages);
		glLineWidth(ceil(SZParam / 1.5f));
		glBegin(GL_LINES);
		for (float a = 0; a < 360.5f; a += (180.f / (TotalStages))) {
			t = a / 360.f;
			glColor4ub(
				255 * (t)+R * (1 - t),
				255 * (t)+G * (1 - t),
				255 * (t)+B * (1 - t),
				255 * (t)+A * (1 - t)
				);
			glVertex2f(SZParam * 1.25f * (cos(ANGTORAD(a + Start))) + x, SZParam * 1.25f * (sin(ANGTORAD(a + Start))) + y + SZParam * 0.75f);
		}
		glEnd();
	}
};

struct SpecialSignHandler : HandleableUIPart {
	float x, y, SZParam;
	DWORD FRGBA, SRGBA;
	void(*DrawFunc)(float, float, float, DWORD, DWORD);
	SpecialSignHandler(void(*DrawFunc)(float, float, float, DWORD, DWORD), float x, float y, float SZParam, DWORD FRGBA, DWORD SRGBA) {
		this->DrawFunc = DrawFunc;
		this->x = x;
		this->y = y;
		this->SZParam = SZParam;
		this->FRGBA = FRGBA;
		this->SRGBA = SRGBA;
	}
	void Draw() override {
		Lock.lock();
		if (this->DrawFunc)this->DrawFunc(x, y, SZParam, FRGBA, SRGBA);
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		x += dx;
		y += dy;
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		x = NewX;
		y = NewY;
		Lock.unlock();
	}
	void _ReplaceVoidFunc(void(*NewDrawFunc)(float, float, float, DWORD, DWORD)) {
		Lock.lock();
		this->DrawFunc = NewDrawFunc;
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {

	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(std::string Meaningless) override {
		return;
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		return 0;
	}
};

#endif // !SAFGUIF_SPECIALSIGNS
