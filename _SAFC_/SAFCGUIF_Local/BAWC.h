#pragma once

#ifndef SAFGUIF_L_BAWC
#define SAFGUIF_L_BAWC 

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"

template<typename bool_type, typename number_type>
struct BoolAndWORDChecker : HandleableUIPart
{
	float XPos, YPos;
	bool_type* Flag;
	number_type* Number;
	SingleTextLine* STL_Info;
	BoolAndWORDChecker(float XPos, float YPos, SingleTextLineSettings* STLS,
		bool_type* Flag, number_type* Number)
	{
		this->XPos = XPos;
		this->YPos = YPos;
		this->Flag = Flag;
		this->Number = Number;
		STLS->SetNewPos(XPos, YPos + 40);
		this->STL_Info = STLS->CreateOne("_");
	}
	~BoolAndWORDChecker() override
	{
		delete this->STL_Info;
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		XPos += dx;
		YPos += dy;
		STL_Info->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX -= XPos;
		NewY -= YPos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{
		return;
	}
	void KeyboardHandler(CHAR CH) override
	{
		return;
	}
	void SafeStringReplace(std::string Meaningless) override
	{
		return;
	}
	void SetInfoString(std::string NewInfoString)
	{
		return;
	}
	void UpdateInfo()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Number)
		{
			std::string T = std::to_string(*Number);
			if (T != STL_Info->_CurrentText)STL_Info->SafeStringReplace(T);
		}
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		UpdateInfo();
		if (Flag)
		{
			if (*Flag)
				SpecialSigns::DrawOK(XPos, YPos, 15, 0x00FFFFFF);
			else
				SpecialSigns::DrawWait(XPos, YPos, 15, 0x007FFFFF, 20);
		}
		else SpecialSigns::DrawNo(XPos, YPos, 15, 0xFF0000FF);
		STL_Info->Draw();
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		return 0;
	}
};

#endif 