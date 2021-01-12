#pragma once
#ifndef SAFGUIF_TEXTBOX
#define SAFGUIF_TEXTBOX

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct TextBox : HandleableUIPart {
	enum class VerticalOverflow { cut, display, recalibrate };
	_Align TextAlign;
	VerticalOverflow VOverflow;
	std::string Text;
	std::vector<SingleTextLine*> Lines;
	float Xpos, Ypos;
	float Width, Height;
	float VerticalOffset, CalculatedTextHeight;
	SingleTextLineSettings* STLS;
	BYTE BorderWidth;
	DWORD RGBABorder, RGBABackground, SymbolsPerLine;
	~TextBox() override {
		Lock.lock();
		for (auto i = Lines.begin(); i != Lines.end(); i++)
			if (*i)delete* i;
		Lines.clear();
		Lock.unlock();
	}
	TextBox(std::string Text, SingleTextLineSettings* STLS, float Xpos, float Ypos, float Height, float Width, float VerticalOffset, DWORD RGBABackground, DWORD RGBABorder, BYTE BorderWidth, _Align TextAlign = _Align::left, VerticalOverflow VOverflow = VerticalOverflow::cut) {
		this->TextAlign = TextAlign;
		this->VOverflow = VOverflow;
		this->BorderWidth = BorderWidth;
		this->VerticalOffset = VerticalOffset;
		this->STLS = STLS;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->Height = Height;
		this->Text = (Text.size()) ? Text : " ";
		RecalculateAvailableSpaceForText();
		TextReformat();
	}
	void TextReformat() {
		Lock.lock();
		std::vector<std::vector<std::string>>SplittedText;
		std::vector<std::string> Paragraph;
		std::string Line;
		STLS->SetNewPos(Xpos, Ypos + 0.5f * Height + 0.5f * VerticalOffset);
		////OOOOOF... Someone help me with these please :d
		for (int i = 0; i < Text.size(); i++) {
			if (Text[i] == ' ') {
				Paragraph.push_back(Line);
				Line.clear();
			}
			else if (Text[i] == '\n') {
				Paragraph.push_back(Line);
				Line.clear();
				SplittedText.push_back(Paragraph);
				Paragraph.clear();
			}
			else Line.push_back(Text[i]);
			if (i == Text.size() - 1) {
				Paragraph.push_back(Line);
				SplittedText.push_back(Paragraph);
				Line.clear();
				Paragraph.clear();
			}
		}
		for (int i = 0; i < SplittedText.size(); i++) {
#define Para SplittedText[i]
#define LINES Paragraph
			for (int q = 0; q < Para.size(); q++) {
				if ((Para[q].size() + 1 + Line.size()) < SymbolsPerLine)
					if (Line.size())Line = (Line + " " + Para[q]);
					else Line = Para[q];
				else {
					if (Line.size())LINES.push_back(Line);
					Line = Para[q];
				}
				while (Line.size() >= SymbolsPerLine) {
					Paragraph.push_back(Line.substr(0, SymbolsPerLine));
					Line = Line.erase(0, SymbolsPerLine);
				}
			}
			Paragraph.push_back(Line);
			Line.clear();
#undef LINES	
#undef Para
		}
		for (int i = 0; i < Paragraph.size(); i++) {
			STLS->Move(0, 0 - VerticalOffset);
			//cout << Paragraph[i] << endl;
			if (VOverflow == VerticalOverflow::cut && STLS->CYpos < Ypos - Height)break;
			Lines.push_back(STLS->CreateOne(Paragraph[i]));
			if (TextAlign == _Align::right)Lines.back()->SafeChangePosition_Argumented(GLOBAL_RIGHT, ((this->Xpos) + (0.5f * Width) - this->STLS->XUnitSize), Lines.back()->CYpos);
			else if (TextAlign == _Align::left)Lines.back()->SafeChangePosition_Argumented(GLOBAL_LEFT, ((this->Xpos) - (0.5f * Width) + this->STLS->XUnitSize), Lines.back()->CYpos);
		}
		CalculatedTextHeight = (Lines.front()->CYpos - Lines.back()->CYpos) + Lines.front()->CalculatedHeight;
		if (VOverflow == VerticalOverflow::recalibrate) {
			float dy = (CalculatedTextHeight - this->Height);
			for (auto Y = Lines.begin(); Y != Lines.end(); Y++) {
				(*Y)->SafeMove(0, (dy + Lines.front()->CalculatedHeight) * 0.5f);
			}
		}
		Lock.unlock();
	}
	void SafeTextColorChange(DWORD NewColor) {
		Lock.lock();
		for (auto i = Lines.begin(); i != Lines.end(); i++) {
			(*i)->SafeColorChange(NewColor);
		}
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		return 0;
	}
	void SafeStringReplace(std::string NewString) override {
		Lock.lock();
		this->Text = (NewString.size()) ? NewString : " ";
		Lines.clear();
		RecalculateAvailableSpaceForText();
		TextReformat();
		Lock.unlock();
	}
	void KeyboardHandler(char CH) {
		return;
	}
	void RecalculateAvailableSpaceForText() {
		Lock.lock();
		SymbolsPerLine = std::floor((Width + STLS->XUnitSize * 2) / (STLS->XUnitSize * 2 + STLS->SpaceWidth));
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		Xpos += dx;
		Ypos += dy;
		STLS->Move(dx, dy);
		for (int i = 0; i < Lines.size(); i++) {
			Lines[i]->SafeMove(dx, dy);
		}
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
	void Draw() override {
		Lock.lock();
		if ((BYTE)RGBABackground) {
			GLCOLOR(RGBABackground);
			glBegin(GL_QUADS);
			glVertex2f(Xpos - (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos - (0.5f * Height));
			glVertex2f(Xpos - (Width * 0.5f), Ypos - (0.5f * Height));
			glEnd();
		}
		if ((BYTE)RGBABorder) {
			GLCOLOR(RGBABorder);
			glLineWidth(BorderWidth);
			glBegin(GL_LINE_LOOP);
			glVertex2f(Xpos - (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos - (0.5f * Height));
			glVertex2f(Xpos - (Width * 0.5f), Ypos - (0.5f * Height));
			glEnd();
		}
		for (int i = 0; i < Lines.size(); i++) 
			Lines[i]->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_TEXTBOX;
	}
};

#endif