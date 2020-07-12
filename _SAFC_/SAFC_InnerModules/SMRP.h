#pragma once
#ifndef SAF_SMRP
#define SAF_SMRP


#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <array>
#include <iostream>
#include <typeinfo>

#include "BS.h"
#include "SMIC.h"
#include "PLC.h"
#include "CAT.h"
#include "../bbb_ffio.h"

template<typename T>
struct conditional {
	BIT is;
	T value;
	conditional() : value(), is(false) {}
	conditional(const T& value) : value(value), is(true) {}
	inline operator bool() const {
		return is;
	}
	inline T operator = (const T& value) {
		//cout << typeid(value).name() << " : " << value << endl;
		is = true;
		return (this->value = value);
	}
	inline T operator()() const {
		return value;
	}
	inline void reset() {
		is = false;
		this->value = 0;
	}
};

struct SingleMIDIReProcessor {
	DWORD ThreadID;
	std::wstring FileName, Postfix;
	std::string LogLine, WarningLine, ErrorLine, AppearanceFilename;
	DWORD GlobalOffset;
	DWORD BoolSettings;
	FLOAT NewTempo;
	WORD NewPPQN;
	WORD TrackCount;
	BIT Finished;
	BIT Processing;
	BIT InQueueToInplaceMerge;
	INT64 SelectionStart, SelectionLength;
	BYTE_PLC_Core* VolumeMapCore;
	_14BIT_PLC_Core* PitchMapCore;
	CutAndTransposeKeys* KeyConverter;
	BIT ResetDeltatimesOutsideOfTheRange;
	UINT64 FilePosition, FileSize;
	static void ostream_write(std::vector<BYTE>& vec, const std::vector<BYTE>::iterator& beg, const std::vector<BYTE>::iterator& end, std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	static void ostream_write(std::vector<BYTE>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	SingleMIDIReProcessor(std::wstring filename, DWORD Settings, FLOAT Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, BYTE_PLC_Core* VolumeMapCore, _14BIT_PLC_Core* PitchMapCore, CutAndTransposeKeys* KeyConverter, std::wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 SelectionStart = 0, INT64 SelectionLength = -1, BIT ResetDeltatimesOutsideOfTheRange = true) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		this->VolumeMapCore = (BYTE_PLC_Core*)VolumeMapCore;
		this->PitchMapCore = (_14BIT_PLC_Core*)PitchMapCore;
		this->KeyConverter = (CutAndTransposeKeys*)KeyConverter;
		this->InQueueToInplaceMerge = InplaceMerge;
		this->Postfix = RPostfix;
		this->FileSize = FileSize;
		this->LogLine = this->WarningLine = this->ErrorLine = " ";
		this->Finished = 0;
		this->TrackCount = 0;
		this->Processing = 0;
		this->FilePosition = 0;
		this->SelectionStart = SelectionStart;
		this->SelectionLength = SelectionLength;
		this->ResetDeltatimesOutsideOfTheRange = ResetDeltatimesOutsideOfTheRange;
	}
	SingleMIDIReProcessor(std::wstring filename, DWORD Settings, FLOAT Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, PLC<BYTE, BYTE>* VolumeMapCore, PLC<WORD, WORD>* PitchMapCore, CutAndTransposeKeys* KeyConverter, std::wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 SelectionStart = 0, INT64 SelectionLength = -1, BIT ResetDeltatimesOutsideOfTheRange = true) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		if (VolumeMapCore)
			this->VolumeMapCore = new BYTE_PLC_Core(VolumeMapCore);
		else
			this->VolumeMapCore = NULL;
		if (PitchMapCore)
			this->PitchMapCore = new _14BIT_PLC_Core(PitchMapCore);
		else
			this->PitchMapCore = NULL;
		this->KeyConverter = KeyConverter;
		this->InQueueToInplaceMerge = InplaceMerge;
		this->Postfix = RPostfix;
		this->FileSize = FileSize;
		this->LogLine = this->WarningLine = this->ErrorLine = "_";
		this->Finished = 0;
		this->TrackCount = 0;
		this->Processing = 0;
		this->FilePosition = 0;
		this->SelectionStart = SelectionStart;
		this->SelectionLength = SelectionLength;
		this->ResetDeltatimesOutsideOfTheRange = ResetDeltatimesOutsideOfTheRange;
	}
#define PCLOG true
	void ReProcess() {
		bbb_ffr file_input(FileName.c_str());
		std::ofstream file_output(this->FileName + Postfix, std::ios::binary | std::ios::out);
		std::vector<BYTE> Track;///quite memory expensive...
		std::vector<BYTE> UnLoad;
		std::vector<BYTE> CorruptData;
		std::array<DWORD, 4096> CurHolded;
		conditional<DWORD> CurrentTempo;
		conditional<WORD> CurrentPitch[16];
		conditional<BYTE> CurrentProgram[16];
		conditional<BYTE> CurrentChannelAftertouch[16];
		conditional<BYTE> CurrentController[4096];
		conditional<BYTE> CurrentNoteAftertouch[4096];
		auto push_vlv = [](uint32_t value, std::vector<BYTE>& vec) -> BYTE {
			uint8_t stack[5];
			uint8_t size = 0;
			uint8_t r_size = 0;
			do {
				stack[size] = (value & 0x7F);
				value >>= 7;
				if (size)
					stack[size] |= 0x80;
				size++;
			} while (value);
			r_size = size;
			while (size)
				vec.push_back(stack[--size]);
			return r_size;
		};
		for (auto& v : CurHolded)
			v = 0;
		const bool ActiveSelection = SelectionLength > 0;
		DWORD EventCounter = 0, Header/*,MultitaskTVariable*/, UnholdedCount = 0;
		const DWORD BoolSettingsBuffer = BoolSettings;
		const DWORD BSB_IgnoreAllSetting = SMP_BOOL_SETTINGS_IGNORE_TEMPOS | SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH | SMP_BOOL_SETTINGS_IGNORE_PITCHES | SMP_BOOL_SETTINGS_IGNORE_NOTES;
		WORD OldPPQN;
		double DeltaTimeTranq = 0, TDeltaTime = 0, PPQNIncreaseAmount = 1;
		BYTE Tempo1 = 0, Tempo2 = 0, Tempo3 = 0;
		BYTE IO = 0, MetaEventType = 0;//register?
		BYTE RunningStatusByte = 0;
		BIT TrackEnded = false;
		TrackCount = 0x0;
		Processing = 0x1;
		Track.reserve(10000000);//just in case...
		if (NewTempo > 3.5763f) {
			FLOAT LNT = 60000000.f / NewTempo;
			Tempo1 = (((DWORD)LNT) >> 16);
			Tempo2 = ((((DWORD)LNT) >> 8) & 0xFF);
			Tempo3 = (((DWORD)LNT) & 0xFF);
		}
		///processing starts here///
		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());
		///PPQN swich
		OldPPQN = ((WORD)file_input.get()) << 8;
		OldPPQN |= file_input.get();
		file_output.put(NewPPQN >> 8);
		file_output.put(NewPPQN & 0xFF);
		PPQNIncreaseAmount = (double)NewPPQN / OldPPQN;
		///if statement says everything
		///just in case of having no tempoevents :)
		if (NewTempo>3.5763f) {
			Track.push_back(0x00);
			Track.push_back(0xFF);
			Track.push_back(0x51);
			Track.push_back(0x03);
			Track.push_back(Tempo1);
			Track.push_back(Tempo2);
			Track.push_back(Tempo3);
			EventCounter++;
		}
		///Tracks 
		while (file_input.good() && !file_input.eof()) {
			Header = 0;
			while (Header != MTrk && file_input.good() && !file_input.eof()) {//MTrk = 1297379947
				Header = (Header << 8) | file_input.get();
			}
			///here was header;
			for (int i = 0; i < 4 && file_input.good(); i++)
				file_input.get();

			if (file_input.eof())
				break;

			/*swap settings*/
			if (ActiveSelection) {
				BoolSettings |= BSB_IgnoreAllSetting;
			}

			CurrentTempo.reset();//resetting things
			for (int CurrentChannel = 0; CurrentChannel < 16; CurrentChannel++) {
				CurrentChannelAftertouch[CurrentChannel].reset();
				CurrentPitch[CurrentChannel].reset();
				CurrentProgram[CurrentChannel].reset();
			}
			for (int i = 0; i < 4096; i++) {
				CurrentController[i].reset();
				CurrentNoteAftertouch[i].reset();
				CurHolded[i] = 0;
			}

			INT64 CurTick = 0;//understandable
			bool Entered = false, Exited = false;
			INT64 TickTranq = 0;
			TrackEnded = false;

			DeltaTimeTranq = GlobalOffset;
			///Parsing events
			while (file_input.good() && !file_input.eof()) {
				if (KeyConverter) {//?
					Header = 0;
					NULL;
				}

				BYTE byte1 = 0, byte2 = 0, byte3 = 0; // arguments of events

				///Deltatime recalculation
				DWORD LastDeltaTime = 0, L_VLV;
				DWORD DeltaTimeSize = 0;

				IO = 0;
				do {//reading LastDeltaTime
					IO = file_input.get();
					LastDeltaTime = LastDeltaTime << 7 | IO & 0x7F;
				} while (IO & 0x80 && !file_input.eof());

				DeltaTimeTranq += ((double)(LastDeltaTime)*PPQNIncreaseAmount) + TickTranq;
				DeltaTimeTranq -= (L_VLV = (LastDeltaTime = (INT64)DeltaTimeTranq));
				CurTick += LastDeltaTime - TickTranq;//adding to the "current tick"

				if (ActiveSelection) {
					if (!Entered && CurTick >= SelectionStart) {
						BoolSettings = BoolSettingsBuffer;
						//printf("Entered#%hu Cur:%li \t Edge:%li \t VLV:%i\n", TrackCount, CurTick, SelectionStart, LastDeltaTime);
						Entered = true;
						bool EscapeFlag = true;
						for (auto& v : CurHolded) {
							if (v || (BoolSettings&SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
								EscapeFlag = false;
								//printf("Escaped\n");
								break;
							}
						}
						if ((CurrentTempo) || (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_TEMPOS))
							EscapeFlag = false;
						for (int CurrentChannel = 0; CurrentChannel < 16; CurrentChannel++) {
							if (
								CurrentChannelAftertouch[CurrentChannel] || (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH) ||
								CurrentController[CurrentChannel] ||
								CurrentNoteAftertouch[CurrentChannel] ||
								CurrentPitch[CurrentChannel] || (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES) ||
								CurrentProgram[CurrentChannel]
								) {
								EscapeFlag = false;
								break;
							}
						}

						INT64 tStartTick = L_VLV - (CurTick - (SelectionStart));
						DWORD tSZ = 0;
						bool Flag_NotFirstRun = false;
						LastDeltaTime = L_VLV - tStartTick;
						L_VLV = LastDeltaTime;
						//printf("Entered#%hu VLV:%i \t TStart:%li\n", TrackCount, LastDeltaTime, tStartTick);
						if (ResetDeltatimesOutsideOfTheRange)
							tStartTick = 0;

						if (EscapeFlag) {
							goto EscapeFromEntered;
						}
						tSZ = push_vlv(tStartTick, Track);

						if (CurrentTempo && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_TEMPOS)) {
							if (Flag_NotFirstRun)
								Track.push_back(0);
							else
								Flag_NotFirstRun = true;
							Track.push_back(0xFF);
							Track.push_back(0x51);
							Track.push_back(0x03);
							Track.push_back((CurrentTempo() >> 16) & 0xFF);
							Track.push_back((CurrentTempo() >> 8) & 0xFF);
							Track.push_back(CurrentTempo() & 0xFF);
							CurrentTempo.reset();
							EventCounter++;
						}
						for (unsigned char CurrentChannel = 0; CurrentChannel < 16; CurrentChannel++) {
							if (CurrentChannelAftertouch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
								if (Flag_NotFirstRun)
									Track.push_back(0);
								else
									Flag_NotFirstRun = true;
								Track.push_back(0xD0 | CurrentChannel);
								Track.push_back(CurrentChannelAftertouch[CurrentChannel]());
								CurrentChannelAftertouch[CurrentChannel].reset();
								EventCounter++;
							}
							if (CurrentProgram[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
								if (Flag_NotFirstRun)
									Track.push_back(0);
								else
									Flag_NotFirstRun = true;
								Track.push_back(0xC0 | CurrentChannel);
								Track.push_back(CurrentProgram[CurrentChannel]());
								CurrentProgram[CurrentChannel].reset();
								EventCounter++;
							}
							if (CurrentPitch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {//stuff
								if (Flag_NotFirstRun)
									Track.push_back(0);
								else
									Flag_NotFirstRun = true;
								Track.push_back(0xE0 | CurrentChannel);
								Track.push_back(CurrentPitch[CurrentChannel]() >> 8);
								Track.push_back(CurrentPitch[CurrentChannel]() & 0xFF);
								CurrentPitch[CurrentChannel].reset();
								EventCounter++;
							}
						}
						for (WORD CurrentChannel = 0; CurrentChannel < 4096; CurrentChannel++) {
							if (CurrentController[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
								if (Flag_NotFirstRun)
									Track.push_back(0);
								else
									Flag_NotFirstRun = true;
								Track.push_back(0xB0 | (CurrentChannel&0xF));
								Track.push_back(CurrentChannel >> 4);
								Track.push_back(CurrentController[CurrentChannel]());
								CurrentController[CurrentChannel].reset();
								EventCounter++;
							}
							if (CurrentNoteAftertouch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
								if (Flag_NotFirstRun)
									Track.push_back(0);
								else
									Flag_NotFirstRun = true;
								Track.push_back(0xA0 | (CurrentChannel&0xF));
								Track.push_back(CurrentChannel>>4);
								Track.push_back(CurrentNoteAftertouch[CurrentChannel]());
								CurrentNoteAftertouch[CurrentChannel].reset();
								EventCounter++;
							}
						}
						if (!(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							for (int k = 0; k < 4096; k++) {//multichanneled tracks.
								for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
									if (Flag_NotFirstRun)
										Track.push_back(0);
									else
										Flag_NotFirstRun = true;
									Track.push_back((k & 0xF) | 0x90);
									Track.push_back(k >> 4);
									Track.push_back(1);
									EventCounter++;
								}
							}
						}
						
					}

				EscapeFromEntered:
					if (Entered && !Exited && (CurTick >= (SelectionStart + SelectionLength))) {
						BoolSettings |= BSB_IgnoreAllSetting;
						//printf("Exited#%hu  Cur:%li \t Edge:%li \t VLV:%i\n", TrackCount, CurTick, SelectionStart + SelectionLength, LastDeltaTime);
						Exited = true;
						bool EscapeFlag = true;
						for (auto& v : CurHolded) {
							if (v) {
								EscapeFlag = false;
								//printf("Escaped\n");
								break;
							}
						}
						if (EscapeFlag)
							goto ActiveSelectionBorderEscapePoint;
						INT64 tStartTick = L_VLV - (CurTick - (SelectionStart + SelectionLength));
						DWORD tSZ = 0;
						bool Flag_FirstRun = false;
						LastDeltaTime = L_VLV - tStartTick;
						L_VLV = LastDeltaTime;
						//printf("Exited#%hu  VLV:%i \t TStart:%li\n", TrackCount, LastDeltaTime, tStartTick);

						tSZ = push_vlv(tStartTick, Track);

						if (!(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							for (int k = 0; k < 4096; k++) {
								for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
									if (Flag_FirstRun)Track.push_back(0);
									else Flag_FirstRun = true;
									Track.push_back((k & 0xF) | 0x80);
									Track.push_back(k >> 4);
									Track.push_back(0);
									EventCounter++;
								}
							}
						}
					}
				}
			ActiveSelectionBorderEscapePoint:
				DeltaTimeSize = push_vlv(L_VLV, Track);

				IO = file_input.get();

				if (IO == 0xFF) {//I love metaevents :d
					RunningStatusByte = 0;
					Track.push_back(IO);
					IO = file_input.get();//MetaEventType.
					Track.push_back(IO);
					if (IO == 0x2F) {//end of track event+
						if ((ResetDeltatimesOutsideOfTheRange) && ((!Entered) || (Exited))) {//it doesn't matter? right?
							for (int i = -2; i < (int)DeltaTimeSize; i++)
								Track.pop_back();
							Track.push_back(0);
							Track.push_back(0xFF);
							Track.push_back(0x2F);
							DeltaTimeSize = 1;
						}

						file_input.get();
						Track.push_back(0);

						BIT PushFlag = false;

						if (ActiveSelection && !Entered) {//in case if these wasn't unloaded
							INT64 LocalEndingDeltaTime = SelectionStart - CurTick;
							if (ResetDeltatimesOutsideOfTheRange) {
								LocalEndingDeltaTime = 0;
							}
							int LEDT_size = 0;

							INT64 UnloadSize = UnLoad.size();

							LEDT_size = push_vlv(LocalEndingDeltaTime, UnLoad);

							if (CurrentTempo && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_TEMPOS)) {
								UnLoad.push_back(0xFF);
								UnLoad.push_back(0x51);
								UnLoad.push_back(0x03);
								UnLoad.push_back((CurrentTempo() >> 16) & 0xFF);
								UnLoad.push_back((CurrentTempo() >> 8) & 0xFF);
								UnLoad.push_back(CurrentTempo() & 0xFF);
								CurrentTempo.reset();
								PushFlag = true;
							}
							for (unsigned char CurrentChannel = 0; CurrentChannel < 16; CurrentChannel++) {
								if (CurrentChannelAftertouch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
									if (PushFlag)
										UnLoad.push_back(0);
									else
										PushFlag = true;
									UnLoad.push_back(0xD0 | CurrentChannel);
									UnLoad.push_back(CurrentChannelAftertouch[CurrentChannel]());
									CurrentChannelAftertouch[CurrentChannel].reset();
								}
								if (CurrentProgram[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
									if (PushFlag)
										UnLoad.push_back(0);
									else
										PushFlag = true;
									UnLoad.push_back(0xC0 | CurrentChannel);
									UnLoad.push_back(CurrentProgram[CurrentChannel]());
									CurrentProgram[CurrentChannel].reset();
								}
								if (CurrentPitch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {//stuff
									if (PushFlag)
										UnLoad.push_back(0);
									else
										PushFlag = true;
									UnLoad.push_back(0xE0 | CurrentChannel);
									UnLoad.push_back(CurrentPitch[CurrentChannel]() >> 8);
									UnLoad.push_back(CurrentPitch[CurrentChannel]() & 0x7F);
									CurrentPitch[CurrentChannel].reset();
								}
							}
							for (WORD CurrentChannel = 0; CurrentChannel < 4096; CurrentChannel++) {
								if (CurrentController[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
									if (PushFlag)
										UnLoad.push_back(0);
									else
										PushFlag = true;
									UnLoad.push_back(0xB0 | (CurrentChannel & 0xF));
									UnLoad.push_back(CurrentChannel >> 4);
									UnLoad.push_back(CurrentController[CurrentChannel]());
									CurrentController[CurrentChannel].reset();
								}
								if (CurrentNoteAftertouch[CurrentChannel] && !(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {//stuff
									if (PushFlag)
										UnLoad.push_back(0);
									else
										PushFlag = true;
									UnLoad.push_back(0xA0 | (CurrentChannel & 0xF));
									UnLoad.push_back(CurrentChannel >> 4);
									UnLoad.push_back(CurrentNoteAftertouch[CurrentChannel]());
									CurrentNoteAftertouch[CurrentChannel].reset();
								}
							}
							if (!PushFlag)
								UnLoad.resize(UnloadSize);
						}
						if (!(BoolSettingsBuffer & SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
								for (int i = 0; i < CurHolded[k]; i++) {
									UnLoad.push_back(0);
									UnLoad.push_back((k & 0xF) | 0x80);
									UnLoad.push_back(k >> 4);
									UnLoad.push_back(0x40);
								}
							}
						}

						if ((BoolSettingsBuffer & SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV && !EventCounter && !UnLoad.size())) {
							LogLine = "Track " + std::to_string(TrackCount) + " deleted!";
						}
						else {
							Track.insert(Track.end() - DeltaTimeSize - 3, UnLoad.begin(), UnLoad.end());
							file_output.put('M'); 
							file_output.put('T'); 
							file_output.put('r'); 
							file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(fo));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							LogLine = "Track " + std::to_string(TrackCount++) + " is processed";
						}
						UnLoad.clear();
						Track.clear();
						EventCounter = 0;
						TrackEnded = true;
						break;
					}
					else {//other meta events
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							L_VLV = 0;
							DWORD MetaVLVSize = 0;
							MetaEventType = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								MetaVLVSize++;
								L_VLV = (L_VLV << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (MetaEventType != 0x51) {
								for (int i = 0; i < L_VLV; i++)
									Track.push_back(file_input.get());
							}
							else {//tempo events
								if (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {//deleting
									TickTranq = LastDeltaTime;
									for (int i = -3; i < (int)DeltaTimeSize; i++)
										Track.pop_back();
									byte1 = file_input.get();
									byte2 = file_input.get();
									byte3 = file_input.get();
									CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
									continue;
								}
								else {
									if (NewTempo > 3.5763f) {
										file_input.get();
										file_input.get();
										file_input.get();
										Track.push_back(byte1 = Tempo1);
										Track.push_back(byte2 = Tempo2);
										Track.push_back(byte3 = Tempo3);
										CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
										EventCounter++;
									}
									else {
										Track.push_back(byte1 = file_input.get());
										Track.push_back(byte2 = file_input.get());
										Track.push_back(byte3 = file_input.get());
										CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
										EventCounter++;
									}
								}
								//printf("M %i %i\n", CurTick,CurrentTempo());
							}
						}
						else {
							L_VLV = 0;
							DWORD MetaVLVSize = 0;
							MetaEventType = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								MetaVLVSize++;
								L_VLV = (L_VLV << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (MetaEventType != 0x51) {//deleting
								TickTranq = LastDeltaTime;
								for (int i = -2 - MetaVLVSize; i < (int)DeltaTimeSize; i++)
									Track.pop_back();
								for (int i = 0; i < L_VLV; i++)
									file_input.get();
								continue;
							}
							else {//tempo 
								if (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {
									TickTranq = LastDeltaTime;
									for (int i = -3; i < (INT32)DeltaTimeSize; i++)
										Track.pop_back();
									byte1 = file_input.get();
									byte2 = file_input.get();
									byte3 = file_input.get();
									CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
									continue;
								}
								else if (NewTempo > 3.5763f) {
									file_input.get();
									file_input.get();
									file_input.get();
									Track.push_back(byte1 = Tempo1);
									Track.push_back(byte2 = Tempo2);
									Track.push_back(byte3 = Tempo3);
									CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
									EventCounter++;
								}
								else {
									Track.push_back(byte1 = file_input.get());
									Track.push_back(byte2 = file_input.get());
									Track.push_back(byte3 = file_input.get());
									CurrentTempo = ((((DWORD)byte1) << 16) | (((DWORD)byte2) << 8) | (DWORD)byte3);
									EventCounter++;
								}
								//printf("N %i %i\n", CurTick, CurrentTempo());
							}
						}
					}
				}
				else if (IO >= 0x80 && IO <= 0x9F) {///notes
					RunningStatusByte = IO;
					bool DeleteEvent = BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES, isnoteon = (RunningStatusByte >= 0x90);
					BYTE Key = file_input.get();
					Track.push_back(IO);///Event|Channel data
					if (this->KeyConverter && !DeleteEvent) {////key data processing
						auto T = (this->KeyConverter->Process(Key));
						if (T.has_value())
							Track.push_back(Key = T.value());
						else {
							Track.push_back(0);
							DeleteEvent = true;
						}
					}
					else Track.push_back(Key);///key data processing
					BYTE Vol = file_input.get();
					if (this->VolumeMapCore && (IO & 0x10))
						Vol = (*this->VolumeMapCore)[Vol];
					if (!Vol && isnoteon) {
						IO = (IO & 0xF) | 0x80;
						*((&Track.back()) - 1) = IO;
					}
					isnoteon = (IO >= 0x90);

					SHORT HoldIndex = (Key << 4) | (IO & 0xF);
					////////added
					if (!DeleteEvent) {
						if (isnoteon) {//if noteon
							CurHolded[HoldIndex]++;
							if (ActiveSelection && (!Entered || (Entered && Exited)))
								DeleteEvent = true;
						}
						else {//if noteoff
							if (CurHolded[HoldIndex]) {///can do something with ticks...
								//INT64 StartTick = CurHolded[Key].back();
								CurHolded[HoldIndex]--;
								if (ActiveSelection && (!Entered || (Entered && Exited)))
									DeleteEvent = true;
							}
							else {
								DeleteEvent = true;
								UnholdedCount++;
								if (STRICT_WARNINGS)
									WarningLine = "OFF of nonON Note: " + std::to_string(UnholdedCount);
								//cout << LastDeltaTime << ":" << TickTranq << endl;
							}
						}
					}
					if (DeleteEvent) {///deletes the note from the point of writing the key data.
						if (isnoteon) //if noteon
							CurHolded[HoldIndex]++;
						else if (CurHolded[HoldIndex])
							CurHolded[HoldIndex]--;

						TickTranq = LastDeltaTime;
						for (int q = -2; q < (INT32)DeltaTimeSize; q++) {
							Track.pop_back();
						}
						continue;
					}
					else {
						Track.push_back(Vol);
					}
					EventCounter++;
				}
				else if (IO >= 0xE0 && IO <= 0xEF) {///pitch bending event
					RunningStatusByte = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
						Track.push_back(IO);
						if (this->PitchMapCore) {
							WORD PitchBend = file_input.get() << 7;
							PitchBend |= file_input.get() & 0x7F;
							PitchBend = (*PitchMapCore)[PitchBend];
							Track.push_back(byte1 = ((PitchBend >> 7) & 0x7F));
							Track.push_back(byte2 = (PitchBend & 0x7F));
						}
						else {
							Track.push_back(byte1 = (file_input.get() & 0x7F));
							Track.push_back(byte2 = (file_input.get() & 0x7F));
						}
						CurrentPitch[IO & 0xF] = ((((WORD)byte1) << 8) | (WORD)byte2);
						EventCounter++;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get()&0x7F;
						byte2 = file_input.get()&0x7F;///for 1st and 2nd parameter
						CurrentPitch[IO & 0xF] = ((((WORD)byte1) << 8) | (WORD)byte2);
						continue;
					}
				}
				else if ((IO >= 0xA0 && IO <= 0xAF)) {///note aftertouch :t
					RunningStatusByte = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(byte1 = file_input.get());///1st parameter//key
						Track.push_back(byte2 = file_input.get());///2nd parameter//value
						EventCounter++;
						CurrentNoteAftertouch[((WORD)byte1 << 4) | (IO & 0xF)] = byte2;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						byte2 = file_input.get();
						CurrentNoteAftertouch[((WORD)byte1 << 4) | (IO & 0xF)] = byte2;
						continue;
					}
				}
				else if ((IO >= 0xB0 && IO <= 0xBF)) {///controller :t
					RunningStatusByte = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(byte1 = file_input.get());///1st parameter
						Track.push_back(byte2 = file_input.get());///2nd parameter
						EventCounter++;
						CurrentController[((WORD)byte1 << 4) | (IO & 0xF)] = byte2;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						byte2 = file_input.get();
						CurrentController[((WORD)byte1 << 4) | (IO & 0xF)] = byte2;
						continue;
					}
				}
				else if ((IO >= 0xC0 && IO <= 0xCF)) {///program change
					RunningStatusByte = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)
							Track.push_back(byte1 = (file_input.get() & 0));///1st parameter
						else
							Track.push_back(byte1 = (file_input.get() & 0x7F));
						EventCounter++;
						CurrentProgram[IO & 0xF] = byte1;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();///for its single parameter
						CurrentProgram[IO & 0xF] = byte1;
						continue;
					}
				}
				else if (IO >= 0xD0 && IO <= 0xDF) {///Channel aftertouch :D
					RunningStatusByte = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(byte1 = (file_input.get() & 0x7F));
						//fi.get();
						EventCounter++;
						CurrentChannelAftertouch[IO & 0xF] = byte1;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						CurrentChannelAftertouch[IO & 0xF] = byte1;
						continue;
					}
				}
				else if (IO == 0xF0 || IO == 0xF7) {
					DWORD sysexVLVsize = 0;
					RunningStatusByte = 0;
					L_VLV = 0;
					Track.push_back(IO);//sysex type 
					do {
						IO = file_input.get();
						Track.push_back(IO);
						sysexVLVsize++;
						L_VLV = (L_VLV << 7) | (IO & 0x7F);
					} while (IO & 0x80);
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						for (int i = 0; i < L_VLV; i++)
							Track.push_back(file_input.get());
						EventCounter++;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = -1 - sysexVLVsize; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						for (int i = 0; i < L_VLV; i++)
							file_input.get();
						continue;
					}
				}
				else {///RUNNING STATUS BYTE
					//////////////////////////
					//WarningLine = "Running status detected";
					if (RunningStatusByte >= 0x80 && RunningStatusByte <= 0x9F) {///notes
						bool DeleteEvent = (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES), isnoteon = (RunningStatusByte >= 0x90);
						BYTE Key = IO;
						Track.push_back(RunningStatusByte);///Event|Channel data
						if (this->KeyConverter) {////key data processing
							auto T = (this->KeyConverter->Process(Key));
							if (T.has_value())
								Track.push_back(Key = T.value());
							else {
								Track.push_back(0);
								DeleteEvent = true;
							}
						}
						else Track.push_back(Key);///key data processing

						BYTE Vol = file_input.get();
						if (this->VolumeMapCore && isnoteon)
							Vol = (*this->VolumeMapCore)[Vol];
						if (!Vol && isnoteon) {
							IO = (RunningStatusByte & 0xF) | 0x80;
							*((&Track.back()) - 1) = IO;
						}
						else
							IO = RunningStatusByte;

						isnoteon = (IO >= 0x90);
						SHORT HoldIndex = (Key << 4) | (RunningStatusByte & 0xF);
						//printf("%X %X\n", Key, file_input.tellg());

						////////added
						if (!DeleteEvent) {
							if (isnoteon) {//if noteon
								CurHolded[HoldIndex]++;
								if (ActiveSelection && (!Entered || (Entered && Exited)))
									DeleteEvent = true;
							}
							else {//if noteoff
								if (CurHolded[HoldIndex]) {///can do something with ticks...
									//INT64 StartTick = CurHolded[Key].back();
									CurHolded[HoldIndex]--;
									if (ActiveSelection && (!Entered || (Entered && Exited)))
										DeleteEvent = true;
								}
								else {
									DeleteEvent = true;
									UnholdedCount++;
									//printf("%X %X nonholded\n", Key, file_input.tellg());
									if (STRICT_WARNINGS)
										WarningLine = "(RSB) OFF of nonON Note: " + std::to_string(UnholdedCount);
								}
							}
						}
						if (DeleteEvent) {///deletes the note from the point of writing the key data.
							if (isnoteon)//if noteon
								CurHolded[HoldIndex]++;
							else if(CurHolded[HoldIndex])
								CurHolded[HoldIndex]--;

							TickTranq = LastDeltaTime;
							for (int q = -2; q < (INT32)DeltaTimeSize; q++) {
								Track.pop_back();
							}
							continue;
						}
						else {
							Track.push_back(Vol);
						}
						EventCounter++;
					}
					else if (RunningStatusByte >= 0xE0 && RunningStatusByte <= 0xEF) {///pitch bending event
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
							Track.push_back(RunningStatusByte);
							if (this->PitchMapCore) {
								WORD PitchBend = IO << 7;
								PitchBend |= file_input.get() & 0x7F;
								PitchBend = (*PitchMapCore)[PitchBend];
								Track.push_back(byte1 = ((PitchBend >> 7) & 0x7F));
								Track.push_back(byte2 = (PitchBend & 0x7F));
							}
							else {
								Track.push_back(byte1 = IO);
								Track.push_back(byte2 = (file_input.get() & 0x7F));
							}
							EventCounter++;
							CurrentPitch[RunningStatusByte & 0xF] = (((DWORD)byte1 << 8) | (DWORD)byte2);
						}
						else {
							TickTranq = LastDeltaTime;
							for (int i = 0; i < (INT32)DeltaTimeSize; i++)
								Track.pop_back();
							byte1 = IO;
							byte2 = file_input.get();///for 2nd parameter
							CurrentPitch[RunningStatusByte & 0xF] = (((DWORD)byte1 << 8) | (DWORD)byte2);
							continue;
						}
					}
					else if ((RunningStatusByte >= 0xA0 && RunningStatusByte <= 0xAF)) {///note aftertouch :t
						//RSB = IO;
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RunningStatusByte);///event type
							Track.push_back(byte1 = IO);///1st parameter
							Track.push_back(byte2 = file_input.get());///2nd parameter
							EventCounter++;
							CurrentNoteAftertouch[(RunningStatusByte & 0xF) | (byte1<<4)] = byte2;
						}
						else {
							TickTranq = LastDeltaTime;
							for (int i = 0; i < (INT32)DeltaTimeSize; i++)
								Track.pop_back();
							byte1 = IO;
							byte2 = file_input.get();
							CurrentNoteAftertouch[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
							continue;
						}
					}
					else if ((RunningStatusByte >= 0xB0 && RunningStatusByte <= 0xBF)) {///controller :t
						//RSB = IO;
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RunningStatusByte);///event type
							Track.push_back(byte1 = IO);///1st parameter
							Track.push_back(byte2 = file_input.get());///2nd parameter
							EventCounter++;
							CurrentController[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
						}
						else {
							TickTranq = LastDeltaTime;
							for (int i = 0; i < (INT32)DeltaTimeSize; i++)
								Track.pop_back();
							byte1 = IO;
							byte2 = file_input.get();
							CurrentController[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
							continue;
						}
					}
					else if ((RunningStatusByte >= 0xC0 && RunningStatusByte <= 0xCF)) {///program change
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RunningStatusByte);///event type
							if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)
								Track.push_back(byte1 = (file_input.get() & 0));///1st parameter
							else
								Track.push_back(byte1 = IO);//
							EventCounter++;
							CurrentProgram[RunningStatusByte & 0xF] = byte1;
						}
						else {
							TickTranq = LastDeltaTime;
							for (int i = 0; i < (INT32)DeltaTimeSize; i++)
								Track.pop_back();
							byte1 = IO;
							CurrentProgram[RunningStatusByte & 0xF] = byte1;
							continue;
							//fi.get();///for its single parameter
						}
					}
					else if (RunningStatusByte >= 0xD0 && RunningStatusByte <= 0xDF) {///????:D
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RunningStatusByte);///event type
							Track.push_back(IO);
							EventCounter++;
							CurrentChannelAftertouch[RunningStatusByte & 0xF] = IO;
						}
						else {
							TickTranq = LastDeltaTime;
							for (int i = 0; i < (INT32)DeltaTimeSize; i++)
								Track.pop_back();
							CurrentChannelAftertouch[RunningStatusByte & 0xF] = IO;
							continue;
						}
					}
					else {
						ErrorLine = "Track corruption. Pos:" + std::to_string(file_input.tellg());
						//ThrowAlert_Error(ErrorLine);
						RunningStatusByte = 0;
						DWORD T = 0;
						BYTE TempIOByte;

						CorruptData.clear();//Corrupted data handler when?//
						CorruptData.push_back(IO);
						while (T != 0x2FFF00 && file_input.good() && !file_input.eof()) {
							TempIOByte = file_input.get();
							CorruptData.push_back(TempIOByte);
							T = ((T << 8) | TempIOByte) & 0xFFFFFF;
						}

						for (int corruptedDeltaTimeInd = 0; corruptedDeltaTimeInd < DeltaTimeSize; corruptedDeltaTimeInd++)
							Track.pop_back();

						Track.push_back(0x00);
						Track.push_back(0x2F);
						Track.push_back(0xFF);
						Track.push_back(0x00);

						BoolSettings = BoolSettingsBuffer;//just in case
						UnLoad.clear();

						UnLoad.push_back(0x00);
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
								for (int i = 0; i < CurHolded[k]; i++) {
									UnLoad.push_back((k & 0xF) | 0x80);
									UnLoad.push_back(k >> 4);
									UnLoad.push_back(0x40);
									UnLoad.push_back(0x00);
								}
								CurHolded[k] = 0;
							}
						}
						UnLoad.pop_back();

						if (!EventCounter && BoolSettings & SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV) {
							WarningLine = "Cleared corrupted track. (" + std::to_string(TrackCount) + ")";
							TrackEnded = true;
						}
						else {
							Track.insert(Track.end() - DeltaTimeSize - 3, UnLoad.begin(), UnLoad.end());
							UnLoad.clear();
							file_output.put('M'); file_output.put('T'); file_output.put('r'); file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(file_output));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							WarningLine = "Partially recovered track. (" + std::to_string(TrackCount++) + ")";
							TrackCount++;
							TrackEnded = true;
						}
						Track.clear();
						EventCounter = 0;
						break;
					}
				}
				TickTranq = 0;
			}
			BoolSettings = BoolSettingsBuffer;
			FilePosition = file_input.tellg();
		}
		file_input.close();
		Track.clear();
		file_output.seekp(10, std::ios::beg);///changing amount of tracks :)
		file_output.put(TrackCount >> 8);
		file_output.put(TrackCount & 0xFF);
		file_output.close();
		Processing = 0;
		Finished = 1;
	}
}; 

#endif 