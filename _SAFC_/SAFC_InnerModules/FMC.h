#pragma once
#ifndef SAF_FMC 
#define SAF_FMC

#include <fstream>
#include <string>
#include <filesystem>
#include <Windows.h>

#include "BS.h"
#include "MCTM.h"

struct FastMIDIChecker {
	std::wstring File;
	BIT IsAcssessable, IsMIDI;
	WORD PPQN, ExpectedTrackNumber;
	UINT64 FileSize;
	FastMIDIChecker(std::wstring File) {
		this->File = File;
		auto [f, fo_ptr] = open_wide_stream<std::istream, std::ios::in>(File, L"rb");
		if (*f)IsAcssessable = 1;
		else IsAcssessable = 0;
		FileSize = PPQN = ExpectedTrackNumber = IsMIDI = 0;
		fclose(fo_ptr);
		delete f;
		Collect();
	}
	void Collect() {
		auto [f, fo_ptr] = open_wide_stream<std::istream, std::ios::in>(File, L"rb");
		DWORD MTHD = 0;
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		MTHD = (MTHD << 8) | (f->get());
		if (MTHD == MThd && f->good()) {
			IsMIDI = 1;
			f->seekg(10, std::ios::beg);
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f->get());
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f->get());
			PPQN = (PPQN << 8) | (f->get());
			PPQN = (PPQN << 8) | (f->get());
			fclose(fo_ptr);
			FileSize = std::filesystem::file_size(File);
			//cout << FileSize;
		}
		else {
			IsMIDI = 0;
			ThrowAlert_Error("Error accured while getting the MIDI file info!");
			fclose(fo_ptr);
		}
		delete f;
	}
};


#endif 
