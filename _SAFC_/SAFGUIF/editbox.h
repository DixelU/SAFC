#pragma once
#ifndef SAF_EBOX
#define SAF_EBOX

#include "textbox.h"
#include <deque>
#include <vector>
#include <utility>
#include <algorithm>

constexpr int eb_cursor_flash_fraction = 30;
constexpr int eb_cursor_flash_subcycle = eb_cursor_flash_fraction / 2;

// doesn't work with fonted mode anymore!
struct EditBox : HandleableUIPart
{
private:
	using char_pos = std::pair<std::deque<SingleTextLine*>::iterator, size_t>;
public:
private:
	std::string BufferedCurText;
	std::uint8_t VisibilityCountDown;
public:
	std::deque<SingleTextLine*> Words;
	char_pos CursorPosition;/*points at the symbol after the cursor in current word?*/
	//void (*OnEdit)();
	SingleTextLineSettings* STLS;
	float Xpos, Ypos, Height, Width, VerticalOffset;
	std::uint32_t RGBABackground, RGBABorder;
	std::uint8_t BorderWidth;

#define CursorWordIter (*CursorPosition.first)

	EditBox(std::string Text, SingleTextLineSettings* STLS, float Xpos, float Ypos, float Height, float Width, float VerticalOffset, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint8_t BorderWidth): STLS(STLS),Xpos(Xpos),Ypos(Ypos),Height(Height), Width(Width), VerticalOffset(VerticalOffset), RGBABackground(RGBABackground), RGBABorder(RGBABorder), BorderWidth(BorderWidth), BufferedCurText(Text), VisibilityCountDown(0)
	{
		Words.push_back(STLS->CreateOne(" "));
		CursorPosition = { Words.begin(), 0 };
		RearrangePositions();
		for (auto& ch : Text)
			WriteSymbolAtCursorPos(ch);
	}
	void WriteSymbolAtCursorPos(char ch, bool rearrange=true) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		std::string cur_word = (CursorPosition.first != Words.end()) ? CursorWordIter->_CurrentText : "";
		float char_width = STLS->XUnitSize*2;
		float fixed_width = (Width - 2*char_width);
		size_t maximal_whole_word_size = (fixed_width / (char_width + STLS->SpaceWidth));
		if (CursorPosition.second) 
		{
			if (ch == '\n' || ch == ' ') 
			{
				std::string str = " ";
				str[0] = ch;
				CursorWordIter->SafeStringReplace(cur_word.substr(0, CursorPosition.second));
				CursorPosition.first = Words.insert(CursorPosition.first + 1, STLS->CreateOne(str));
				CursorPosition.first = Words.insert(CursorPosition.first + 1, STLS->CreateOne(cur_word.substr(CursorPosition.second, 0x7FFFFFFF)));
				CursorPosition.second = 0;
			}
			else if (ch < 32 || ch == 127)
			{
				return;
			}
			else if (cur_word.size() >= maximal_whole_word_size)
			{
				cur_word.insert(cur_word.begin() + CursorPosition.second, ch);
				CursorWordIter->SafeStringReplace(cur_word.substr(0, maximal_whole_word_size-1));
				CursorPosition.first = Words.insert(CursorPosition.first + 1, STLS->CreateOne(cur_word.substr(maximal_whole_word_size-1, 0x7FFFFFFF)));
				CursorPosition.second++;
				if (CursorPosition.second < maximal_whole_word_size) 
					--CursorPosition.first;
				else 
					CursorPosition.second -= maximal_whole_word_size - 1;
			}
			else
			{
				cur_word.insert(cur_word.begin() + CursorPosition.second, ch);
				CursorWordIter->SafeStringReplace(cur_word);
				CursorPosition.second++;
			}
		}
		else 
		{
			if (ch == '\n' || ch == ' ')
			{
				std::string str = " ";
				str[0] = ch;
				CursorPosition.first = Words.insert(CursorPosition.first, STLS->CreateOne(str));
				++CursorPosition.first;
				CursorPosition.second = 0;
			}
			else if (ch < 32 || ch == 127)
				return;
			else if (cur_word.size() >= maximal_whole_word_size) 
			{
				cur_word.insert(cur_word.begin() + 1, ch);
				CursorWordIter->SafeStringReplace(cur_word.substr(0, maximal_whole_word_size - 1));
				auto itt = CursorPosition.first;
				CursorPosition.first = Words.insert(CursorPosition.first + 1, STLS->CreateOne(cur_word.substr(maximal_whole_word_size - 1, 0x7FFFFFFF))),itt;
				CursorPosition.second++;
				--CursorPosition.first;
			}
			else 
			{
				cur_word.insert(cur_word.begin(), ch);
				CursorWordIter->SafeStringReplace(cur_word);
				CursorPosition.second++;
			}
		}

		VisibilityCountDown = eb_cursor_flash_subcycle;
		if(rearrange)
			RearrangePositions();
	}
	void RemoveSymbolBeforeCursorPos()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		//auto cur_pos = CursorPosition;
		if (CursorPosition.second) 
		{
			std::string cur_word = CursorWordIter->_CurrentText;
			cur_word.erase(cur_word.begin() + CursorPosition.second - 1);
			CursorWordIter->SafeStringReplace(cur_word);
			CursorPosition.second--;
			RearrangePositions();
		}
		else if(CursorPosition.first!=Words.begin())
		{
			std::string cur_word;
			auto it = CursorPosition.first - 1;
			if ((*it)->_CurrentText.size() > 1)
			{
				cur_word = (*it)->_CurrentText;
				cur_word.pop_back();
				(*it)->SafeStringReplace(cur_word);
			}
			else 
			{
				delete (*it);
				CursorPosition.first = Words.erase(it);
			}
			RearrangePositions();
		}
	}
	void RemoveSymbolAfterCursorPos()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (CursorPosition.first == Words.end() - 1 && CursorPosition.second == Words.back()->_CurrentText.size() - 1) 
			return;
		MoveCursorBy1(_Align::right);
		RemoveSymbolBeforeCursorPos();
	}
	void UpdateBufferedCurText() 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		BufferedCurText.clear();
		for (auto& word : Words) 
			if (word->_CurrentText != "\t")
				BufferedCurText += word->_CurrentText;
	}
	void RearrangePositions() 
	{
		float char_width = STLS->XUnitSize*2;
		float char_height = VerticalOffset;
		float fixed_height = Height - VerticalOffset;
		float space_width = STLS->SpaceWidth;
		float fixed_width = (Width - char_width);
		float top_line = Ypos + fixed_height * 0.5;
		float left_line = Xpos - fixed_width * 0.5;
		size_t line_no = 0, col_no = 1;
		for (auto& word : Words) 
		{
			if (word->_CurrentText == "\n")
			{
				col_no++;
				word->SafeChangePosition_Argumented(_Align::right | _Align::center, left_line + col_no * char_width + (col_no - 1) * space_width, top_line - char_height * line_no);
				line_no++;
				col_no = 1;
				continue;
			}
			else if (word->CalculatedWidth + col_no * char_width + (col_no - 1) * space_width > fixed_width)
			{
				line_no++;
				col_no = word->Chars.size();
			}
			else
			{
				col_no += word->Chars.size();
			}
			word->SafeChangePosition_Argumented(_Align::right | _Align::center, left_line + col_no * char_width + (col_no - 1) * space_width, top_line - char_height * line_no);
		}
	}
	void Clear() 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto& word : Words) 
			delete word;
		Words.clear();
		Words.push_back(STLS->CreateOne("\t"));
		Words.push_back(STLS->CreateOne("\n"));
		CursorPosition = { Words.begin(), 0 };
	}
	void ReparseCurrentTextFromScratch()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		Clear();
		for (auto& ch : BufferedCurText)
			WriteSymbolAtCursorPos(ch);
	}
	~EditBox() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto& line : Words)
			delete line;
	}
	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override 
	{
		return 0;
	}
	void SafeStringReplace(std::string NewString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		BufferedCurText = std::move(NewString);
		ReparseCurrentTextFromScratch();
	}
	inline std::string _UnsafeGetCurrentText() const
	{
		return BufferedCurText;
	}
	void MoveCursorBy1(_Align Move)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if ((CursorPosition.first == Words.end())) 
			return;
		auto Y = CursorPosition.first;
		auto cur_y_coord = (*Y)->CYpos;
		auto cur_x_coord = (*Y)->Chars[CursorPosition.second]->Xpos;
		switch (Move)
		{
		case _Align::left:
			//printf("left %i %i\n", (*CursorPosition.first), CursorPosition.second);
			if (CursorPosition.second>0)
			{
				--CursorPosition.second;
			}
			else if (CursorPosition.first != Words.begin()) 
			{
				--CursorPosition.first;
				CursorPosition.second = (*CursorPosition.first)->_CurrentText.size()-1;
			}
			break;
		case _Align::right:
			//printf("right %i %i\n", (*CursorPosition.first), CursorPosition.second);
			if (CursorPosition.second < (*CursorPosition.first)->_CurrentText.size() - 1)
			{
				++CursorPosition.second;
			}
			else if (CursorPosition.first != Words.end() - 1)
			{
				++CursorPosition.first;
				CursorPosition.second = 0;
			}
			break;
		case _Align::top:
			//printf("top %i %i\n", (*CursorPosition.first), CursorPosition.second);
			do { MoveCursorBy1(_Align::left); } while (
				!(CursorPosition.first == Words.begin() && CursorPosition.second == 0) && (
					cur_x_coord + STLS->XUnitSize * 0.5 < (*CursorPosition.first)->Chars[CursorPosition.second]->Xpos ||
					cur_y_coord == (*CursorPosition.first)->CYpos
				));
			break;
		case _Align::bottom:
			//printf("bottom %i %i\n", (*CursorPosition.first), CursorPosition.second);
			do { MoveCursorBy1(_Align::right); } while (
				//printf("%f %f\n", cur_x_coord - STLS->XUnitSize * 0.5, (*CursorPosition.first)->Chars[CursorPosition.second - 1]->Xpos);
				!(CursorPosition.first == Words.end() - 1 && CursorPosition.second == Words.back()->_CurrentText.size()-1) && (
					cur_x_coord - STLS->XUnitSize*0.5 > (*CursorPosition.first)->Chars[CursorPosition.second]->Xpos ||
					cur_y_coord == (*CursorPosition.first)->CYpos
				));
			break;
		case _Align::center:
			break;
		}
	}
	void KeyboardHandler(char CH) override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		switch (CH)
		{
		case 13:
			CH = '\n'; 
			VisibilityCountDown = eb_cursor_flash_subcycle;
			break;
		case 8:
			VisibilityCountDown = eb_cursor_flash_subcycle;
			RemoveSymbolBeforeCursorPos();
			break;//erase?
		case 127:
			VisibilityCountDown = eb_cursor_flash_subcycle;
			RemoveSymbolAfterCursorPos();
			break;//erase?
		case 1:
			MoveCursorBy1(_Align::bottom);
			VisibilityCountDown = eb_cursor_flash_subcycle;
			break;
		case 2:
			MoveCursorBy1(_Align::top); 
			VisibilityCountDown = eb_cursor_flash_subcycle;
			break;
		case 3:
			MoveCursorBy1(_Align::left); 
			VisibilityCountDown = eb_cursor_flash_subcycle;
			break;
		case 4:
			MoveCursorBy1(_Align::right); 
			VisibilityCountDown = eb_cursor_flash_subcycle;
			break;
		}
		WriteSymbolAtCursorPos(CH);
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		Xpos += dx;
		Ypos += dy;
		STLS->Move(dx, dy);
		for (auto& line : Words)
			line->SafeMove(dx, dy);
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
		if (VisibilityCountDown)
			VisibilityCountDown--;
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
		if ((TimerV % eb_cursor_flash_fraction > eb_cursor_flash_subcycle) || VisibilityCountDown)
		{
			if (CursorPosition.first != Words.end())
			{
				const float fonted_fix = (is_fonted ? 0.75f : 1.5f);
				auto t = (*CursorPosition.first)->Chars[CursorPosition.second];
				__glcolor(STLS->RGBAColor);
				glBegin(GL_LINES);
				glVertex2f(t->Xpos - STLS->XUnitSize - STLS->SpaceWidth * 0.5f, t->Ypos + STLS->YUnitSize * fonted_fix);
				glVertex2f(t->Xpos - STLS->XUnitSize - STLS->SpaceWidth * 0.5f, t->Ypos - STLS->YUnitSize * fonted_fix);
				glEnd();
			}
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
		float bottom_y = Ypos - 0.5 * Height;
		for (auto& line : Words)
		{
			if (line->CYpos < bottom_y)
				break;
			line->Draw();
		}
	}
	inline std::uint32_t TellType() override
	{
		return TT_EDITBOX;
	}
#undef CursorWordIter
};

#endif
