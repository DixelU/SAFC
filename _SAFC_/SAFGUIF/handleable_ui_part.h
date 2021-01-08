#pragma once
#ifndef SAFGUIF_HUP
#define SAFGUIF_HUP

#include <mutex>
#include "header_utils.h"

struct HandleableUIPart {
	std::recursive_mutex Lock;
	BIT Enabled;
	~HandleableUIPart() {}
	HandleableUIPart() {}
	BIT virtual MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right*/, CHAR State /*-1 down, 1 up*/) = 0;
	void virtual Draw() = 0;
	void virtual SafeMove(float, float) = 0;
	void virtual SafeChangePosition(float, float) = 0;
	void virtual SafeChangePosition_Argumented(BYTE, float, float) = 0;
	void virtual SafeStringReplace(std::string) = 0;
	void virtual KeyboardHandler(char CH) = 0;
	void Enable() {
		Enabled = true;
	}
	void Disable() {
		Enabled = false;
	}
	void Invert_Enable() {
		Enabled ^= true;
	}
	bool virtual IsResizeable() {
		return false;
	}
	/* relative to right-bottom corner */
	void virtual SafeResize(float NewHeight, float NewWidth) {
		return;
	}
	inline DWORD virtual TellType() {
		return TT_UNSPECIFIED;
	}
};


#endif