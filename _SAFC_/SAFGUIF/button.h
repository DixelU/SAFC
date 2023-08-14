#pragma once
#ifndef SAFGUIF_BUTTON
#define SAFGUIF_BUTTON

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct Button : HandleableUIPart
{
	SingleTextLine* STL, * Tip;
	float Xpos, Ypos;
	float Width, Height;
	std::uint32_t RGBAColor, RGBABackground, RGBABorder;
	std::uint32_t HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder;
	std::uint8_t BorderWidth;
	bool Hovered;
	void(*OnClick)();
	~Button() override
	{
		delete STL;
		if (Tip)delete Tip;
	}
	Button(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint32_t gRGBAColor, std::uint8_t BasePoint/*15 if gradient is disabled*/, std::uint8_t GradPoint, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText = " ")
	{
		SingleTextLineSettings STLS(ButtonText, Xpos, Ypos, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint);
		this->STL = STLS.CreateOne();
		if (Tip)
		{
			Tip->SetNewPos(Xpos, Ypos - Height);
			this->Tip = Tip->CreateOne(TipText);
		}
		else this->Tip = NULL;
		this->BorderWidth = BorderWidth;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->RGBAColor = RGBAColor;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABorder = HoveredRGBABorder;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->Hovered = 0;
		this->OnClick = OnClick;
		this->Enabled = true;
	}
	Button(std::string ButtonText, SingleTextLineSettings* ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText = " ")
	{
		ButtonTextSTLS->SetNewPos(Xpos, Ypos);
		this->STL = ButtonTextSTLS->CreateOne(ButtonText);
		if (Tip)
		{
			Tip->SetNewPos(Xpos, Ypos - Height);
			this->Tip = Tip->CreateOne(TipText);
		}
		else this->Tip = NULL;
		this->BorderWidth = BorderWidth;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->RGBAColor = ButtonTextSTLS->RGBAColor;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABorder = HoveredRGBABorder;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->Hovered = 0;
		this->OnClick = OnClick;
		this->Enabled = true;
	}
	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Enabled) 
			return 0;
		mx = Xpos - mx;
		my = Ypos - my;
		if (Hovered)
		{
			if (fabsf(mx) > Width * 0.5 || fabsf(my) > Height * 0.5)
			{
				//cout << "UNHOVERED\n";
				Hovered = 0;
				STL->SafeColorChange(RGBAColor);
			}
			else
			{
				SetCursor(HandCursor);
				if (Button && State == 1)
				{
					if (Button == -1 && OnClick)
						OnClick();
					return 1;
				}
			}
		}
		else 
		{
			if (fabsf(mx) <= Width * 0.5 && fabsf(my) <= Height * 0.5) 
			{
				Hovered = 1;
				STL->SafeColorChange(HoveredRGBAColor);
			}
		}
		return 0;
	}
	void SafeMove(float dx, float dy) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Tip)
			Tip->SafeMove(dx, dy);
		STL->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
	}
	void KeyboardHandler(char CH) override 
	{
		return;
	}
	void SafeStringReplace(std::string NewString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->STL->SafeStringReplace(NewString);
	}
	void SafeChangePosition(float NewX, float NewY) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * Width,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * Height;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void Draw()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Enabled) 
			return;
		if (Hovered)
		{
			if ((std::uint8_t)HoveredRGBABackground)
			{
				__glcolor(HoveredRGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
			if ((std::uint8_t)HoveredRGBABorder)
			{
				__glcolor(HoveredRGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
		}
		else 
		{
			if ((std::uint8_t)RGBABackground)
			{
				__glcolor(RGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
			if ((std::uint8_t)RGBABorder)
			{
				__glcolor(RGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
		}
		if (Tip && Hovered)
			Tip->Draw();
		STL->Draw();
	}
	inline std::uint32_t TellType() override
	{
		return TT_BUTTON;
	}
};

#endif // !SAFGUIF_BUTTON
