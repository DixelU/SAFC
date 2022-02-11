#pragma once
#ifndef SAFGUIF_L_SMRPV
#define SAFGUIF_L_SMRPV

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"

struct SMRP_Vis : HandleableUIPart {
	SingleMIDIReProcessor* SMRP;
	float XPos, YPos;
	bool Processing, Finished, Hovered;
	SingleTextLine* STL_Log, * STL_War, * STL_Err, * STL_Info;
	SMRP_Vis(float XPos, float YPos, SingleTextLineSettings* STLS) {
		std::lock_guard<std::recursive_mutex> locker(Lock);
		SMRP = nullptr;
		std::uint32_t BASERGBA;
		this->Processing = this->Hovered = this->Finished = 0;
		this->XPos = XPos;
		this->YPos = YPos;
		BASERGBA = STLS->RGBAColor;
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos - 20);
		this->STL_Log = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFFFF00FF;
		STLS->SetNewPos(XPos, YPos - 30);
		this->STL_War = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFF3F00FF;
		STLS->SetNewPos(XPos, YPos - 40);
		this->STL_Err = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos + 40);
		this->STL_Info = STLS->CreateOne("_");
		STLS->RGBAColor = BASERGBA;
	}
	~SMRP_Vis() override {
		delete STL_Log;
		delete STL_War;
		delete STL_Err;
		delete STL_Info;
	}
	void SafeMove(float dx, float dy) override {
		std::lock_guard<std::recursive_mutex> locker(Lock);
		XPos += dx;
		YPos += dy;
		STL_Err->SafeMove(dx, dy);
		STL_War->SafeMove(dx, dy);
		STL_Log->SafeMove(dx, dy);
		STL_Info->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override {
		NewX -= XPos;
		NewY -= YPos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override {
		return;
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(std::string Meaningless) override {
		return;
	}
	void SetInfoString(std::string NewInfoString) {
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->STL_Info->SafeStringReplace(NewInfoString);
	}
	void UpdateInfo() {
		if (!SMRP)
			return;
		std::lock_guard<std::recursive_mutex> locker(Lock);
		std::string T;
		if (SMRP->LogLine.size() && SMRP->LogLine != STL_Log->_CurrentText)
			STL_Log->SafeStringReplace(SMRP->LogLine);
		if (SMRP->WarningLine.size() && SMRP->WarningLine != STL_War->_CurrentText)
			STL_War->SafeStringReplace(SMRP->WarningLine);
		if (SMRP->ErrorLine.size() && SMRP->ErrorLine != STL_Err->_CurrentText)
			STL_Err->SafeStringReplace(SMRP->ErrorLine);
		if (Processing != SMRP->Processing)
			Processing = SMRP->Processing;
		if (Finished != SMRP->Finished)
			Finished = SMRP->Finished;
		T = std::to_string((SMRP->FilePosition * 100.) / (SMRP->FileSize)).substr(0, 5) + "%";
		if (Hovered && Processing) {
			T = SMRP->AppearanceFilename.substr(0, 30) + " " + T;
			STL_Info->SafeColorChange(0x9FCFFFFF);
		}
		else {
			STL_Info->SafeColorChange(0xFFFFFFFF);
		}
		if (STL_Info->_CurrentText != T)
			STL_Info->SafeStringReplace(T);
	}
	void Draw() override {
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!SMRP) {
			if (STL_Err->_CurrentText != "SMRP is NULL!")
				STL_Err->SafeStringReplace("SMRP is NULL!");
		}
		else {
			if (STL_Err->_CurrentText == "SMRP is NULL!")
				STL_Err->SafeStringReplace("_");
			UpdateInfo();
		}
		this->STL_Err->Draw();
		this->STL_War->Draw();
		this->STL_Log->Draw();
		this->STL_Info->Draw();
		if (SMRP) {
			if (Processing && !Finished)
				SpecialSigns::DrawWait(XPos, YPos, 15, 0x007FFF7F, 20);
			else {
				if (Finished)
					SpecialSigns::DrawOK(XPos, YPos, 20, 0x00FFFFFF);
				else SpecialSigns::DrawNo(XPos, YPos, 20, 0xFF3F00FF);
			}
		}
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		std::lock_guard<std::recursive_mutex> locker(Lock);
		mx -= XPos;
		my -= YPos;
		if (mx * mx + my * my < 900)
			Hovered = 1;
		else
			Hovered = 0;
		return 0;
	}
};

#endif // !SAFGUIF_L_SMRPV
