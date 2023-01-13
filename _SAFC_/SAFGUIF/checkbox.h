#pragma once
#ifndef SAFGUIF_CHECKBOX
#define SAFGUIF_CHECKBOX

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct CheckBox : HandleableUIPart 
{
	float Xpos, Ypos, SideSize;
	std::uint32_t BorderRGBAColor, UncheckedRGBABackground, CheckedRGBABackground;
	SingleTextLine* Tip;
	bool State, Focused;
	std::uint8_t BorderWidth;
	~CheckBox() override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Tip)
			delete Tip;
	}
	CheckBox(float Xpos, float Ypos, float SideSize, std::uint32_t BorderRGBAColor, std::uint32_t UncheckedRGBABackground, std::uint32_t CheckedRGBABackground, std::uint8_t BorderWidth, bool StartState = false, SingleTextLineSettings* TipSettings = NULL, _Align TipAlign = _Align::left, std::string TipText = " ")
	{
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->SideSize = SideSize;
		this->BorderRGBAColor = BorderRGBAColor;
		this->UncheckedRGBABackground = UncheckedRGBABackground;
		this->CheckedRGBABackground = CheckedRGBABackground;
		this->State = StartState;
		this->Focused = 0;
		this->BorderWidth = BorderWidth;
		if (TipSettings)
		{
			this->Tip = TipSettings->CreateOne(TipText);
			this->Tip->SafeChangePosition_Argumented(TipAlign, Xpos - ((TipAlign == _Align::left) ? 0.5f : ((TipAlign == _Align::right) ? -0.5f : 0)) * SideSize, Ypos - SideSize);
		}
	}
	void Draw() override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float hSideSize = 0.5f * SideSize;
		if (State)
			__glcolor(CheckedRGBABackground);
		else
			__glcolor(UncheckedRGBABackground);
		glBegin(GL_QUADS);
		glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
		glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
		glEnd();
		if ((std::uint8_t)BorderRGBAColor && BorderWidth) 
		{
			__glcolor(BorderRGBAColor);
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
		if (Focused && Tip)
			Tip->Draw();
	}
	void SafeMove(float dx, float dy) override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		Xpos += dx;
		Ypos += dy;
		if (Tip)Tip->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(INT32)((bool)(GLOBAL_LEFT & Arg))
			- (INT32)((bool)(GLOBAL_RIGHT & Arg))
			) * SideSize,
			CH = 0.5f * (
				(INT32)((bool)(GLOBAL_BOTTOM & Arg))
				- (INT32)((bool)(GLOBAL_TOP & Arg))
				) * SideSize;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void KeyboardHandler(CHAR CH) override
	{
		return;
	}
	void SafeStringReplace(std::string TipString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Tip)
			Tip->SafeStringReplace(TipString);
	}
	void FocusChange()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (fabsf(mx - Xpos) < 0.5 * SideSize && fabsf(my - Ypos) < 0.5 * SideSize)
		{
			if (!Focused)
				FocusChange();
			if (Button)
			{
				//cout << "State switch from " << State << endl;
				if (State == 1)
					this->State = !this->State;
				return 1;
			}
			else 
				return 0;
		}
		else 
		{
			if (Focused)
				FocusChange();
			return 0;
		}
	}
	inline std::uint32_t TellType() override
	{
		return _TellType::checkbox;
	}
};
#endif // !SAFGUIF_CHECKBOX
