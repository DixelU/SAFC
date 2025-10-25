#pragma once

#ifndef SAF_CAT 
#define SAF_CAT 

#include <optional>
#include <cstdint>

struct cut_and_transpose
{
	std::uint8_t min_val, max_val;
	std::int16_t transpose_val;

	cut_and_transpose(std::uint8_t min, std::uint8_t max_val, std::int16_t transpose_val = 0) noexcept;

	std::optional<std::uint8_t> process(std::uint8_t value) noexcept;
};

#endif