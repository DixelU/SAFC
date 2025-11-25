#pragma once

#ifndef SAF_CAT 
#define SAF_CAT 

#include <optional>
#include <cstdint>

struct cut_and_transpose
{
	std::uint8_t min_val, max_val;
	std::int16_t transpose_val;

	cut_and_transpose(std::uint8_t min, std::uint8_t max_val, std::int16_t transpose_val = 0) noexcept:
		min_val(min), max_val(max_val), transpose_val(transpose_val) {}

	inline std::optional<std::uint8_t> process(std::uint8_t value) noexcept
	{
		if (value <= this->max_val && value >= this->min_val)
		{
			std::int16_t svalue = (std::int16_t)value + this->transpose_val;
			if (svalue < 0 || svalue>255)
				return std::nullopt;
			value = svalue;
			return value;
		}
		
		return std::nullopt;
	}
};

#endif