#pragma once
#ifndef SAFGUIF_CHECKBOX
#define SAFGUIF_CHECKBOX

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct CheckBox : HandleableUIPart {///NeedsTest
	float Xpos, Ypos, SideSize;
	DWORD BorderRGBAColor, UncheckedRGBABackground, CheckedRGBABackground;
	SingleTextLine* Tip;
	BIT State, Focused;
	BYTE BorderWidth;
	~CheckBox() {
		Lock.lock();
		if (Tip)delete Tip;
		Lock.unlock();
	}
	CheckBox(float Xpos, float Ypos, float SideSize, DWORD BorderRGBAColor, DWORD UncheckedRGBABackground, DWORD CheckedRGBABackground, BYTE BorderWidth, BIT StartState = false, SingleTextLineSettings* TipSettings = NULL, _Align TipAlign = _Align::left, std::string TipText = " ") {
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->SideSize = SideSize;
		this->BorderRGBAColor = BorderRGBAColor;
		this->UncheckedRGBABackground = UncheckedRGBABackground;
		this->CheckedRGBABackground = CheckedRGBABackground;
		this->State = StartState;
		this->Focused = 0;
		this->BorderWidth = BorderWidth;
		if (TipSettings) {
			this->Tip = TipSettings->CreateOne(TipText);
			this->Tip->SafeChangePosition_Argumented(TipAlign, Xpos - ((TipAlign == _Align::left) ? 0.5f : ((TipAlign == _Align::right) ? -0.5f : 0)) * SideSize, Ypos - SideSize);
		}
	}
	void Draw() override {
		Lock.lock();
		float hSideSize = 0.5f * SideSize;
		if (State)
			GLCOLOR(CheckedRGBABackground);
		else
			GLCOLOR(UncheckedRGBABackground);
		glBegin(GL_QUADS);
		glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
		glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
		glEnd();
		if ((BYTE)BorderRGBAColor && BorderWidth) {
			GLCOLOR(BorderRGBAColor);
			glLineWidth(BorderWidth);
			glBegin(GL_LINE_LOOP);
			glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
			glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
			glEnd();
			glPointSize(BorderWidth);
			glBegin(GL_POINTS);
			glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
			glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
			glEnd();
		}
		if (Focused && Tip)Tip->Draw();
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		Xpos += dx;
		Ypos += dy;
		if (Tip)Tip->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * SideSize,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * SideSize;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(std::string TipString) override {
		Lock.lock();
		if (Tip)Tip->SafeStringReplace(TipString);
		Lock.unlock();
	}
	void FocusChange() {
		Lock.lock();
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		if (fabsf(mx - Xpos) < 0.5 * SideSize && fabsf(my - Ypos) < 0.5 * SideSize) {
			if (!Focused)
				FocusChange();
			if (Button) {
				//cout << "State switch from " << State << endl;
				if (State == 1)
					this->State = !this->State;
				Lock.unlock();
				return 1;
			}
			else {
				Lock.unlock();
				return 0;
			}
		}
		else {
			if (Focused)
				FocusChange();
			Lock.unlock();
			return 0;
		}
	}
	inline DWORD TellType() override {
		return _TellType::checkbox;
	}
};
#endif // !SAFGUIF_CHECKBOX
