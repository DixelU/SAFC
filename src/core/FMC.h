#pragma once
#ifndef SAF_FMC 
#define SAF_FMC

#include "header_utils.h"

#include <string>

struct fast_midi_checker
{
	std_unicode_string filename;
	bool is_accessible, is_midi;
	std::uint16_t ppqn, expected_track_number;
	std::uint64_t file_size;

	inline static constexpr std::uint32_t MThd_header = 1297377380;

	fast_midi_checker(std_unicode_string filename);

	void collect() noexcept;
};


#endif 
