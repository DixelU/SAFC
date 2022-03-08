#pragma once
#ifndef SAF_SMRP
#define SAF_SMRP

#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <array>
#include <iostream>
#include <stack>
#include <typeinfo>

#include <ranges>

#include "SMRP2.h"

#include "BS.h"
#include "SMIC.h"
#include "PLC.h"
#include "CAT.h"
#include "../bbb_ffio.h"


template<typename T>
struct optional {
	bool is;
	T value;
	optional() : value(), is(false) {}
	optional(const T& value) : value(value), is(true) {}
	optional(const T&& value) : value(value), is(true) {}
	inline operator bool() const {
		return is;
	}
	inline T operator = (const T& value) {
		//cout << typeid(value).name() << " : " << value << endl;
		is = true;
		return (this->value = value);
	}
	inline T operator = (const T&& value) {
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
	std::uint32_t ThreadID;
	std::wstring FileName, Postfix;
	std::string LogLine, WarningLine, ErrorLine, AppearanceFilename;
	std::int64_t GlobalOffset;
	std::uint32_t BoolSettings;
	float NewTempo;
	std::uint16_t NewPPQN;
	std::uint16_t TrackCount;
	bool Finished;
	bool Processing;
	bool InQueueToInplaceMerge, forced_single_channel_mode;
	std::uint8_t ProgramToOverride;
	std::int64_t SelectionStart, SelectionLength;
	std::shared_ptr<BYTE_PLC_Core> VolumeMapCore;
	std::shared_ptr<_14BIT_PLC_Core> PitchMapCore;
	std::shared_ptr<CutAndTransposeKeys> KeyConverter;
	bool ResetDeltatimesOutsideOfTheRange, AllowLegacyRunningStatusMetaIgnorance;
	std::uint64_t FilePosition, FileSize;
	inline static void ostream_write(std::vector<std::uint8_t>& vec, const std::vector<std::uint8_t>::iterator& beg, const std::vector<std::uint8_t>::iterator& end, std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	inline static void ostream_write(std::vector<std::uint8_t>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	inline static uint8_t push_vlv(uint32_t value, std::vector<std::uint8_t>& vec) {
		return push_vlv_s(value, vec);
	};
	inline static uint8_t push_vlv_s(std::uint64_t s_value, std::vector<std::uint8_t>& vec)
	{
		constexpr uint8_t $7byte_mask = 0x7F, max_size = 10, $7byte_mask_size = 7;
		constexpr uint8_t $adjacent7byte_mask = ~$7byte_mask;
		uint64_t value = s_value;
		uint8_t stack[max_size];
		uint8_t size = 0;
		uint8_t r_size = 0;
		do 
		{
			stack[size] = (value & $7byte_mask);
			value >>= $7byte_mask_size;
			if (size)
				stack[size] |= $adjacent7byte_mask;
			size++;
		} while (value);
		r_size = size;
		while (size)
			vec.push_back(stack[--size]);
		return r_size;
	}
	inline static uint64_t get_vlv(bbb_ffr& file_input)
	{
		uint64_t value = 0;
		std::uint8_t single_byte;
		do 
		{
			single_byte = file_input.get();
			value = value << 7 | single_byte & 0x7F;
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	SingleMIDIReProcessor(std::wstring filename, std::uint32_t Settings, float Tempo, std::int64_t GlobalOffset, std::uint16_t PPQN, std::uint32_t ThreadID, std::shared_ptr<BYTE_PLC_Core> VolumeMapCore, std::shared_ptr<_14BIT_PLC_Core> PitchMapCore, std::shared_ptr<CutAndTransposeKeys> KeyConverter, std::wstring RPostfix = L"_.mid", bool InplaceMerge = 0, std::uint64_t FileSize = 0, std::int64_t SelectionStart = 0, std::int64_t SelectionLength = -1, bool ResetDeltatimesOutsideOfTheRange = true) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		this->VolumeMapCore = VolumeMapCore;
		this->PitchMapCore = PitchMapCore;
		this->KeyConverter = KeyConverter;
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
		this->AllowLegacyRunningStatusMetaIgnorance = false;
	}
	SingleMIDIReProcessor(std::wstring filename, std::uint32_t Settings, float Tempo, std::int64_t GlobalOffset, std::uint16_t PPQN, std::uint32_t ThreadID, std::shared_ptr<PLC<std::uint8_t, std::uint8_t>> VolumeMapCore, std::shared_ptr<PLC<std::uint16_t, std::uint16_t>> PitchMapCore, std::shared_ptr<CutAndTransposeKeys> KeyConverter, std::wstring RPostfix = L"_.mid", bool InplaceMerge = 0, std::uint64_t FileSize = 0, std::int64_t SelectionStart = 0, std::int64_t SelectionLength = -1, bool ResetDeltatimesOutsideOfTheRange = true) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		if (VolumeMapCore)
			this->VolumeMapCore = std::make_shared<BYTE_PLC_Core>(VolumeMapCore);
		else
			this->VolumeMapCore = NULL;
		if (PitchMapCore)
			this->PitchMapCore = std::make_shared<_14BIT_PLC_Core>(PitchMapCore);
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
		this->AllowLegacyRunningStatusMetaIgnorance = false;
	}
#define PCLOG true


	void ReProcess() {
		bbb_ffr file_input(FileName.c_str());
		std::ofstream file_output(this->FileName + Postfix, std::ios::binary | std::ios::out);
		std::vector<std::uint8_t> Track;
		std::vector<std::uint8_t> UnLoad;
		std::vector<std::uint8_t> CorruptData;
		std::array<std::uint32_t, 4096> CurHolded;
		optional<std::uint32_t> CurrentTempo;
		optional<std::uint16_t> CurrentPitch[16];
		optional<std::uint8_t> CurrentProgram[16];
		optional<std::uint8_t> CurrentChannelAftertouch[16];
		optional<std::uint8_t> CurrentController[4096];
		optional<std::uint8_t> CurrentNoteAftertouch[4096];
		for (auto& v : CurHolded)
			v = 0;
		const bool ActiveSelection = SelectionLength > 0;
		std::uint32_t EventCounter = 0, Header/*,MultitaskTVariable*/, UnholdedCount = 0;
		const std::uint32_t BoolSettingsBuffer = BoolSettings;
		const std::uint32_t BSB_IgnoreAllSetting = SMP_BOOL_SETTINGS_IGNORE_TEMPOS | SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH | SMP_BOOL_SETTINGS_IGNORE_PITCHES | SMP_BOOL_SETTINGS_IGNORE_NOTES;
		std::uint16_t OldPPQN;
		double DeltaTimeTranq = 0, PPQNIncreaseAmount = 1;
		std::uint8_t Tempo1 = 0, Tempo2 = 0, Tempo3 = 0;
		std::uint8_t CurByte = 0, MetaEventType = 0;//register?
		std::uint8_t RunningStatusByte = 0;
		bool TrackEnded = false;
		std::uint64_t NegativeOffset = (GlobalOffset < 0) ? -GlobalOffset : 0;
		TrackCount = 0x0;
		Processing = 0x1;
		Track.reserve(10000000);//just in case...
		if (NewTempo > 3.5763f) {
			float LNT = 60000000.f / NewTempo;
			Tempo1 = (((std::uint32_t)LNT) >> 16);
			Tempo2 = ((((std::uint32_t)LNT) >> 8) & 0xFF);
			Tempo3 = (((std::uint32_t)LNT) & 0xFF);
		}
		///processing starts here///
		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());
		///PPQN swich
		OldPPQN = ((std::uint16_t)file_input.get()) << 8;
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

			RunningStatusByte = 0;
			std::int64_t CurTick = 0;//understandable
			bool Entered = false, Exited = false;
			std::int64_t TickTranq = 0;
			TrackEnded = false;

			DeltaTimeTranq = (GlobalOffset>=0) ? GlobalOffset : 0;
			///Parsing events
			while (file_input.good() && !file_input.eof()) {
				if (KeyConverter) {//?
					Header = 0;
					NULL;
				}

				std::uint8_t byte1 = 0, byte2 = 0, byte3 = 0; // arguments of events

				///Deltatime recalculation
				std::uint32_t LastDeltaTime = 0, L_VLV;
				std::uint32_t DeltaTimeSize = 0;

				CurByte = 0;
				do {//reading LastDeltaTime
					CurByte = file_input.get();
					LastDeltaTime = LastDeltaTime << 7 | CurByte & 0x7F;
				} while (CurByte & 0x80 && !file_input.eof());

				DeltaTimeTranq += ((double)(LastDeltaTime)*PPQNIncreaseAmount) + TickTranq;
				DeltaTimeTranq -= (L_VLV = (LastDeltaTime = (std::int64_t)DeltaTimeTranq));
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

						std::int64_t tStartTick = L_VLV - (CurTick - (SelectionStart));
						std::uint32_t tSZ = 0;
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
						for (std::uint16_t CurrentChannel = 0; CurrentChannel < 4096; CurrentChannel++) {
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
						std::int64_t tStartTick = L_VLV - (CurTick - (SelectionStart + SelectionLength));
						std::uint32_t tSZ = 0;
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

				CurByte = file_input.get();

				switch (CurByte) {
				case 0xFF: {
					if(!AllowLegacyRunningStatusMetaIgnorance)
						RunningStatusByte = 0;
					Track.push_back(CurByte);
					CurByte = file_input.get();//MetaEventType.
					Track.push_back(CurByte);
					if (CurByte == 0x2F) {//end of track event+
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

						bool PushFlag = false;

						if (ActiveSelection && !Entered) {//in case if these wasn't unloaded
							std::int64_t LocalEndingDeltaTime = SelectionStart - CurTick;
							if (ResetDeltatimesOutsideOfTheRange) {
								LocalEndingDeltaTime = 0;
							}
							int LEDT_size = 0;

							std::int64_t UnloadSize = UnLoad.size();

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
							for (std::uint16_t CurrentChannel = 0; CurrentChannel < 4096; CurrentChannel++) {
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
							//copy(Track.begin(), Track.end(), ostream_iterator<std::uint8_t>(fo));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							LogLine = "Track " + std::to_string(TrackCount++) + " is processed";
						}
						UnLoad.clear();
						Track.clear();
						EventCounter = 0;
						TrackEnded = true;
						goto loop_break_stmp;
					}
					else {//other meta events
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							L_VLV = 0;
							std::uint32_t MetaVLVSize = 0;
							MetaEventType = CurByte;
							do {
								CurByte = file_input.get();
								Track.push_back(CurByte);
								MetaVLVSize++;
								L_VLV = (L_VLV << 7) | (CurByte & 0x7F);
							} while (CurByte & 0x80);
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
									if (NewTempo > 3.5763f) 
										CurrentTempo = ((((std::uint32_t)Tempo1) << 16) | (((std::uint32_t)Tempo2) << 8) | (std::uint32_t)Tempo3);
									else
										CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
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
										CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
										EventCounter++;
									}
									else {
										Track.push_back(byte1 = file_input.get());
										Track.push_back(byte2 = file_input.get());
										Track.push_back(byte3 = file_input.get());
										CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
										EventCounter++;
									}
								}
								//printf("M %i %i\n", CurTick,CurrentTempo());
							}
						}
						else {
							L_VLV = 0;
							std::uint32_t MetaVLVSize = 0;
							MetaEventType = CurByte;
							do {
								CurByte = file_input.get();
								Track.push_back(CurByte);
								MetaVLVSize++;
								L_VLV = (L_VLV << 7) | (CurByte & 0x7F);
							} while (CurByte & 0x80);
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
									CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
									continue;
								}
								else if (NewTempo > 3.5763f) {
									file_input.get();
									file_input.get();
									file_input.get();
									Track.push_back(byte1 = Tempo1);
									Track.push_back(byte2 = Tempo2);
									Track.push_back(byte3 = Tempo3);
									CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
									EventCounter++;
								}
								else {
									Track.push_back(byte1 = file_input.get());
									Track.push_back(byte2 = file_input.get());
									Track.push_back(byte3 = file_input.get());
									CurrentTempo = ((((std::uint32_t)byte1) << 16) | (((std::uint32_t)byte2) << 8) | (std::uint32_t)byte3);
									EventCounter++;
								}
								//printf("N %i %i\n", CurTick, CurrentTempo());
							}
						}
					}
				} break;
				MH_CASE(0x80) : MH_CASE(0x90) : {
					RunningStatusByte = CurByte;
					bool DeleteEvent = BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES, isnoteon = (RunningStatusByte >= 0x90);
					std::uint8_t Key = file_input.get();
					Track.push_back(CurByte);///Event|Channel data
					if (this->KeyConverter && !DeleteEvent) {////key data processing
						auto T = (this->KeyConverter->process(Key));
						if (T.has_value())
							Track.push_back(Key = T.value());
						else {
							Track.push_back(0);
							DeleteEvent = true;
						}
					}
					else Track.push_back(Key);///key data processing
					std::uint8_t Vol = file_input.get();
					if (this->VolumeMapCore && (CurByte & 0x10))
						Vol = (*this->VolumeMapCore)[Vol];
					if (!Vol && isnoteon) {
						CurByte = (CurByte & 0xF) | 0x80;
						*((&Track.back()) - 1) = CurByte;
					}
					isnoteon = (CurByte >= 0x90);

					std::int16_t HoldIndex = (Key << 4) | (CurByte & 0xF);
					////////added
					if (!DeleteEvent) {
						if (isnoteon) {//if noteon
							CurHolded[HoldIndex]++;
							if (ActiveSelection && (!Entered || (Entered && Exited)))
								DeleteEvent = true;
						}
						else {//if noteoff
							if (CurHolded[HoldIndex]) {///can do something with ticks...
								//std::int64_t StartTick = CurHolded[Key].back();
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
				} break;
				MH_CASE(0xA0) : {
					RunningStatusByte = CurByte;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(CurByte);///event type
						Track.push_back(byte1 = file_input.get());///1st parameter//key
						Track.push_back(byte2 = file_input.get());///2nd parameter//value
						EventCounter++;
						CurrentNoteAftertouch[((std::uint16_t)byte1 << 4) | (CurByte & 0xF)] = byte2;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						byte2 = file_input.get();
						CurrentNoteAftertouch[((std::uint16_t)byte1 << 4) | (CurByte & 0xF)] = byte2;
						continue;
					}
				} break;
				MH_CASE(0xB0) : {
					RunningStatusByte = CurByte;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(CurByte);///event type
						Track.push_back(byte1 = file_input.get());///1st parameter
						Track.push_back(byte2 = file_input.get());///2nd parameter
						EventCounter++;
						CurrentController[((std::uint16_t)byte1 << 4) | (CurByte & 0xF)] = byte2;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						byte2 = file_input.get();
						CurrentController[((std::uint16_t)byte1 << 4) | (CurByte & 0xF)] = byte2;
						continue;
					}
				} break;
				MH_CASE(0xC0) : {
					RunningStatusByte = CurByte;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(CurByte);///event type
						if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)
							Track.push_back(byte1 = (file_input.get() & 0));///1st parameter
						else
							Track.push_back(byte1 = (file_input.get() & 0x7F));
						EventCounter++;
						CurrentProgram[CurByte & 0xF] = byte1;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();///for its single parameter
						CurrentProgram[CurByte & 0xF] = byte1;
						continue;
					}
				} break;
				MH_CASE(0xD0) :{
					RunningStatusByte = CurByte;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(CurByte);///event type
						Track.push_back(byte1 = (file_input.get() & 0x7F));
						//fi.get();
						EventCounter++;
						CurrentChannelAftertouch[CurByte & 0xF] = byte1;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get();
						CurrentChannelAftertouch[CurByte & 0xF] = byte1;
						continue;
					}
				} break;
				MH_CASE(0xE0) : {
					RunningStatusByte = CurByte;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
						Track.push_back(CurByte);
						if (this->PitchMapCore) {
							std::uint16_t PitchBend = file_input.get() << 7;
							PitchBend |= file_input.get() & 0x7F;
							PitchBend = (*PitchMapCore)[PitchBend];
							Track.push_back(byte1 = ((PitchBend >> 7) & 0x7F));
							Track.push_back(byte2 = (PitchBend & 0x7F));
						}
						else {
							Track.push_back(byte1 = (file_input.get() & 0x7F));
							Track.push_back(byte2 = (file_input.get() & 0x7F));
						}
						CurrentPitch[CurByte & 0xF] = ((((std::uint16_t)byte1) << 8) | (std::uint16_t)byte2);
						EventCounter++;
					}
					else {
						TickTranq = LastDeltaTime;
						for (int i = 0; i < (INT32)DeltaTimeSize; i++)
							Track.pop_back();
						byte1 = file_input.get() & 0x7F;
						byte2 = file_input.get() & 0x7F;///for 1st and 2nd parameter
						CurrentPitch[CurByte & 0xF] = ((((std::uint16_t)byte1) << 8) | (std::uint16_t)byte2);
						continue;
					}
				} break;
				case 0xF0: case 0xF7: {
					std::uint32_t sysexVLVsize = 0;
					if (!AllowLegacyRunningStatusMetaIgnorance)
						RunningStatusByte = 0;
					L_VLV = 0;
					Track.push_back(CurByte);//sysex type 
					do {
						CurByte = file_input.get();
						Track.push_back(CurByte);
						sysexVLVsize++;
						L_VLV = (L_VLV << 7) | (CurByte & 0x7F);
					} while (CurByte & 0x80);
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
				} break;
				default: {
					//printf("RSB: %li\n", file_input.tellg());
					switch (RunningStatusByte) {
						MH_CASE(0x80) :MH_CASE(0x90) :  {
							bool DeleteEvent = (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES), isnoteon = (RunningStatusByte >= 0x90);
							std::uint8_t Key = CurByte;
							Track.push_back(RunningStatusByte);///Event|Channel data
							if (this->KeyConverter) {////key data processing
								auto T = (this->KeyConverter->process(Key));
								if (T.has_value())
									Track.push_back(Key = T.value());
								else {
									Track.push_back(0);
									DeleteEvent = true;
								}
							}
							else Track.push_back(Key);///key data processing

							std::uint8_t Vol = file_input.get();
							if (this->VolumeMapCore && isnoteon)
								Vol = (*this->VolumeMapCore)[Vol];
							if (!Vol && isnoteon) {
								CurByte = (RunningStatusByte & 0xF) | 0x80;
								*((&Track.back()) - 1) = CurByte;
							}
							else
								CurByte = RunningStatusByte;

							isnoteon = (CurByte >= 0x90);
							std::int16_t HoldIndex = (Key << 4) | (RunningStatusByte & 0xF);
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
										//std::int64_t StartTick = CurHolded[Key].back();
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
						}break; 
						MH_CASE(0xA0) : {
							//RSB = CurByte;
							if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
								Track.push_back(RunningStatusByte);///event type
								Track.push_back(byte1 = CurByte);///1st parameter
								Track.push_back(byte2 = file_input.get());///2nd parameter
								EventCounter++;
								CurrentNoteAftertouch[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
							}
							else {
								TickTranq = LastDeltaTime;
								for (int i = 0; i < (INT32)DeltaTimeSize; i++)
									Track.pop_back();
								byte1 = CurByte;
								byte2 = file_input.get();
								CurrentNoteAftertouch[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
								continue;
							}
						}break; 
						MH_CASE(0xB0) : {
							if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
								Track.push_back(RunningStatusByte);///event type
								Track.push_back(byte1 = CurByte);///1st parameter
								Track.push_back(byte2 = file_input.get());///2nd parameter
								EventCounter++;
								CurrentController[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
							}
							else {
								TickTranq = LastDeltaTime;
								for (int i = 0; i < (INT32)DeltaTimeSize; i++)
									Track.pop_back();
								byte1 = CurByte;
								byte2 = file_input.get();
								CurrentController[(RunningStatusByte & 0xF) | (byte1 << 4)] = byte2;
								continue;
							}
						}break;
						MH_CASE(0xC0) : {
							if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
								Track.push_back(RunningStatusByte);///event type
								if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)
									Track.push_back(byte1 = 0);///1st parameter
								else
									Track.push_back(byte1 = CurByte);//
								EventCounter++;
								CurrentProgram[RunningStatusByte & 0xF] = byte1;
							}
							else {
								TickTranq = LastDeltaTime;
								for (int i = 0; i < (INT32)DeltaTimeSize; i++)
									Track.pop_back();
								byte1 = CurByte;
								CurrentProgram[RunningStatusByte & 0xF] = byte1;
								continue;
								//fi.get();///for its single parameter
							}
						}break;
						MH_CASE(0xD0) : {
							if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
								Track.push_back(RunningStatusByte);///event type
								Track.push_back(CurByte);
								EventCounter++;
								CurrentChannelAftertouch[RunningStatusByte & 0xF] = CurByte;
							}
							else {
								TickTranq = LastDeltaTime;
								for (int i = 0; i < (INT32)DeltaTimeSize; i++)
									Track.pop_back();
								CurrentChannelAftertouch[RunningStatusByte & 0xF] = CurByte;
								continue;
							}
						}break;
						MH_CASE(0xE0) : {
							if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
								Track.push_back(RunningStatusByte);
								if (this->PitchMapCore) {
									std::uint16_t PitchBend = CurByte << 7;
									PitchBend |= file_input.get() & 0x7F;
									PitchBend = (*PitchMapCore)[PitchBend];
									Track.push_back(byte1 = ((PitchBend >> 7) & 0x7F));
									Track.push_back(byte2 = (PitchBend & 0x7F));
								}
								else {
									Track.push_back(byte1 = CurByte);
									Track.push_back(byte2 = (file_input.get() & 0x7F));
								}
								EventCounter++;
								CurrentPitch[RunningStatusByte & 0xF] = (((std::uint32_t)byte1 << 8) | (std::uint32_t)byte2);
							}
							else {
								TickTranq = LastDeltaTime;
								for (int i = 0; i < (INT32)DeltaTimeSize; i++)
									Track.pop_back();
								byte1 = CurByte;
								byte2 = file_input.get();///for 2nd parameter
								CurrentPitch[RunningStatusByte & 0xF] = (((std::uint32_t)byte1 << 8) | (std::uint32_t)byte2);
								continue;
							}
						}break;
					default: {
						ErrorLine = "Track corruption. Pos:" + std::to_string(file_input.tellg());
						auto ErrorHolder = (ErrorLine + " " + std::string(FileName.begin(), FileName.end()));
						printf("%s\n", ErrorHolder.c_str());
						//ThrowAlert_Error(ErrorLine);
						RunningStatusByte = 0;
						std::uint32_t T = 0;
						std::uint8_t TempIOByte;

						CorruptData.clear();//Corrupted data handler when?//
						CorruptData.push_back(CurByte);
						while (T != 0xFF2F00 && file_input.good() && !file_input.eof()) {
							TempIOByte = file_input.get();
							CorruptData.push_back(TempIOByte);
							T = ((T << 8) | TempIOByte) & 0xFFFFFF;
						}

						for (int corruptedDeltaTimeInd = 0; corruptedDeltaTimeInd < DeltaTimeSize; corruptedDeltaTimeInd++)
							Track.pop_back(); 

						Track.push_back(0x00);
						Track.push_back(0xFF);
						Track.push_back(0x2F);
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
							//copy(Track.begin(), Track.end(), ostream_iterator<std::uint8_t>(file_output));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							WarningLine = "Partially recovered track. (" + std::to_string(TrackCount++) + ")";
							TrackCount++;
							TrackEnded = true;
						}
						Track.clear();
						EventCounter = 0;
						goto loop_break_stmp;
					} break;
					}
				} break;
				}

				goto post_break_stmp;
			loop_break_stmp:
				break;
			post_break_stmp:
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