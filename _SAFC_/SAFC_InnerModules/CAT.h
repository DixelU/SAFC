#pragma once
#ifndef SAF_CAT 
#define SAF_CAT 

#include <optional>
#include <Windows.h>

struct CutAndTransposeKeys {
	std::uint8_t Min, Max;
	std::int16_t TransposeVal;
	CutAndTransposeKeys(std::uint8_t Min, std::uint8_t Max, std::int16_t TransposeVal = 0) {
		this->Min = Min;
		this->Max = Max;
		this->TransposeVal = TransposeVal;
	}
	inline std::optional<std::uint8_t> Process(std::uint8_t Value) {
		if (Value <= Max && Value >= Min) {
			std::int16_t SValue = (std::int16_t)Value + TransposeVal;
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