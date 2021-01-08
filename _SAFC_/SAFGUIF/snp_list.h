#pragma once
#ifndef SAFGUIF_SNPLIST
#define SAFGUIF_SNPLIST
#include <deque>
#include <algorithm>

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "button_settings.h"

#define ARROW_STICK_HEIGHT 10
struct SelectablePropertedList : HandleableUIPart {
	_Align TextInButtonsAlign;
	void(*OnSelect)(int ID);
	void(*OnGetProperties)(int ID);
	float HeaderCXPos, HeaderYPos, CalculatedHeight, SpaceBetween, Width;
	ButtonSettings* ButtSettings;
	std::deque<std::string> SelectorsText;
	std::deque<Button*> Selectors;
	std::deque<DWORD> SelectedID;
	DWORD MaxVisibleLines, CurrentTopLineID, MaxCharsInLine;
	BYTE TopArrowHovered, BottomArrowHovered;
	~SelectablePropertedList() {
		Lock.lock();
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++)
			delete (*Y);
		Lock.unlock();
	}
	SelectablePropertedList(ButtonSettings* ButtSettings, void(*OnSelect)(int SelectedID), void(*OnGetProperties)(int ID), float HeaderCXPos, float HeaderYPos, float Width, float SpaceBetween, DWORD MaxCharsInLine = 0, DWORD MaxVisibleLines = 0, _Align TextInButtonsAlign = _Align::left) {
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
		//SelectedID = 0xFFFFFFFF;
	}
	void RecalculateCurrentHeight() {
		Lock.lock();
		CalculatedHeight = SpaceBetween * Selectors.size();
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		Lock.lock();
		TopArrowHovered = BottomArrowHovered = 0;
		if (fabsf(mx - HeaderCXPos) < 0.5 * Width && my < HeaderYPos && my > HeaderYPos - CalculatedHeight) {
			if (Button == 2 /*UP*/) {
				if (State == -1) {
					SafeRotateList(-3);
				}
			}
			else if (Button == 3 /*DOWN*/) {
				if (State == -1) {
					SafeRotateList(3);
				}
			}
		}
		if (MaxVisibleLines && Selectors.size() < SelectorsText.size()) {
			if (fabsf(mx - HeaderCXPos) < 0.5 * Width) {
				if (my > HeaderYPos && my < HeaderYPos + ARROW_STICK_HEIGHT) {
					TopArrowHovered = 1;
					if (Button == -1 && State == -1) {
						SafeRotateList(-1);
						Lock.unlock();
						return 1;
					}
				}
				else if (my < HeaderYPos - CalculatedHeight && my > HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT) {
					BottomArrowHovered = 1;
					if (Button == -1 && State == -1) {
						SafeRotateList(1);
						Lock.unlock();
						return 1;
					}
				}
			}
		}
		if (Button) {
			BIT flag = 1;
			for (int i = 0; i < Selectors.size(); i++) {
				if (Selectors[i]->MouseHandler(mx, my, Button, State) && flag) {
					if (Button == -1) {
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
					if (Button == 1) {
						//cout << "PROP\n";
						if (OnGetProperties)
							OnGetProperties(i + CurrentTopLineID);
					}
					flag = 0;
					Lock.unlock();
					return 1;
				}
			}
		}
		else {
			for (int i = 0; i < Selectors.size(); i++)
				Selectors[i]->MouseHandler(mx, my, 0, 0);
		}
		for (auto ID : SelectedID)
			if (ID < SelectorsText.size())
				if (ID >= CurrentTopLineID && ID < CurrentTopLineID + MaxVisibleLines)
					Selectors[ID - CurrentTopLineID]->MouseHandler(Selectors[ID - CurrentTopLineID]->Xpos, Selectors[ID - CurrentTopLineID]->Ypos, 0, 0);
		Lock.unlock();
		return 0;
	}
	void SafeStringReplace(std::string NewString) override {
		Lock.lock();
		this->SafeStringReplace(NewString, 0xFFFFFFFF);
		Lock.unlock();
	}
	void SafeStringReplace(std::string NewString, DWORD LineID) {
		Lock.lock();
		if (LineID == 0xFFFFFFFF) {
			SafeStringReplace(NewString, SelectedID.front());
		}
		else {
			SelectorsText[LineID] = NewString;
			if (LineID > CurrentTopLineID && LineID - CurrentTopLineID < MaxVisibleLines)
				Selectors[LineID - CurrentTopLineID]->SafeStringReplace(NewString);
		}
		Lock.unlock();
	}
	void SafeUpdateLines() {
		Lock.lock();
		while (SelectorsText.size() < Selectors.size()) {
			delete Selectors.back();
			Selectors.pop_back();
		}
		if (CurrentTopLineID + MaxVisibleLines > SelectorsText.size()) {
			if (SelectorsText.size() >= MaxVisibleLines) CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
			else CurrentTopLineID = 0;
		}
		for (int i = 0; i < Selectors.size(); i++) {
			if (i + CurrentTopLineID < SelectorsText.size())
				Selectors[i]->SafeStringReplace(
					(MaxCharsInLine) ?
					(SelectorsText[i + CurrentTopLineID].substr(0, MaxCharsInLine))
					:
					(SelectorsText[i + CurrentTopLineID])
			);
		}
		ReSetAlign_All(TextInButtonsAlign);
		Lock.unlock();
	}
	bool IsResizeable() override {
		return true;
	}
	void SafeResize(float NewHeight, float NewWidth) override {
		Lock.lock();
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

		if (Selectors.size()) {
			if (difference < 0) {
				while (difference && Selectors.size()) {
					delete Selectors.back();
					Selectors.pop_back();
					if (CurrentTopLineID && Selectors.size() + CurrentTopLineID == SelectorsText.size())
						CurrentTopLineID--;
					difference++;
				}
			}
			else if (difference > 0) {
				ButtSettings->Height = SpaceBetween;
				ButtSettings->Width = NewWidth;
				ButtSettings->OnClick = NULL;
				while (difference && Selectors.size() + CurrentTopLineID < SelectorsText.size() + 1) {
					ButtSettings->ChangePosition(HeaderCXPos, HeaderYPos - (Selectors.size() + 0.5f) * SpaceBetween);
					Selectors.push_back(ButtSettings->CreateOne(SelectorsText[Selectors.size() + CurrentTopLineID - 1]));
					difference--;
				}
			}
		}

		for (auto& button : Selectors) {
			button->Width = Width;
			button->SafeChangePosition(HeaderCXPos, button->Ypos);
		}
		MaxVisibleLines = Lines;
		SafeUpdateLines();
		RecalculateCurrentHeight();
		Lock.unlock();
	}
	void SafeRotateList(INT32 Delta) {
		Lock.lock();
		if (!MaxVisibleLines) {
			Lock.unlock(); return;
		}
		if (Delta < 0 && CurrentTopLineID < 0 - Delta)CurrentTopLineID = 0;
		else if (Delta > 0 && CurrentTopLineID + Delta + MaxVisibleLines > SelectorsText.size())
			CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
		else CurrentTopLineID += Delta;
		SafeUpdateLines();
		Lock.unlock();
	}
	void SafeRemoveStringByID(DWORD ID) {
		Lock.lock();
		if (ID >= SelectorsText.size()) {
			Lock.unlock(); return;
		}
		if (SelectorsText.empty()) {
			Lock.unlock(); return;
		}
		if (MaxVisibleLines) {
			if (ID < CurrentTopLineID) {
				CurrentTopLineID--;
			}
			else if (ID == CurrentTopLineID) {
				if (CurrentTopLineID == SelectorsText.size() - 1)CurrentTopLineID--;
			}
		}
		SelectorsText.erase(SelectorsText.begin() + ID);
		SafeUpdateLines();
		SelectedID.clear();
		Lock.unlock();
	}
	void RemoveSelected() {
		Lock.lock();
		std::sort(SelectedID.begin(), SelectedID.end());
		while (SelectedID.size()) {
			if (MaxVisibleLines) {
				if (SelectedID.back() < CurrentTopLineID) {
					CurrentTopLineID--;
				}
				else if (SelectedID.back() == CurrentTopLineID) {
					if (CurrentTopLineID == SelectorsText.size() - 1)CurrentTopLineID--;
				}
			}
			SelectorsText.erase(SelectorsText.begin() + SelectedID.back());
			SelectedID.pop_back();
		}
		SafeUpdateLines();
		SelectedID.clear();
		Lock.unlock();
	}
	void ReSetAlignFor(DWORD ID, _Align Align) {
		Lock.lock();
		if (ID >= Selectors.size()) {
			Lock.unlock(); return;
		}
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right) ? 0 - 0.5f : 0)) * (Width - SpaceBetween);
		Selectors[ID]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[ID]->Ypos);
		Lock.unlock();
	}
	void ReSetAlign_All(_Align Align) {
		Lock.lock();
		if (!Align) {
			Lock.unlock(); return;
		}
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right) ? 0 - 0.5f : 0)) * (Width - SpaceBetween);
		for (int i = 0; i < Selectors.size(); i++)
			Selectors[i]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[i]->Ypos);
		Lock.unlock();
	}
	void SafePushBackNewString(std::string ButtonText) {
		Lock.lock();
		if (MaxCharsInLine)ButtonText = ButtonText.substr(0, MaxCharsInLine);
		SelectorsText.push_back(ButtonText);
		if (MaxVisibleLines && SelectorsText.size() > MaxVisibleLines) {
			SafeUpdateLines(); 
			Lock.unlock(); 
			return;
		}
		ButtSettings->ChangePosition(HeaderCXPos, HeaderYPos - (SelectorsText.size() - 0.5f) * SpaceBetween);
		ButtSettings->Height = SpaceBetween;
		ButtSettings->Width = Width;
		ButtSettings->OnClick = NULL;
		Button* ptr;
		Selectors.push_back(ptr = ButtSettings->CreateOne(ButtonText));
		//free(ptr);
		RecalculateCurrentHeight();
		ReSetAlignFor(SelectorsText.size() - 1, this->TextInButtonsAlign);
		Lock.unlock();
	}
	void PushStrings(std::list<std::string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
		Lock.unlock();
	}
	void PushStrings(std::vector<std::string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
		Lock.unlock();
	}
	void PushStrings(std::initializer_list<std::string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
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
				) * CalculatedHeight;
		SafeChangePosition(NewX + CW, NewY - 0.5f * CalculatedHeight + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeChangePosition(float NewCXPos, float NewHeaderYPos) override {
		Lock.lock();
		NewCXPos -= HeaderCXPos;
		NewHeaderYPos -= HeaderYPos;
		SafeMove(NewCXPos, NewHeaderYPos);
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		HeaderCXPos += dx;
		HeaderYPos += dy;
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++)
			(*Y)->SafeMove(dx, dy);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		if (Selectors.size() < SelectorsText.size()) {
			///TOP BAR
			if (TopArrowHovered)GLCOLOR(ButtSettings->HoveredRGBABorder);
			else GLCOLOR(ButtSettings->RGBABorder);
			glBegin(GL_QUADS);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos + ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos + ARROW_STICK_HEIGHT);
			///BOTTOM BAR
			if (BottomArrowHovered)GLCOLOR(ButtSettings->HoveredRGBABorder);
			else GLCOLOR(ButtSettings->RGBABorder);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f * Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f * Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glEnd();
			///TOP ARROW
			if (TopArrowHovered)
				if (ButtSettings->HoveredRGBAColor & 0xFF)GLCOLOR(ButtSettings->HoveredRGBAColor);
				else GLCOLOR(ButtSettings->RGBAColor);
			else GLCOLOR(ButtSettings->RGBAColor);
			glBegin(GL_TRIANGLES);
			glVertex2f(HeaderCXPos, HeaderYPos + 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			///BOTTOM ARROW
			if (BottomArrowHovered)
				if (ButtSettings->HoveredRGBAColor & 0xFF)GLCOLOR(ButtSettings->HoveredRGBAColor);
				else GLCOLOR(ButtSettings->RGBAColor);
			else GLCOLOR(ButtSettings->RGBAColor);
			glVertex2f(HeaderCXPos, HeaderYPos - CalculatedHeight - 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glEnd();
		}
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++)
			(*Y)->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_SELPROPLIST;
	}
};
#endif // !SAFGUIF_SNPLIST
