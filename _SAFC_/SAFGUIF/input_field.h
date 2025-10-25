#pragma once
#ifndef SAFGUIF_IF
#define SAFGUIF_IF

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct InputField : HandleableUIPart
{
	enum PassCharsType
	{
		PassNumbers = 0b1,
		PassFirstPoint = 0b10,
		PassFrontMinusSign = 0b100,
		PassAll = 0xFF
	};

	enum Type
	{
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
	std::uint32_t MaxChars, BorderRGBAColor;
	float Xpos, Ypos, Height, Width;
	bool Focused, FirstInput;
	SingleTextLine* STL;
	SingleTextLine* Tip;

	InputField(std::string DefaultString, float Xpos, float Ypos, float Height, float Width, SingleTextLineSettings* DefaultStringSettings, std::string* OutputSource, std::uint32_t BorderRGBAColor, SingleTextLineSettings* TipLineSettings = NULL, std::string TipLineText = " ", std::uint32_t MaxChars = 0, _Align InputAlign = _Align::left, _Align TipAlign = _Align::center, Type InputType = Type::Text)
	{
		this->DefaultString = DefaultString;
		DefaultStringSettings->SetNewPos(Xpos, Ypos);
		this->STL = DefaultStringSettings->CreateOne(DefaultString);
		if (TipLineSettings)
		{
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
		this->CurrentString.clear();//DefaultString;
		this->FirstInput = this->Focused = false;
		this->OutputSource = OutputSource;
	}

	~InputField() override
	{
		delete STL;
		delete Tip;
	}

	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override
	{
		if (!Enabled)
			return false;

		if (abs(mx - Xpos) < 0.5 * Width && abs(my - Ypos) < 0.5 * Height)
		{
			if (!Focused)
				FocusChange();
			
			return Button;
		}
		else
		{
			if (Focused)
				FocusChange();
			return 0;
		}
	}

	inline static bool CheckStringOnType(const std::string& String, InputField::Type CheckType) 
	{
		bool first_symb = true;
		bool met_minus = false;
		bool met_point = false;
		for (const auto& ch : String) 
		{
			switch (CheckType) 
			{
			case InputField::NaturalNumbers:
				if (!(ch >= '0' && ch <= '9'))
					return false;
				break;
			case InputField::WholeNumbers:
				if (!((ch >= '0' && ch <= '9')))
				{
					if (first_symb && !met_minus && ch == '-')
						met_minus = true;
					else
						return false;
				}
				break;
			case InputField::FP_PositiveNumbers:
				if (!((ch >= '0' && ch <= '9')))
				{
					if (!met_point && ch == '.')
						met_point = true;
					else
						return false;
				}
				break;
			case InputField::FP_Any:
				if (!((ch >= '0' && ch <= '9')))
				{
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

	void SafeMove(float dx, float dy) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		STL->SafeMove(dx, dy);
		if (Tip)Tip->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
	}

	void SafeChangePosition(float NewX, float NewY)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX = Xpos - NewX;
		NewY = Ypos - NewY;
		SafeMove(NewX, NewY);
	}

	void FocusChange() 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
	}

	void UpdateInputString(const std::string& NewString = "")
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (NewString.size())
			CurrentString.clear();
		float x = Xpos - ((InputAlign == _Align::left) ? 1 : ((InputAlign == _Align::right) ? -1 : 0)) * (0.5f * Width - STL->_XUnitSize);
		this->STL->SafeStringReplace((NewString.size()) ? NewString.substr(0, this->MaxChars) : CurrentString);
		this->STL->SafeChangePosition_Argumented(InputAlign, x, Ypos);
	}

	void BackSpace()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		ProcessFirstInput();
		if (CurrentString.size()) 
		{
			CurrentString.pop_back();
			UpdateInputString();
		}
		else 
			this->STL->SafeStringReplace(" ");
	}

	void FlushCurrentStringWithoutGUIUpdate(bool SetDefault = false)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->CurrentString = (SetDefault) ? this->DefaultString : "";
	}

	void PutIntoSource(std::string* AnotherSource = NULL)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (OutputSource) 
		{
			if (CurrentString.size())
				*OutputSource = CurrentString;
		}
		else if (AnotherSource)
			if (CurrentString.size())
				*AnotherSource = CurrentString;
	}

	void ProcessFirstInput() 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (FirstInput) 
		{
			FirstInput = false;
			CurrentString.clear();
		}
	}

	std::string GetCurrentInput(const std::string& Replacement)
	{
		if (InputField::CheckStringOnType(CurrentString, InputType))
			return CurrentString;
		if (STL && InputField::CheckStringOnType(STL->_CurrentText, InputType))
			return STL->_CurrentText;
		if (InputField::CheckStringOnType(Replacement, InputType))
			return Replacement;
		else
			return "0";
	}

	void KeyboardHandler(char CH) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		if (!Enabled)
			return;

		if (!Focused)
			return;

		if (CH >= 32)
		{
			if (InputType & PassCharsType::PassNumbers)
			{
				if (CH >= '0' && CH <= '9')
				{
					Input(CH);
					return;
				}
			}
			if (InputType & PassCharsType::PassFrontMinusSign)
			{
				if (CH == '-' && CurrentString.empty())
				{
					Input(CH);
					return;
				}
			}
			if (InputType & PassCharsType::PassFirstPoint)
			{
				if (CH == '.' && CurrentString.find('.') >= CurrentString.size())
				{
					Input(CH);
					return;
				}
			}

			if ((InputType & PassCharsType::PassAll) == PassCharsType::PassAll)
				Input(CH);
		}
		else if (CH == 13)
			PutIntoSource();
		else if (CH == 8)
			BackSpace();
	}

	void Input(char CH) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		ProcessFirstInput();
		if (!MaxChars || CurrentString.size() < MaxChars)
		{
			CurrentString.push_back(CH);
			UpdateInputString();
		}
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

	void SafeStringReplace(std::string NewString) override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		CurrentString = NewString.substr(0, this->MaxChars);
		UpdateInputString(NewString);
		FirstInput = true;
	}

	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		if (!Enabled)
			return;

		__glcolor(BorderRGBAColor);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos + 0.5f * Width, Ypos + 0.5f * Height);
		glVertex2f(Xpos - 0.5f * Width, Ypos + 0.5f * Height);
		glVertex2f(Xpos - 0.5f * Width, Ypos - 0.5f * Height);
		glVertex2f(Xpos + 0.5f * Width, Ypos - 0.5f * Height);
		glEnd();
		this->STL->Draw();
		if (Focused && Tip)
			Tip->Draw();
	}

	inline std::uint32_t TellType() override
	{
		return TT_INPUT_FIELD;
	}
};

#endif // !SAFGUIF_IF 
