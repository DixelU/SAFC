#pragma once
#ifndef SAF_FMC 
#define SAF_FMC

#include <fstream>
#include <string>
#include <filesystem>

#include "../platform_compat.h"

struct fast_midi_checker
{
	std_unicode_string filename;
	bool is_acssessible, is_midi;
	std::uint16_t PPQN, expected_track_number;
	std::uint64_t filesize;

	fast_midi_checker(std_unicode_string filename):
		filename(filename)
	{
		auto f = open_binary_input(filename);
		
		if (f)
			is_acssessible = true;
		else
			is_acssessible = false;

		filesize = PPQN = expected_track_number = is_midi = 0;

		f.close();

		collect();
	}

	void collect()
	{
		constexpr uint32_t MThd_header = 1297377380;

		auto f = open_binary_input(filename);
		std::uint32_t header = 0;

		for(int i = 0; i < 4; ++i)
			header = (header << 8) | (f.get());
		
		if (header == MThd_header && f.good())
		{
			is_midi = 1;
			f.seekg(10, std::ios::beg);
			expected_track_number = (expected_track_number << 8) | (f.get());
			expected_track_number = (expected_track_number << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			f.close();
			filesize = std::filesystem::file_size(path_to_filesystem(filename));
		}
		else
		{
			is_midi = 0;
			throw_alert_error("Error accured while getting the MIDI file info!");
			f.close();
		}
		f.close();
	}
};

#endif 
