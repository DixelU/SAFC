#pragma once
#ifndef SAF_FMC 
#define SAF_FMC

#include <fstream>
#include <string>
#include <filesystem>
#include <Windows.h>

#include "BS.h"
#include "MCTM.h"

struct FastMIDIChecker
{
	std::wstring File;
	bool IsAcssessable, IsMIDI;
	std::uint16_t PPQN, ExpectedTrackNumber;
	std::uint64_t FileSize;
	FastMIDIChecker(std::wstring File):
		File(File)
	{
		std::ifstream f(File, std::ios::in);
		if (f)IsAcssessable = 1;
		else IsAcssessable = 0;
		FileSize = PPQN = ExpectedTrackNumber = IsMIDI = 0;
		f.close();
		Collect();
	}
	void Collect()
	{
		std::ifstream f(File, std::ios::in | std::ios::binary);
		std::uint32_t MTHD = 0;

		for(int i = 0; i < 4; ++i)
			MTHD = (MTHD << 8) | (f.get());
		
		if (MTHD == MThd && f.good())
		{
			IsMIDI = 1;
			f.seekg(10, std::ios::beg);
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f.get());
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			f.close();
			FileSize = std::filesystem::file_size(File);
		}
		else
		{
			IsMIDI = 0;
			ThrowAlert_Error("Error accured while getting the MIDI file info!");
			f.close();
		}
		f.close();
	}
};

#endif 