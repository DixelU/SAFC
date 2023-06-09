#pragma once
#ifndef SAF_FMC 
#define SAF_FMC

#include <fstream>
#include <string>
#include <filesystem>
//#include <Windows.h>

#include "BS.h"
#include "MCTM.h"

struct FastMIDIChecker
{
	std_unicode_string File;
	bool IsAcssessable, IsMIDI;
	std::uint16_t PPQN, ExpectedTrackNumber;
	std::uint64_t FileSize;
	FastMIDIChecker(std_unicode_string File)
	{
		this->File = File;
		auto [f, fo_ptr] = open_wide_stream<std::istream, std::ios::in>(File,
#ifdef WINDOWS
			L"rb");
#else
			"rb");
#endif
		if (*f)IsAcssessable = 1;
		else IsAcssessable = 0;
		FileSize = PPQN = ExpectedTrackNumber = IsMIDI = 0;
		fclose(fo_ptr);
		delete f;
		Collect();
	}

	void Collect() {
		auto [f, fo_ptr] = open_wide_stream<std::istream, std::ios::in>(File,
#ifdef WINDOWS
			L"rb");
#else
			"rb");
#endif
		std::uint32_t MTHD = 0;
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		if (MTHD == MThd && f->good())
		{
			IsMIDI = 1;
			f->seekg(10, std::ios::beg);
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f->get());
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f->get());
			PPQN = (PPQN << 8) | (f->get());
			PPQN = (PPQN << 8) | (f->get());
			fclose(fo_ptr);
			std::error_code ec;
			auto size = std::filesystem::file_size(File, ec);
			if (ec) {
	            std::cout << ec.message() << std::endl;
	        }
	        else {
	        	std::uintmax_t t;
	        	std::cout << size << std::endl;
	        	std::cout << (FileSize = size) << std::endl;
			}
			//cout << FileSize;
		}
		else
		{
			IsMIDI = 0;
			ThrowAlert_Error("Error accured while getting the MIDI file info!");
			fclose(fo_ptr);
		}
		delete f;
	}
};


#endif 