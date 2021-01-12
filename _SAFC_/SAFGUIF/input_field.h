#pragma once
#ifndef SAFGUIF_IF
#define SAFGUIF_IF

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct InputField : HandleableUIPart {
	enum PassCharsType {
		PassNumbers = 0b1,
		PassFirstPoint = 0b10,
		PassFrontMinusSign = 0b100,
		PassAll = 0xFF
	};
	enum Type {
		NaturalNumbers = PassCharsType::PassNumbers,
		WholeNumbers = PassCharsType::PassNumbers | PassCharsType::PassFrontMinusSign,
		FP_PositiveNumbers = PassCharsType::PassNumbers | PassCharsType::PassFirstPoint,
		FP_Any = PassCharsType::PassNumbers | PassCharsType::PassFirstPoint | PassCharsType::PassFrontMinusSign,
		Text = PassCharsType::PassAll,
	};
	_Align InputAlign, TipAlign;
	Type InputType;
	std::string CurrentString, DefaultString;
	std::string* OutputSource;
	DWORD MaxChars, BorderRGBAColor;
	float Xpos, Ypos, Height, Width;
	BIT Focused, FirstInput;
	SingleTextLine* STL;
	SingleTextLine* Tip;
	InputField(std::string DefaultString, float Xpos, float Ypos, float Height, float Width, SingleTextLineSettings* DefaultStringSettings, std::string* OutputSource, DWORD BorderRGBAColor, SingleTextLineSettings* TipLineSettings = NULL, std::string TipLineText = " ", DWORD MaxChars = 0, _Align InputAlign = _Align::left, _Align TipAlign = _Align::center, Type InputType = Type::Text) {
		this->DefaultString = DefaultString;
		DefaultStringSettings->SetNewPos(Xpos, Ypos);
		this->STL = DefaultStringSettings->CreateOne(DefaultString);
		if (TipLineSettings) {
			this->Tip = TipLineSettings->CreateOne(TipLineText);
			this->Tip->SafeChangePosition_Argumented(TipAlign, Xpos - ((TipAlign == _Align::left) ? 0.5f : ((TipAlign == _Align::right) ? -0.5f : 0)) * Width, Ypos - Height);
		}
		else this->Tip = NULL;
		this->InputAlign = InputAlign;
		this->InputType = InputType;
		this->TipAlign = TipAlign;

		this->MaxChars = MaxChars;
		this->BorderRGBAColor = BorderRGBAColor;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Height = (DefaultStringSettings->YUnitSize * 2 > Height) ? DefaultStringSettings->YUnitSize * 2 : Height;
		this->Width = Width;
		this->CurrentString = "";//DefaultString;
		this->FirstInput = this->Focused = false;
		this->OutputSource = OutputSource;
	}
	~InputField() override {
		delete STL;
		delete Tip;
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override {
		if (abs(mx - Xpos) < 0.5 * Width && abs(my - Ypos) < 0.5 * Height) {
			if (!Focused)
				FocusChange();
			if (Button)return 1;
			else return 0;
		}
		else {
			if (Focused)
				FocusChange();
			return 0;
		}
	}
	inline static bool CheckStringOnType(const std::string& String, InputField::Type CheckType) {
		bool first_symb = true;
		bool met_minus = false;
		bool met_point = false;
		for (const auto& ch : String) {
			switch (CheckType) {
			case InputField::NaturalNumbers:
				if (!(ch >= '0' && ch <= '9'))
					return false;
				break;
			case InputField::WholeNumbers:
				if (!((ch >= '0' && ch <= '9'))) {
					if (first_symb && !met_minus && ch == '-')
						met_minus = true;
					else
						return false;
				}
				break;
			case InputField::FP_PositiveNumbers:
				if (!((ch >= '0' && ch <= '9'))) {
					if (!met_point && ch == '.')
						met_point = true;
					else
						return false;
				}
				break;
			case InputField::FP_Any:
				if (!((ch >= '0' && ch <= '9'))) {
					if (!met_point && ch == '.')
						met_point = true;
					else if (first_symb && !met_minus && ch == '-')
						met_minus = true;
					else
						return false;
				}
				break;
			case InputField::Text:
				break;
			default:
				break;
			}
			first_symb = false;
		}
		return !first_symb;
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		STL->SafeMove(dx, dy);
		if (Tip)Tip->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) {
		Lock.lock();
		NewX = Xpos - NewX;
		NewY = Ypos - NewY;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void FocusChange() {
		Lock.lock();
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
		Lock.unlock();
	}
	void UpdateInputString(std::string NewString = "") {
		Lock.lock();
		if (NewString.size())
			CurrentString = "";
		float x = Xpos - ((InputAlign == _Align::left) ? 1 : ((InputAlign == _Align::right) ? -1 : 0)) * (0.5f * Width - STL->_XUnitSize);
		this->STL->SafeStringReplace((NewString.size()) ? NewString.substr(0, this->MaxChars) : CurrentString);
		this->STL->SafeChangePosition_Argumented(InputAlign, x, Ypos);
		Lock.unlock();
	}
	void BackSpace() {
		Lock.lock();
		ProcessFirstInput();
		if (CurrentString.size()) {
			CurrentString.pop_back();
			UpdateInputString();
		}
		else {
			this->STL->SafeStringReplace(" ");
		}
		Lock.unlock();
	}
	void FlushCurrentStringWithoutGUIUpdate(BIT SetDefault = false) {
		Lock.lock();
		this->CurrentString = (SetDefault) ? this->DefaultString : "";
		Lock.unlock();
	}
	void PutIntoSource(std::string* AnotherSource = NULL) {
		Lock.lock();
		if (OutputSource) {
			if (CurrentString.size())
				*OutputSource = CurrentString;
		}
		else if (AnotherSource)
			if (CurrentString.size())
				*AnotherSource = CurrentString;
		Lock.unlock();
	}
	void ProcessFirstInput() {
		Lock.lock();
		if (FirstInput) {
			FirstInput = 0;
			CurrentString = "";
		}
		Lock.unlock();
	}
	std::string GetCurrentInput(std::string Replacement) {
		if (InputField::CheckStringOnType(CurrentString, InputType))
			return CurrentString;
		if (STL && InputField::CheckStringOnType(STL->_CurrentText, InputType))
			return STL->_CurrentText;
		if (InputField::CheckStringOnType(Replacement, InputType))
			return Replacement;
		else
			return "0";
	}
	void KeyboardHandler(char CH) {
		Lock.lock();
		if (Focused) {
			if (CH >= 32) {
				if (InputType & PassCharsType::PassNumbers) {
					if (CH >= '0' && CH <= '9') {
						Input(CH);
						Lock.unlock();
						return;
					}
				}
				if (InputType & PassCharsType::PassFrontMinusSign) {
					if (CH == '-' && CurrentString.empty()) {
						Input(CH);
						Lock.unlock();
						return;
					}
				}
				if (InputType & PassCharsType::PassFirstPoint) {
					if (CH == '.' && CurrentString.find('.') >= CurrentString.size()) {
						Input(CH);
						Lock.unlock();
						return;
					}
				}

				if (InputType == PassCharsType::PassAll)Input(CH);
			}
			else if (CH == 13)PutIntoSource();
			else if (CH == 8)BackSpace();
		}
		Lock.unlock();
	}
	void Input(char CH) {
		Lock.lock();
		ProcessFirstInput();
		if (!MaxChars || CurrentString.size() < MaxChars) {
			CurrentString.push_back(CH);
			UpdateInputString();
		}
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
	void SafeStringReplace(std::string NewString) override {
		Lock.lock();
		CurrentString = NewString.substr(0, this->MaxChars);
		UpdateInputString(NewString);
		FirstInput = true;
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		GLCOLOR(BorderRGBAColor);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos + 0.5f * Width, Ypos + 0.5f * Height);
		glVertex2f(Xpos - 0.5f * Width, Ypos + 0.5f * Height);
		glVertex2f(Xpos - 0.5f * Width, Ypos - 0.5f * Height);
		glVertex2f(Xpos + 0.5f * Width, Ypos - 0.5f * Height);
		glEnd();
		this->STL->Draw();
		if (Focused && Tip)Tip->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_INPUT_FIELD;
	}
};

#endif // !SAFGUIF_IF 
