#pragma once
#ifndef SAFGUIF_SNPLIST
#define SAFGUIF_SNPLIST
#include <deque>
#include <algorithm>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "button_settings.h"

constexpr int ARROW_STICK_HEIGHT = 10;
struct SelectablePropertedList : HandleableUIPart
{
	_Align TextInButtonsAlign;
	void(*OnSelect)(int ID);
	void(*OnGetProperties)(int ID);
	float HeaderCXPos, HeaderYPos, CalculatedHeight, SpaceBetween, Width;
	ButtonSettings* ButtSettings;
	std::deque<std::string> SelectorsText;
	std::deque<Button*> Selectors;
	std::deque<std::uint32_t> SelectedID;
	std::uint32_t MaxVisibleLines, CurrentTopLineID, MaxCharsInLine;
	std::uint8_t TopArrowHovered, BottomArrowHovered;
	~SelectablePropertedList() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto Y = Selectors.begin(); Y != Selectors.end(); ++Y)
			delete (*Y);
	}
	SelectablePropertedList(ButtonSettings* ButtSettings, void(*OnSelect)(int SelectedID), void(*OnGetProperties)(int ID), float HeaderCXPos, float HeaderYPos, float Width, float SpaceBetween, std::uint32_t MaxCharsInLine = 0, std::uint32_t MaxVisibleLines = 0, _Align TextInButtonsAlign = _Align::left)
	{
		this->MaxCharsInLine = MaxCharsInLine;
		this->MaxVisibleLines = MaxVisibleLines;
		this->ButtSettings = ButtSettings;
		this->OnSelect = OnSelect;
		this->OnGetProperties = OnGetProperties;
		this->HeaderCXPos = HeaderCXPos;
		this->Width = Width;
		this->SpaceBetween = SpaceBetween;
		this->HeaderCXPos = HeaderCXPos;
		this->HeaderYPos = HeaderYPos;
		this->CurrentTopLineID = 0;
		this->TextInButtonsAlign = TextInButtonsAlign;
		this->BottomArrowHovered = this->TopArrowHovered = false;
		this->CalculatedHeight = 0;
		//SelectedID = 0xFFFFFFFF;
	}
	void RecalculateCurrentHeight()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		CalculatedHeight = SpaceBetween * Selectors.size();
	}
	bool MouseHandler(float mx, float my, char Button/*-1 left, 1 right, 0 move*/, char State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		TopArrowHovered = BottomArrowHovered = 0;
		if (fabsf(mx - HeaderCXPos) < 0.5 * Width && my < HeaderYPos && my > HeaderYPos - CalculatedHeight)
		{
			if (Button == 2 /*UP*/) 
			{
				if (State == -1)
				{
					SafeRotateList(-3);
				}
			}
			else if (Button == 3 /*DOWN*/)
			{
				if (State == -1)
				{
					SafeRotateList(3);
				}
			}
		}
		if (MaxVisibleLines && Selectors.size() < SelectorsText.size())
		{
			if (fabsf(mx - HeaderCXPos) < 0.5 * Width)
			{
				if (my > HeaderYPos && my < HeaderYPos + ARROW_STICK_HEIGHT)
				{
					TopArrowHovered = 1;
					if (Button == -1 && State == -1)
					{
						SafeRotateList(-1);
						return 1;
					}
				}
				else if (my < HeaderYPos - CalculatedHeight && my > HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT)
				{
					BottomArrowHovered = 1;
					if (Button == -1 && State == -1)
					{
						SafeRotateList(1);
						return 1;
					}
				}
			}
		}
		if (Button)
		{
			bool flag = 1;
			for (int i = 0; i < Selectors.size(); i++)
			{
				if (Selectors[i]->MouseHandler(mx, my, Button, State) && flag)
				{
					if (Button == -1)
					{
						if (SHIFT_HELD)
							SelectedID.clear();
						auto it = std::find(SelectedID.begin(), SelectedID.end(), i + CurrentTopLineID);
						if (!(it != SelectedID.end()))
							SelectedID.push_back(i + CurrentTopLineID);
						else
							SelectedID.erase(it);
						if (OnSelect)
							OnSelect(i + CurrentTopLineID);
					}
					if (Button == 1)
					{
						//cout << "PROP\n";
						if (OnGetProperties)
							OnGetProperties(i + CurrentTopLineID);
					}
					flag = 0;
					return 1;
				}
			}
		}
		else
		{
			for (int i = 0; i < Selectors.size(); i++)
				Selectors[i]->MouseHandler(mx, my, 0, 0);
		}
		for (auto ID : SelectedID)
			if (ID < SelectorsText.size())
				if (ID >= CurrentTopLineID && ID < CurrentTopLineID + MaxVisibleLines)
					Selectors[ID - CurrentTopLineID]->MouseHandler(Selectors[ID - CurrentTopLineID]->Xpos, Selectors[ID - CurrentTopLineID]->Ypos, 0, 0);
		return 0;
	}
	void SafeStringReplace(std::string NewString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->SafeStringReplace(NewString, 0xFFFFFFFF);
	}
	void SafeStringReplace(const std::string& NewString, std::uint32_t LineID)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (LineID == 0xFFFFFFFF)
		{
			SafeStringReplace(NewString, SelectedID.front());
		}
		else 
		{
			SelectorsText[LineID] = NewString;
			if (LineID > CurrentTopLineID && LineID - CurrentTopLineID < MaxVisibleLines)
				Selectors[LineID - CurrentTopLineID]->SafeStringReplace(NewString);
		}
	}
	void SafeUpdateLines()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		while (SelectorsText.size() < Selectors.size())
		{
			delete Selectors.back();
			Selectors.pop_back();
		}
		if (CurrentTopLineID + MaxVisibleLines > SelectorsText.size())
		{
			if (SelectorsText.size() >= MaxVisibleLines) CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
			else CurrentTopLineID = 0;
		}
		for (int i = 0; i < Selectors.size(); i++)
		{
			if (i + CurrentTopLineID < SelectorsText.size())
				Selectors[i]->SafeStringReplace(
					(MaxCharsInLine) ?
					(SelectorsText[i + CurrentTopLineID].substr(0, MaxCharsInLine))
					:
					(SelectorsText[i + CurrentTopLineID])
			);
		}
		ResetAlign_All(TextInButtonsAlign);
	}
	bool IsResizeable() override
	{
		return true;
	}
	void SafeResize(float NewHeight, float NewWidth) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float dCX = (NewWidth - Width) * 0.5;
		HeaderCXPos += dCX;
		float OldWidth = Width;
		Width = NewWidth;
		float Lines = std::floor(NewHeight / SpaceBetween);
		int difference = (int)Lines - (int)MaxVisibleLines;

		float YSpace = ButtSettings->STLS->SpaceWidth + 2 * ButtSettings->STLS->XUnitSize;
		int NewMaxCharsInLine = Width / YSpace;
		int ImportantDifference = OldWidth / YSpace - MaxCharsInLine;
		MaxCharsInLine = NewMaxCharsInLine - ImportantDifference;

		if (Selectors.size())
		{
			if (difference < 0)
			{
				while (difference && Selectors.size()) 
				{
					delete Selectors.back();
					Selectors.pop_back();
					if (CurrentTopLineID && Selectors.size() + CurrentTopLineID == SelectorsText.size())
						CurrentTopLineID--;
					difference++;
				}
			}
			else if (difference > 0)
			{
				ButtSettings->Height = SpaceBetween;
				ButtSettings->Width = NewWidth;
				ButtSettings->OnClick = NULL;
				while (difference && Selectors.size() + CurrentTopLineID < SelectorsText.size() + 1) 
				{
					ButtSettings->ChangePosition(HeaderCXPos, HeaderYPos - (Selectors.size() + 0.5f) * SpaceBetween);
					Selectors.push_back(ButtSettings->CreateOne(SelectorsText[Selectors.size() + CurrentTopLineID - 1]));
					difference--;
				}
			}
		}

		for (auto& button : Selectors)
		{
			button->Width = Width;
			button->SafeChangePosition(HeaderCXPos, button->Ypos);
		}
		MaxVisibleLines = Lines;
		SafeUpdateLines();
		RecalculateCurrentHeight();
	}
	void SafeRotateList(std::int32_t Delta)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!MaxVisibleLines)
			return;
		if (Delta < 0 && CurrentTopLineID < 0 - Delta)
			CurrentTopLineID = 0;
		else if (Delta > 0 && CurrentTopLineID + Delta + MaxVisibleLines > SelectorsText.size())
			CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
		else CurrentTopLineID += Delta;
		SafeUpdateLines();
	}
	void SafeRemoveStringByID(std::uint32_t ID)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (ID >= SelectorsText.size()) 
			return;
		if (SelectorsText.empty()) 
			return;
		if (MaxVisibleLines)
		{
			if (ID < CurrentTopLineID)
				CurrentTopLineID--;
			else if (ID == CurrentTopLineID)
				if (CurrentTopLineID == SelectorsText.size() - 1)
					CurrentTopLineID--;
		}
		SelectorsText.erase(SelectorsText.begin() + ID);
		SafeUpdateLines();
		SelectedID.clear();
	}
	void RemoveSelected()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		std::sort(SelectedID.begin(), SelectedID.end());
		while (SelectedID.size())
		{
			if (MaxVisibleLines) {
				if (SelectedID.back() < CurrentTopLineID)
					CurrentTopLineID--;
				else if (SelectedID.back() == CurrentTopLineID)
					if (CurrentTopLineID == SelectorsText.size() - 1)
						CurrentTopLineID--;
			}
			SelectorsText.erase(SelectorsText.begin() + SelectedID.back());
			SelectedID.pop_back();
		}
		SafeUpdateLines();
		SelectedID.clear();
	}
	void ResetAlignFor(std::uint32_t ID, _Align Align)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (ID >= Selectors.size()) 
			return;
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right) ? 0 - 0.5f : 0)) * (Width - SpaceBetween);
		Selectors[ID]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[ID]->Ypos);
	}
	void ResetAlign_All(_Align Align)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Align == _Align::center) 
			return;
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right) ? 0 - 0.5f : 0)) * (Width - SpaceBetween);
		for (int i = 0; i < Selectors.size(); i++)
			Selectors[i]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[i]->Ypos);
	}
	void SafePushBackNewString(std::string ButtonText)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (MaxCharsInLine)ButtonText = ButtonText.substr(0, MaxCharsInLine);
		SelectorsText.push_back(ButtonText);
		if (MaxVisibleLines && SelectorsText.size() > MaxVisibleLines)
		{
			SafeUpdateLines(); 
			return;
		}
		ButtSettings->ChangePosition(HeaderCXPos, HeaderYPos - (SelectorsText.size() - 0.5f) * SpaceBetween);
		ButtSettings->Height = SpaceBetween;
		ButtSettings->Width = Width;
		ButtSettings->OnClick = NULL;
		Button* ptr;
		Selectors.push_back(ptr = ButtSettings->CreateOne(ButtonText));
		RecalculateCurrentHeight();
		ResetAlignFor(SelectorsText.size() - 1, this->TextInButtonsAlign);
	}
	void PushStrings(std::vector<std::string> LStrings)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto Y = LStrings.begin(); Y != LStrings.end(); ++Y)
			SafePushBackNewString(*Y);
	}
	void PushStrings(std::initializer_list<std::string> LStrings)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto Y = LStrings.begin(); Y != LStrings.end(); ++Y)
			SafePushBackNewString(*Y);
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
				) * CalculatedHeight;
		SafeChangePosition(NewX + CW, NewY - 0.5f * CalculatedHeight + CH);
	}
	void KeyboardHandler(char CH) override
	{
		return;
	}
	void SafeChangePosition(float NewCXPos, float NewHeaderYPos) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewCXPos -= HeaderCXPos;
		NewHeaderYPos -= HeaderYPos;
		SafeMove(NewCXPos, NewHeaderYPos);
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		HeaderCXPos += dx;
		HeaderYPos += dy;
		for (auto Y = Selectors.begin(); Y != Selectors.end(); ++Y)
			(*Y)->SafeMove(dx, dy);
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Selectors.size() < SelectorsText.size())
		{
			///TOP BAR
			if (TopArrowHovered)
				__glcolor(ButtSettings->HoveredRGBABorder);
			else 
				__glcolor(ButtSettings->RGBABorder);
			glBegin(GL_QUADS);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos + ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos + ARROW_STICK_HEIGHT);
			///BOTTOM BAR
			if (BottomArrowHovered)
				__glcolor(ButtSettings->HoveredRGBABorder);
			else 
				__glcolor(ButtSettings->RGBABorder);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glEnd();
			///TOP ARROW
			if (TopArrowHovered)
			{
				if (ButtSettings->HoveredRGBAColor & 0xFF)
					__glcolor(ButtSettings->HoveredRGBAColor);
				else 
					__glcolor(ButtSettings->RGBAColor);
			}
			else 
				__glcolor(ButtSettings->RGBAColor);
			glBegin(GL_TRIANGLES);
			glVertex2f(HeaderCXPos, HeaderYPos + 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			///BOTTOM ARROW
			if (BottomArrowHovered)
			{ 
				if (ButtSettings->HoveredRGBAColor & 0xFF)
					__glcolor(ButtSettings->HoveredRGBAColor);
				else 
					__glcolor(ButtSettings->RGBAColor);
			}
			else 
				__glcolor(ButtSettings->RGBAColor);
			glVertex2f(HeaderCXPos, HeaderYPos - CalculatedHeight - 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glEnd();
		}
		for (auto Y = Selectors.begin(); Y != Selectors.end(); ++Y)
			(*Y)->Draw();
	}
	inline std::uint32_t TellType() override
	{
		return TT_SELPROPLIST;
	}
};
#endif // !SAFGUIF_SNPLIST
