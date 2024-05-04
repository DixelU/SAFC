#pragma once
#ifndef SAFGUIF_TEXTBOX
#define SAFGUIF_TEXTBOX

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct TextBox : HandleableUIPart
{
	enum class VerticalOverflow { cut, display, recalibrate };
	_Align TextAlign;
	VerticalOverflow VOverflow;
	std::string Text;
	std::vector<SingleTextLine*> Lines;
	float Xpos, Ypos;
	float Width, Height;
	float VerticalOffset, CalculatedTextHeight;
	SingleTextLineSettings* STLS;
	std::uint8_t BorderWidth;
	std::uint32_t RGBABorder, RGBABackground, SymbolsPerLine;
	~TextBox() override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto i = Lines.begin(); i != Lines.end(); ++i)
			delete* i;
		Lines.clear();
	}
	TextBox(std::string Text, SingleTextLineSettings* STLS, float Xpos, float Ypos, float Height, float Width, float VerticalOffset, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint8_t BorderWidth, _Align TextAlign = _Align::left, VerticalOverflow VOverflow = VerticalOverflow::cut)
	{
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
	void TextReformat()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		std::vector<std::vector<std::string>>SplittedText;
		std::vector<std::string> Paragraph;
		std::string Line;
		STLS->SetNewPos(Xpos, Ypos + 0.5f * Height + 0.5f * VerticalOffset);
		////OOOOOF... Someone help me with these please :d
		for (int i = 0; i < Text.size(); i++)
		{
			if (Text[i] == ' ')
			{
				Paragraph.push_back(Line);
				Line.clear();
			}
			else if (Text[i] == '\n')
			{
				Paragraph.push_back(Line);
				Line.clear();
				SplittedText.push_back(Paragraph);
				Paragraph.clear();
			}
			else Line.push_back(Text[i]);
			if (i == Text.size() - 1)
			{
				Paragraph.push_back(Line);
				SplittedText.push_back(Paragraph);
				Line.clear();
				Paragraph.clear();
			}
		}
		for (int i = 0; i < SplittedText.size(); i++)
		{
#define Para SplittedText[i]
#define LINES Paragraph
			for (int q = 0; q < Para.size(); q++)
			{
				if ((Para[q].size() + 1 + Line.size()) < SymbolsPerLine)
					if (Line.size())Line = (Line + " " + Para[q]);
					else Line = Para[q];
				else
				{
					if (Line.size())LINES.push_back(Line);
					Line = Para[q];
				}
				while (Line.size() >= SymbolsPerLine)
				{
					Paragraph.emplace_back(Line.substr(0, SymbolsPerLine));
					Line = Line.erase(0, SymbolsPerLine);
				}
			}
			Paragraph.push_back(Line);
			Line.clear();
#undef LINES	
#undef Para
		}
		for (int i = 0; i < Paragraph.size(); i++)
		{
			STLS->Move(0, 0 - VerticalOffset);
			//cout << Paragraph[i] << endl;
			if (VOverflow == VerticalOverflow::cut && STLS->CYpos < Ypos - Height)break;
			Lines.push_back(STLS->CreateOne(Paragraph[i]));
			if (TextAlign == _Align::right)Lines.back()->SafeChangePosition_Argumented(GLOBAL_RIGHT, ((this->Xpos) + (0.5f * Width) - this->STLS->XUnitSize), Lines.back()->CYpos);
			else if (TextAlign == _Align::left)Lines.back()->SafeChangePosition_Argumented(GLOBAL_LEFT, ((this->Xpos) - (0.5f * Width) + this->STLS->XUnitSize), Lines.back()->CYpos);
		}
		CalculatedTextHeight = (Lines.front()->CYpos - Lines.back()->CYpos) + Lines.front()->CalculatedHeight;
		if (VOverflow == VerticalOverflow::recalibrate)
		{
			float dy = (CalculatedTextHeight - this->Height);
			for (auto Y = Lines.begin(); Y != Lines.end(); ++Y)
				(*Y)->SafeMove(0, (dy + Lines.front()->CalculatedHeight) * 0.5f);
		}
	}
	void SafeTextColorChange(std::uint32_t NewColor)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto i = Lines.begin(); i != Lines.end(); ++i)
			(*i)->SafeColorChange(NewColor);
	}
	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override
	{
		return 0;
	}
	void SafeStringReplace(std::string NewString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->Text = (NewString.size()) ? NewString : " ";
		Lines.clear();
		RecalculateAvailableSpaceForText();
		TextReformat();
	}
	void KeyboardHandler(char CH)
	{
		return;
	}
	void RecalculateAvailableSpaceForText()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		SymbolsPerLine = std::floor((Width + STLS->XUnitSize * 2) / (STLS->XUnitSize * 2 + STLS->SpaceWidth));
	}
	void SafeMove(float dx, float dy)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		Xpos += dx;
		Ypos += dy;
		STLS->Move(dx, dy);
		for (int i = 0; i < Lines.size(); i++) {
			Lines[i]->SafeMove(dx, dy);
		}
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
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if ((std::uint8_t)RGBABackground)
		{
			__glcolor(RGBABackground);
			glBegin(GL_QUADS);
			glVertex2f(Xpos - (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos + (0.5f * Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos - (0.5f * Height));
			glVertex2f(Xpos - (Width * 0.5f), Ypos - (0.5f * Height));
			glEnd();
		}
		if ((std::uint8_t)RGBABorder)
		{
			__glcolor(RGBABorder);
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
	}
	inline std::uint32_t TellType() override
	{
		return TT_TEXTBOX;
	}
};

#endif