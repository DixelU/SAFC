#pragma once
#ifndef SAF_CAT 
#define SAF_CAT 

#include <array>
#include <cstdint>

struct cut_and_transpose
{
	using result_type = std::uint16_t;
	using lookup_table = std::array<result_type, 256>;
	static constexpr result_type rejected = 0xFFFF;

	std::uint8_t min_val, max_val;
	std::int16_t transpose_val;

	constexpr cut_and_transpose(std::uint8_t min, std::uint8_t max_val, std::int16_t transpose_val) noexcept :
		min_val(min), max_val(max_val), transpose_val(transpose_val)
	{}

	[[nodiscard]] constexpr result_type process(std::uint8_t value) const noexcept
	{
		if (value < min_val || value > max_val)
			return rejected;

		const auto transposed = static_cast<std::int32_t>(value) + transpose_val;
		if (transposed < 0 || transposed > 255)
			return rejected;

		return static_cast<result_type>(transposed);
	}

	// Snapshot the editable settings once before processing any notes.
	[[nodiscard]] constexpr lookup_table bake() const noexcept
	{
		lookup_table result{};
		for (std::size_t value = 0; value < result.size(); ++value)
			result[value] = process(static_cast<std::uint8_t>(value));
		return result;
	}
};

#endif
