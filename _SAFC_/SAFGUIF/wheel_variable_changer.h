#pragma once
#ifndef SAFGUIF_WVC
#define SAFGUIF_WVC

#include "header_utils.h"
#include "input_field.h"

struct WheelVariableChanger :HandleableUIPart {
	enum class Type { exponential, addictable };
	enum class Sensitivity { on_enter, on_click, on_wheel };
	Type type;
	Sensitivity Sen;
	InputField* var_if, * fac_if;
	float Width, Height;
	float Xpos, Ypos;
	std::string var_s, fact_s;
	double variable;
	double factor;
	bool IsHovered, WheelFieldHovered;
	void(*OnApply)(double);
	~WheelVariableChanger() override {
		if (var_if)
			delete var_if;
		if (var_if)
			delete fac_if;
	}
	WheelVariableChanger(void(*OnApply)(double), float Xpos, float Ypos, double default_var, double default_fact, SingleTextLineSettings* STLS, std::string var_string = " ", std::string fac_string = " ", Type type = Type::exponential) : Width(100), Height(50) {
		this->OnApply = OnApply;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->variable = default_var;
		this->factor = default_fact;
		this->IsHovered = WheelFieldHovered = false;
		this->type = type;
		this->Sen = Sensitivity::on_wheel;
		var_if = new InputField(std::to_string(default_var).substr(0, 8), Xpos - 25., Ypos + 15, 10, 40, STLS, nullptr, 0x007FFFFF, STLS, var_string, 8, _Align::center, _Align::center, InputField::Type::FP_PositiveNumbers);
		fac_if = new InputField(std::to_string(default_fact).substr(0, 8), Xpos - 25., Ypos - 5, 10, 40, STLS, nullptr, 0x007FFFFF, STLS, fac_string, 8, _Align::center, _Align::center, InputField::Type::FP_PositiveNumbers);
	}
	void Draw() override {
		GLCOLOR(0xFFFFFF3F + WheelFieldHovered * 0x3F);
		glBegin(GL_QUADS);
		glVertex2f(Xpos, Ypos + 25);
		glVertex2f(Xpos, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		GLCOLOR((0x007FFF3F + WheelFieldHovered * 0x3F));
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos, Ypos + 25);
		glVertex2f(Xpos, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos - 50, Ypos + 25);
		glVertex2f(Xpos - 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		var_if->Draw();
		fac_if->Draw();
	}
	void SafeMove(float dx, float dy) override {
		Xpos += dx;
		Ypos += dy;
		var_if->SafeMove(dx, dy);
		fac_if->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override {
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * Width,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * Height;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void CheckupInputs() {
		variable = stod(var_if->GetCurrentInput("0"));
		factor = stod(fac_if->GetCurrentInput("0"));
	}
	void KeyboardHandler(CHAR CH) override {
		fac_if->KeyboardHandler(CH);
		var_if->KeyboardHandler(CH);
		if (IsHovered) {
			if (CH == 13) {
				CheckupInputs();
				if (OnApply)
					OnApply(variable);
			}
		}
	}
	void SafeStringReplace(std::string Meaningless) override {

	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		this->fac_if->MouseHandler(mx, my, Button, State);
		this->var_if->MouseHandler(mx, my, Button, State);
		mx -= Xpos;
		my -= Ypos;
		if (fabsf(mx) < Width * 0.5 && fabsf(my) < Height * 0.5) {
			IsHovered = true;
			if (mx >= 0 && mx <= Width * 0.5 && fabsf(my) < Height * 0.5) {
				if (Sen == Sensitivity::on_click && State == 1)
					if (OnApply)
						OnApply(variable);
				WheelFieldHovered = true;
				if (Button) {
					CheckupInputs();
					if (Button == 2 /*UP*/) {
						if (State == -1) {
							switch (type) {
							case WheelVariableChanger::Type::exponential: {variable *= factor; break; }
							case WheelVariableChanger::Type::addictable: {variable += factor;	break; }
							}
							var_if->UpdateInputString(std::to_string(variable));
							if (Sen == Sensitivity::on_wheel)
								if (OnApply)
									OnApply(variable);
						}
					}
					else if (Button == 3 /*DOWN*/) {
						if (State == -1) {
							switch (type) {
							case WheelVariableChanger::Type::exponential: {variable /= factor; break; }
							case WheelVariableChanger::Type::addictable: {variable -= factor;	break; }
							}
							var_if->UpdateInputString(std::to_string(variable));
							if (Sen == Sensitivity::on_wheel)
								if (OnApply)
									OnApply(variable);
						}
					}
				}
			}
		}
		else {
			IsHovered = false;
			WheelFieldHovered = false;
		}
		return 0;
	}
};
#endif