#pragma once
#ifndef SAFGUIF_L_CATH
#define SAFGUIF_L_CATH

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"

bool IsWhiteKey(std::uint8_t Key)
{
	Key %= 12;
	if (Key < 5)return !(Key & 1);
	else return (Key & 1);
}

struct CAT_Piano :HandleableUIPart
{
	std::shared_ptr<CutAndTransposeKeys> PianoTransform;
	SingleTextLine* MinCont, * MaxCont, * Transp;
	float CalculatedHeight, CalculatedWidth;
	float BaseXPos, BaseYPos, PianoHeight, KeyWidth;
	bool Focused;
	~CAT_Piano() override
	{
		delete MinCont;
		delete MaxCont;
		delete Transp;
	}

	CAT_Piano(float BaseXPos, float BaseYPos, float KeyWidth, float PianoHeight, std::shared_ptr<CutAndTransposeKeys> PianoTransform)
	{
		this->BaseXPos = BaseXPos;
		this->BaseYPos = BaseYPos;
		this->KeyWidth = KeyWidth;
		this->PianoHeight = PianoHeight;
		this->CalculatedHeight = PianoHeight * 4;
		this->CalculatedWidth = KeyWidth * (128 * 3);
		this->PianoTransform = std::move(PianoTransform);
		this->Focused = 0;
		System_White->SetNewPos(BaseXPos + KeyWidth * (128 * 1.25f), BaseYPos - 0.75f * PianoHeight);
		this->MinCont = System_White->CreateOne("_");
		System_White->SetNewPos(BaseXPos - KeyWidth * (128 * 1.25f), BaseYPos - 0.75f * PianoHeight);
		this->MaxCont = System_White->CreateOne("_");
		this->Transp = System_White->CreateOne("_");
		UpdateInfo();
	}

	void UpdateInfo()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!PianoTransform) 
			return;
		MinCont->SafeStringReplace("Min: " + std::to_string(PianoTransform->Min));
		MaxCont->SafeStringReplace("Max: " + std::to_string(PianoTransform->Max));
		Transp->SafeStringReplace("Transp: " + std::to_string(PianoTransform->TransposeVal));
		Transp->SafeChangePosition(BaseXPos + ((PianoTransform->TransposeVal >= 0) ? 1 : -1) * KeyWidth * (128 * 1.25f), BaseYPos +
			0.75 * PianoHeight
			);
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float x = BaseXPos - 128 * KeyWidth, pixsz = internal_range / window_base_width;
		bool Inside = 0;

		MinCont->Draw();
		MaxCont->Draw();
		Transp->Draw();
		if (!PianoTransform) 
			return;

		glLineWidth(0.5f * KeyWidth / pixsz);
		glBegin(GL_LINES);
		for (int i = 0; i < 256; i++, x += KeyWidth)
		{//Main piano
			Inside = ((i >= (PianoTransform->Min)) && (i <= (PianoTransform->Max)));
			if (IsWhiteKey(i))
			{
				glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos - 1.25f * PianoHeight);
				glVertex2f(x, BaseYPos - 0.25f * PianoHeight);
				continue;
			}
			else
			{
				glColor4f(0, 0, 0, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos - 0.75f * PianoHeight);
				glVertex2f(x, BaseYPos - 0.25f * PianoHeight);
			}
			glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
			glVertex2f(x, BaseYPos - 0.75f * PianoHeight);
			glVertex2f(x, BaseYPos - 1.25f * PianoHeight);
		}
		x = BaseXPos - (128 + PianoTransform->TransposeVal) * KeyWidth;
		for (int i = 0; i < 256; i++, x += KeyWidth)
		{//CAT Piano
			if (fabs(x - BaseXPos) >= 0.5 * CalculatedWidth)
				continue;
			Inside = ((i - (PianoTransform->TransposeVal) >= (PianoTransform->Min)) && (i - (PianoTransform->TransposeVal) <= (PianoTransform->Max)));
			if (IsWhiteKey(i))
			{
				glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos + 1.25f * PianoHeight);
				glVertex2f(x, BaseYPos + 0.25f * PianoHeight);
				continue;
			}
			else 
			{
				glColor4f(0, 0, 0, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos + 0.75f * PianoHeight);
				glVertex2f(x, BaseYPos + 0.25f * PianoHeight);
			}
			glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
			glVertex2f(x, BaseYPos + 1.25f * PianoHeight);
			glVertex2f(x, BaseYPos + 0.75f * PianoHeight);
		}
		glEnd();

		x = BaseXPos - (128 - PianoTransform->Min + 0.5f) * KeyWidth;//Square
		glLineWidth(1);
		glColor4f(0, 1, 0, 0.75f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, BaseYPos - 1.5f * PianoHeight);
		glVertex2f(x, BaseYPos + 1.5f * PianoHeight);
		x += KeyWidth * (PianoTransform->Max - PianoTransform->Min + 1);
		glVertex2f(x, BaseYPos + 1.5f * PianoHeight);
		glVertex2f(x, BaseYPos - 1.5f * PianoHeight);
		glEnd();
		if (!Focused) 
			return;
		glColor4f(1, 0.5, 0, 1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(BaseXPos - 0.5f * CalculatedWidth, BaseYPos - 0.5f * CalculatedHeight);
		glVertex2f(BaseXPos - 0.5f * CalculatedWidth, BaseYPos + 0.5f * CalculatedHeight);
		glVertex2f(BaseXPos + 0.5f * CalculatedWidth, BaseYPos + 0.5f * CalculatedHeight);
		glVertex2f(BaseXPos + 0.5f * CalculatedWidth, BaseYPos - 0.5f * CalculatedHeight);
		glEnd();
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		BaseXPos += dx;
		BaseYPos += dy;
		this->MaxCont->SafeMove(dx, dy);
		this->MinCont->SafeMove(dx, dy);
		this->Transp->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX -= BaseXPos;
		NewY -= BaseYPos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * CalculatedWidth,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * CalculatedHeight;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void KeyboardHandler(CHAR CH) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Focused)
		{
			if (!PianoTransform)
				return;
			switch (CH)
			{
			case 'W':
			case 'w':
				if (PianoTransform->TransposeVal < 255)
				{
					PianoTransform->TransposeVal += 1;
					UpdateInfo();
				}
				break;
			case 'S':
			case 's':
				if (PianoTransform->TransposeVal > -255)
				{
					PianoTransform->TransposeVal -= 1;
					UpdateInfo();
				}
				break;
			case 'D':
			case 'd':
				if (PianoTransform->Min < 255)
				{
					PianoTransform->Min += 1;
					UpdateInfo();
				}
				break;
			case 'A':
			case 'a':
				if (PianoTransform->Min > 0)
				{
					PianoTransform->Min -= 1;
					UpdateInfo();
				}
				break;
			case 'E':
			case 'e':
				if (PianoTransform->Max < 255)
				{
					PianoTransform->Max += 1;
					UpdateInfo();
				}
				break;
			case 'Q':
			case 'q':
				if (PianoTransform->Max > 0)
				{
					PianoTransform->Max -= 1;
					UpdateInfo();
				}
				break;
			}
		}
	}
	void SafeStringReplace(std::string Meaningless) override
	{
		return;
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		mx -= BaseXPos;
		my -= BaseYPos;
		if (fabsf(mx) <= CalculatedWidth * 0.5f && fabsf(my) <= CalculatedHeight * 0.5)
			Focused = 1;
		else Focused = 0;
		return 0;
	}
};


#endif 