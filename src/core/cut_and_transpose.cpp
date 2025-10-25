#include "cut_and_transpose.h"

cut_and_transpose::cut_and_transpose(std::uint8_t min, std::uint8_t max_val, std::int16_t transpose_val) noexcept :
	min_val(min), max_val(max_val), transpose_val(transpose_val) {
}

std::optional<std::uint8_t> cut_and_transpose::process(std::uint8_t value) noexcept
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
