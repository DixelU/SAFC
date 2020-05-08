#pragma once
#ifndef SAF_CAT 
#define SAF_CAT 

#include <optional>
#include <Windows.h>

struct CutAndTransposeKeys {
	BYTE Min, Max;
	SHORT TransposeVal;
	CutAndTransposeKeys(BYTE Min, BYTE Max, SHORT TransposeVal = 0) {
		this->Min = Min;
		this->Max = Max;
		this->TransposeVal = TransposeVal;
	}
	inline std::optional<BYTE> Process(BYTE Value) {
		if (Value <= Max && Value >= Min) {
			SHORT SValue = (SHORT)Value + TransposeVal;
			if (SValue < 0 || SValue>255)return {};
			Value = SValue;
			return Value;
		}
		else {
			return {};
		}
	}
};
#endif