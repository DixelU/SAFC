#pragma once
#ifndef SAFGUIF_WH
#define SAFGUIF_WH

#include "handleable_ui_part.h"
#include "movable_resizeable_window.h"
#include "textbox.h"
#include "special_signs.h"
#include "input_field.h"
#include "button_settings.h"
#include "stls_presets.h"


struct WindowsHandler {
	std::map<std::string, MoveableWindow*> Map;
#define Map(WindowName,ElementName) (*Map[WindowName])[ElementName]
	std::list<std::map<std::string, MoveableWindow*>::iterator> ActiveWindows;
	std::string MainWindow_ID, MW_ID_Holder;
	BIT WindowWasDisabledDuringMouseHandling;
	std::recursive_mutex locker;
	static constexpr float alerttext_vert_pos = -7.5, alertheight = 65;
	WindowsHandler() {
		MW_ID_Holder = "";
		MainWindow_ID = "MAIN";
		WindowWasDisabledDuringMouseHandling = 0;
		MoveableWindow* ptr;
		Map["ALERT"] = ptr = new MoveableWindow("Alert window", System_White, -100, alertheight / 2, 200, alertheight, 0x3F3F3FCF, 0x7F7F7F7F);
		(*ptr)["AlertText"] = new TextBox("_", System_White, 20, alerttext_vert_pos, alertheight - 12.5, 155, 7.5, 0, 0, 0, _Align::left, TextBox::VerticalOverflow::recalibrate);
		(*ptr)["AlertSign"] = new SpecialSignHandler(SpecialSigns::DrawACircle, -78.5, -17, 12, 0x000000FF, 0x001FFFFF);

		Map["PROMPT"] = ptr = new MoveableWindow("prompt", System_White, -50, 50, 100, 100, 0x3F3F3FCF, 0x7F7F7F7F);
		(*ptr)["FLD"] = new InputField("", 0, 35 - WindowHeapSize, 10, 80, System_White, NULL, 0x007FFFFF, NULL, "", 0, _Align::center);
		(*ptr)["TXT"] = new TextBox("_abc_", System_White, 0, 7.5 - WindowHeapSize, 10, 80, 7.5, 0, 0, 2, _Align::center, TextBox::VerticalOverflow::recalibrate);
		(*ptr)["BUTT"] = new Button("Submit", System_White, NULL, -0, -20 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xFF7F00FF, NULL, " ");
	}
	~WindowsHandler() {
		for (auto& [_, ptr] : Map) {
			delete ptr;
			ptr = nullptr;
		}
	}
	void MouseHandler(float mx, float my, CHAR Button, CHAR State) {
		locker.lock();
		//printf("%X\n", Button);
		std::list<std::map<std::string, MoveableWindow*>::iterator>::iterator AWIterator = ActiveWindows.begin(), CurrentAW;
		CurrentAW = AWIterator;
		//BIT flag = 0;
		if (!Button && !ActiveWindows.empty())(*ActiveWindows.begin())->second->MouseHandler(mx, my, 0, 0);
		else {
			while (AWIterator != ActiveWindows.end() && !((*AWIterator)->second->MouseHandler(mx, my, Button, State)) && !WindowWasDisabledDuringMouseHandling)
				AWIterator++;
			if (!WindowWasDisabledDuringMouseHandling && ActiveWindows.size() > 1 && AWIterator != ActiveWindows.end() && AWIterator != ActiveWindows.begin())
				if (CurrentAW == ActiveWindows.begin())EnableWindow(*AWIterator);
			if (WindowWasDisabledDuringMouseHandling)
				WindowWasDisabledDuringMouseHandling = 0;
		}
		locker.unlock();
	}
	void ThrowPrompt(std::string StaticTipText, std::string WindowTitle, void(*OnSubmit)(), _Align STipAlign, InputField::Type InputType, std::string DefaultString = "", DWORD MaxChars = 0) {
		locker.lock();
		auto wptr = Map["PROMPT"];
		auto ifptr = ((InputField*)(*wptr)["FLD"]);
		auto tbptr = ((TextBox*)(*wptr)["TXT"]);
		wptr->SafeWindowRename(WindowTitle);
		ifptr->InputType = InputType;
		ifptr->MaxChars = MaxChars;
		ifptr->UpdateInputString(DefaultString);
		tbptr->TextAlign = STipAlign;
		tbptr->SafeStringReplace(StaticTipText);
		((Button*)(*wptr)["BUTT"])->OnClick = OnSubmit;

		wptr->SafeChangePosition(-50, 50);
		EnableWindow("PROMPT");
		locker.unlock();
	}
	void ThrowAlert(std::string AlertText, std::string AlertHeader, void(*SpecialSignsDrawFunc)(float, float, float, DWORD, DWORD), BIT Update = false, DWORD FRGBA = 0, DWORD SRGBA = 0) {
		locker.lock();
		auto AlertWptr = Map["ALERT"];
		AlertWptr->SafeWindowRename(AlertHeader);
		AlertWptr->SafeChangePosition_Argumented(0, 0, 0);
		AlertWptr->_NotSafeResize_Centered(alertheight, 200);
		TextBox* AlertWTptr = (TextBox*)((*AlertWptr)["AlertText"]);
		AlertWTptr->SafeStringReplace(AlertText);
		/*
			if (AlertWTptr->CalculatedTextHeight > AlertWTptr->Height) {
				AlertWptr->_NotSafeResize_Centered(AlertWTptr->CalculatedTextHeight + WindowHeapSize, AlertWptr->Width);
			}
		*/
		auto AlertWSptr = ((SpecialSignHandler*)(*AlertWptr)["AlertSign"]);
		AlertWSptr->_ReplaceVoidFunc(SpecialSignsDrawFunc);
		if (Update) {
			AlertWSptr->FRGBA = FRGBA;
			AlertWSptr->SRGBA = SRGBA;
		}
		EnableWindow("ALERT");
		locker.unlock();
	}
	void DisableWindow(std::string ID) {
		locker.lock();
		auto Y = Map.find(ID);
		if (Y != Map.end()) {
			WindowWasDisabledDuringMouseHandling = 1;
			Y->second->Drawable = 1;
			ActiveWindows.remove(Y);
		}
		locker.unlock();
	}
	void DisableAllWindows() {
		locker.lock();
		WindowWasDisabledDuringMouseHandling = 1;
		ActiveWindows.clear();
		EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	void TurnOnMainWindow() {
		locker.lock();
		if (this->MW_ID_Holder != "")
			swap(this->MainWindow_ID, this->MW_ID_Holder);
		this->EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	void TurnOffMainWindow() {
		locker.lock();
		if (this->MW_ID_Holder == "")
			swap(this->MainWindow_ID, this->MW_ID_Holder);
		this->DisableWindow(MainWindow_ID);
		locker.unlock();
	}
	void DisableWindow(std::list<std::map<std::string, MoveableWindow*>::iterator>::iterator Window) {
		locker.lock();
		if (Window == ActiveWindows.end()) {
			locker.lock();
			return;
		}
		WindowWasDisabledDuringMouseHandling = 1;
		(*Window)->second->Drawable = 1;
		ActiveWindows.erase(Window);
		locker.unlock();
	}
	void EnableWindow(std::map<std::string, MoveableWindow*>::iterator Window) {
		locker.lock();
		if (Window == Map.end()) {
			locker.lock();
			return;
		}
		for (auto Y = ActiveWindows.begin(), Q = ActiveWindows.begin(); Y != ActiveWindows.end(); Y++) {
			if (*Y == Window) {
				Window->second->Drawable = 1;
				if (Y != ActiveWindows.begin()) {
					Q = Y;
					Q--;
					ActiveWindows.erase(Y);
					Y = Q;
				}
				else {
					ActiveWindows.erase(Y);
					if (ActiveWindows.size())Y = ActiveWindows.begin();
					else break;
				}
			}
		}

		ActiveWindows.push_front(Window);
		locker.unlock();
		//cout << Window->first << " " << ActiveWindows.front()->first << endl;
	}
	void EnableWindow(std::string ID) {
		locker.lock();
		this->EnableWindow(Map.find(ID));
		locker.unlock();
	}
	void KeyboardHandler(char CH) {
		locker.lock();
		if (ActiveWindows.size())
			(*(ActiveWindows.begin()))->second->KeyboardHandler(CH);
		locker.unlock();
	}
	void Draw() {
		BIT MetMain = 0;
		locker.lock();
		if (!ActiveWindows.empty()) {//if only reverse iterators could be easily converted to usual iterators...
			auto Y = (++ActiveWindows.rbegin()).base();

			while (true) {
				//cout << ((*Y)->first) << endl;
				if (!((*Y)->second->Drawable)) {
					if ((*Y)->first != MainWindow_ID)
						if (Y == ActiveWindows.begin()) {
							DisableWindow(Y);
							break;
						}
						else DisableWindow(Y);
					else (*Y)->second->Drawable = true;
					continue;
				}
				(*Y)->second->Draw();
				if ((*Y)->first == MainWindow_ID)MetMain = 1;
				if (Y == ActiveWindows.begin())break;
				Y--;
			}
		}
		//cout << endl;
		if (!MetMain)this->EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	inline MoveableWindow*& operator[](std::string ID) {
		return (this->Map[ID]);
	}
};
WindowsHandler* WH;

#endif // !SAFGUIF_WH
