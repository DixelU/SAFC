#pragma once
#ifndef SAFGUIF_BUTTON
#define SAFGUIF_BUTTON

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct Button : HandleableUIPart {
	SingleTextLine* STL, * Tip;
	float Xpos, Ypos;
	float Width, Height;
	DWORD RGBAColor, RGBABackground, RGBABorder;
	DWORD HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder;
	BYTE BorderWidth;
	BIT Hovered;
	void(*OnClick)();
	~Button() override {
		delete STL;
		if (Tip)delete Tip;
	}
	Button(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint/*15 if gradient is disabled*/, BYTE GradPoint, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText = " ") {
		SingleTextLineSettings STLS(ButtonText, Xpos, Ypos, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint);
		this->STL = STLS.CreateOne();
		if (Tip) {
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
	Button(std::string ButtonText, SingleTextLineSettings* ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText = " ") {
		ButtonTextSTLS->SetNewPos(Xpos, Ypos);
		this->STL = ButtonTextSTLS->CreateOne(ButtonText);
		if (Tip) {
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
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		Lock.lock();
		if (!Enabled) {
			Lock.unlock();
			return 0;
		}
		mx = Xpos - mx;
		my = Ypos - my;
		if (Hovered) {
			if (fabsf(mx) > Width * 0.5 || fabsf(my) > Height * 0.5) {
				//cout << "UNHOVERED\n";
				Hovered = 0;
				STL->SafeColorChange(RGBAColor);
			}
			else {
				SetCursor(HandCursor);
				if (Button && State == 1) {

					if (Button == -1 && OnClick)OnClick();
					Lock.unlock();
					return 1;
				}

			}
		}
		else {
			if (fabsf(mx) <= Width * 0.5 && fabsf(my) <= Height * 0.5) {
				Hovered = 1;
				STL->SafeColorChange(HoveredRGBAColor);
			}
		}
		Lock.unlock();
		return 0;
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		if (Tip)Tip->SafeMove(dx, dy);
		STL->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
		Lock.unlock();
	}
	void KeyboardHandler(char CH) override {
		return;
	}
	void SafeStringReplace(std::string NewString) override {
		Lock.lock();
		this->STL->SafeStringReplace(NewString);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) {
		Lock.lock();
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * Width,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * Height;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void Draw() {
		Lock.lock();
		if (!Enabled) {
			Lock.unlock();
			return;
		}
		if (Hovered) {
			if ((BYTE)HoveredRGBABackground) {
				GLCOLOR(HoveredRGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
			if ((BYTE)HoveredRGBABorder) {
				GLCOLOR(HoveredRGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
		}
		else {
			if ((BYTE)RGBABackground) {
				GLCOLOR(RGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
			if ((BYTE)RGBABorder) {
				GLCOLOR(RGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f * Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f * Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f * Height);
				glEnd();
			}
		}
		if (Tip && Hovered)Tip->Draw();
		STL->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_BUTTON;
	}
};

#endif // !SAFGUIF_BUTTON
