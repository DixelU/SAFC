#pragma once
#ifndef SAF_SMRP
#define SAF_SMRP

#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <array>
#include <iostream>

#include "BS.h"
#include "SMIC.h"
#include "PLC.h"
#include "CAT.h"
#include "../bbb_ffio.h"

struct SingleMIDIReProcessor {
	DWORD ThreadID;
	std::wstring FileName, Postfix;
	std::string LogLine, WarningLine, ErrorLine, AppearanceFilename;
	DWORD GlobalOffset;
	DWORD BoolSettings;
	DWORD NewTempo;
	WORD NewPPQN;
	WORD TrackCount;
	BIT Finished;
	BIT Processing;
	BIT InQueueToInplaceMerge;
	INT64 Start, Length;
	BYTE_PLC_Core* VolumeMapCore;
	_14BIT_PLC_Core* PitchMapCore;
	CutAndTransposeKeys* KeyConverter;
	BIT CutFromStart;
	UINT64 FilePosition, FileSize;
	static void ostream_write(std::vector<BYTE>& vec, const std::vector<BYTE>::iterator& beg, const std::vector<BYTE>::iterator& end, std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	static void ostream_write(std::vector<BYTE>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	SingleMIDIReProcessor(std::wstring filename, DWORD Settings, DWORD Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, BYTE_PLC_Core* VolumeMapCore, _14BIT_PLC_Core* PitchMapCore, CutAndTransposeKeys* KeyConverter, std::wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 Start = 0, INT64 Length = -1, BIT CurFromStart = false) {
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
		this->Start = Start;
		this->Length = Length;
		this->CutFromStart = CutFromStart;
	}
	SingleMIDIReProcessor(std::wstring filename, DWORD Settings, DWORD Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, PLC<BYTE, BYTE>* VolumeMapCore, PLC<WORD, WORD>* PitchMapCore, CutAndTransposeKeys* KeyConverter, std::wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 Start = 0, INT64 Length = -1, BIT CurFromStart = false) {
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
		this->Start = Start;
		this->Length = Length;
		this->CutFromStart = CutFromStart;
	}
#define PCLOG true
	void ReProcess() {
		bbb_ffr file_input(FileName.c_str());
		std::ofstream file_output(this->FileName + Postfix, std::ios::binary | std::ios::out);
		std::vector<BYTE> Track;///quite memory expensive...
		std::vector<BYTE> UnLoad;
		std::array<DWORD, 4096> CurHolded;
		for (auto& v : CurHolded)
			v = 0;
		bool ActiveSelection = Length > 0;
		DWORD EventCounter = 0, Header/*,MultitaskTVariable*/, UnholdedCount = 0;
		WORD OldPPQN;
		double DeltaTimeTranq = GlobalOffset, TDeltaTime = 0, PPQNIncreaseAmount = 1;
		BYTE Tempo1, Tempo2, Tempo3;
		BYTE IO = 0, TYPEIO = 0;//register?
		BYTE RSB = 0;
		BIT TrackEnded = false;
		TrackCount = 0x0;
		Processing = 0x1;
		Track.reserve(10000000);//just in case...

		if (NewTempo) {
			Tempo1 = ((60000000 / NewTempo) >> 16),
				Tempo2 = (((60000000 / NewTempo) >> 8) & 0xFF),
				Tempo3 = ((60000000 / NewTempo) & 0xFF);
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
		if (NewTempo) {
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

			if (file_input.eof())break;

			INT64 CurTick = 0;//understandable
			bool Entered = false, Exited = false;
			INT64 TickTranq = 0;
			TrackEnded = false;

			DeltaTimeTranq += GlobalOffset;
			///Parsing events
			while (file_input.good() && !file_input.eof()) {
				if (KeyConverter) {//?
					Header = 0;
					NULL;
				}
				///Deltatime recalculation
				DWORD vlv = 0, tvlv;
				DWORD size = 0, true_delta;
				IO = 0;
				do {
					IO = file_input.get();
					vlv = vlv << 7 | IO & 0x7F;
				} while (IO & 0x80 && !file_input.eof());
				//DWORD file_pos = fi.tellg();
				DeltaTimeTranq += ((double)vlv * PPQNIncreaseAmount) + TickTranq;
				DeltaTimeTranq -= (tvlv = (vlv = (DWORD)DeltaTimeTranq));
				CurTick += vlv - TickTranq;//adding to the "current tick"
				///pushing proto vlv to track 

				if (ActiveSelection) {
					if (!Entered && CurTick >= Start) {
						//printf("Entered# Cur:%li \t Edge:%li \t VLV:%i\n", CurTick, Start, vlv);
						Entered = true;
						bool EscapeFlag = true;
						for (auto& v : CurHolded) {//in case if none of the notes are pressed
							if (v) {
								EscapeFlag = false;
								//printf("Escaped\n");
								break;
							}
						}
						if (EscapeFlag)
							goto EscapeFromEntered;
						INT64 tStartTick = tvlv - (CurTick - (Start));
						DWORD tSZ = 0;
						bool Flag_FirstRun = false;
						vlv = tvlv - tStartTick;
						tvlv = vlv;
						//printf("Entered# VLV:%i \t TStart:%li\n", vlv, tStartTick);
						do {
							tSZ++;
							Track.push_back(tStartTick & 0x7F);
							tStartTick >>= 7;
						} while (tStartTick);
						///making magic with it...
						for (DWORD i = 0; i < (tSZ >> 1); i++) {
							std::swap(Track[Track.size() - 1 - i], Track[Track.size() - tSZ + i]);
						}
						for (DWORD i = 2; i <= tSZ; i++) {
							Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
						}
						for (int k = 0; k < 4096; k++) {//multichanneled tracks.
							for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
								if (Flag_FirstRun)Track.push_back(0);
								else Flag_FirstRun = true;
								Track.push_back((k & 0xF) | 0x90);
								Track.push_back(k >> 4);
								Track.push_back(1);
							}
						}
					}
				EscapeFromEntered:
					if (Entered && !Exited && (CurTick >= (Start + Length))) {
						//printf("Exited#  Cur:%li \t Edge:%li \t VLV:%i\n", CurTick, Start + Length, vlv);
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
						INT64 tStartTick = tvlv - (CurTick - (Start + Length));
						DWORD tSZ = 0;
						bool Flag_FirstRun = false;
						vlv = tvlv - tStartTick;
						tvlv = vlv;
						//printf("Exited#  VLV:%i \t TStart:%li\n", vlv, tStartTick);
						do {
							tSZ++;
							Track.push_back(tStartTick & 0x7F);
							tStartTick >>= 7;
						} while (tStartTick);
						///making magic with it...
						for (DWORD i = 0; i < (tSZ >> 1); i++) {
							std::swap(Track[Track.size() - 1 - i], Track[Track.size() - tSZ + i]);
						}
						for (DWORD i = 2; i <= tSZ; i++) {
							Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
						}
						for (int k = 0; k < 4096; k++) {
							for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
								if (Flag_FirstRun)Track.push_back(0);
								else Flag_FirstRun = true;
								Track.push_back((k & 0xF) | 0x80);
								Track.push_back(k >> 4);
								Track.push_back(1);
							}
						}
					}
				}
			ActiveSelectionBorderEscapePoint:
				do {
					size++;
					Track.push_back(tvlv & 0x7F);
					tvlv >>= 7;
				} while (tvlv);
				///making magic with it...
				for (DWORD i = 0; i < (size >> 1); i++) {
					std::swap(Track[Track.size() - 1 - i], Track[Track.size() - size + i]);
				}
				for (DWORD i = 2; i <= size; i++) {
					Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
				}
				IO = file_input.get();
				if (IO == 0xFF) {//I love metaevents :d
					RSB = 0;
					Track.push_back(IO);
					IO = file_input.get();//MetaEventType.
					Track.push_back(IO);
					if (IO == 0x2F) {//end of track event
						file_input.get();
						Track.push_back(0);

						UnLoad.push_back(0x01);
						for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
							for (int i = 0; i < CurHolded[k]; i++) {
								UnLoad.push_back((k & 0xF) | 0x80);
								UnLoad.push_back(k >> 4);
								UnLoad.push_back(0x40);
								UnLoad.push_back(0x00);
							}
							CurHolded[k] = 0;
						}
						UnLoad.pop_back();

						if ((BoolSettings & SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV && !EventCounter)) {
							LogLine = "Track " + std::to_string(TrackCount) + " deleted!";
						}
						else {
							Track.insert(Track.end() - size - 3, UnLoad.begin(), UnLoad.end());
							UnLoad.clear();
							file_output.put('M'); file_output.put('T'); file_output.put('r'); file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(fo));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							LogLine = "Track " + std::to_string(TrackCount++) + " is processed";
						}
						Track.clear();
						EventCounter = 0;
						TrackEnded = true;
						break;
					}
					else {//other meta events
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							tvlv = 0;
							DWORD vlvsize = 0;
							TYPEIO = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								vlvsize++;
								tvlv = (tvlv << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (TYPEIO != 0x51) {
								for (int i = 0; i < tvlv; i++)
									Track.push_back(file_input.get());
							}
							else {
								if (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {//deleting
									TickTranq = vlv;
									for (int i = -2 - vlvsize; i < (INT32)size; i++)
										Track.pop_back();
									for (int i = 0; i < tvlv; i++)file_input.get();
									continue;
								}
								else {
									if (NewTempo) {
										file_input.get();
										file_input.get();
										file_input.get();
										Track.push_back(Tempo1);
										Track.push_back(Tempo2);
										Track.push_back(Tempo3);
										EventCounter++;
									}
									else {
										Track.push_back(file_input.get());
										Track.push_back(file_input.get());
										Track.push_back(file_input.get());
										EventCounter++;
									}
								}
							}
						}
						else {
							tvlv = 0;
							DWORD vlvsize = 0;
							TYPEIO = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								vlvsize++;
								tvlv = (tvlv << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (TYPEIO != 0x51 || BoolSettings & SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {//deleting
								TickTranq = vlv;
								for (int i = -2 - vlvsize; i < (INT32)size; i++)
									Track.pop_back();
								for (int i = 0; i < tvlv; i++)file_input.get();
								continue;
							}
							else {
								if (NewTempo) {
									file_input.get();
									file_input.get();
									file_input.get();
									Track.push_back(Tempo1);
									Track.push_back(Tempo2);
									Track.push_back(Tempo3);
									EventCounter++;
								}
								else {
									Track.push_back(file_input.get());
									Track.push_back(file_input.get());
									Track.push_back(file_input.get());
									EventCounter++;
								}
							}
						}
					}
				}
				else if (IO >= 0x80 && IO <= 0x9F) {///notes
					RSB = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES) || Length > 0) {
						bool DeleteEvent = BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES, isnoteon = (RSB >= 0x90);
						BYTE Key = file_input.get();
						Track.push_back(IO);///Event|Channel data
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
									//cout << vlv << ":" << TickTranq << endl;
								}
							}
						}
						if (DeleteEvent) {///deletes the note from the point of writing the key data.
							TickTranq = vlv;
							for (int q = -2; q < (INT32)size; q++) {
								Track.pop_back();
							}
							continue;
						}
						else {
							Track.push_back(Vol);
						}
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();///for 1st and 2nd parameter
						continue;
					}
				}
				else if (IO >= 0xE0 && IO <= 0xEF) {///pitch bending event
					RSB = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
						Track.push_back(IO);
						if (this->PitchMapCore) {
							WORD PitchBend = file_input.get() << 7;
							PitchBend |= file_input.get() & 0x7F;
							PitchBend = (*PitchMapCore)[PitchBend];
							Track.push_back((PitchBend >> 7) & 0x7F);
							Track.push_back(PitchBend & 0x7F);
						}
						else {
							Track.push_back(file_input.get() & 0x7F);
							Track.push_back(file_input.get() & 0x7F);
						}
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();///for 1st and 2nd parameter
						continue;
					}
				}
				else if ((IO >= 0xA0 && IO <= 0xBF)) {///idk :t
					RSB = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(file_input.get());///1st parameter
						Track.push_back(file_input.get());///2nd parameter
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();
						continue;
					}
				}
				else if ((IO >= 0xC0 && IO <= 0xCF)) {///program change
					RSB = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)Track.push_back(file_input.get() & 0);///1st parameter
						else Track.push_back(file_input.get() & 0x7F);
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get();///for its single parameter
						continue;
					}
				}
				else if (IO >= 0xD0 && IO <= 0xDF) {///????:D
					RSB = IO;
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(file_input.get() & 0x7F);
						//fi.get();
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						file_input.get();///...:D
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						continue;
					}
				}
				else if (IO == 0xF0 || IO == 0xF7) {
					DWORD vlvsize = 0;
					RSB = 0;
					tvlv = 0;
					Track.push_back(IO);
					do {
						IO = file_input.get();
						Track.push_back(IO);
						vlvsize++;
						tvlv = (tvlv << 7) | (IO & 0x7F);
					} while (IO & 0x80);
					if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						for (int i = 0; i < tvlv; i++)
							Track.push_back(file_input.get());
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = -1 - vlvsize; i < (INT32)size; i++)
							Track.pop_back();
						for (int i = 0; i < tvlv; i++)file_input.get();
						continue;
					}
				}
				else {///RUNNING STATUS BYTE
					//////////////////////////
					//WarningLine = "Running status detected";
					if (RSB >= 0x80 && RSB <= 0x9F) {///notes
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							bool DeleteEvent = (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES), isnoteon = (RSB >= 0x90);
							BYTE Key = IO;
							Track.push_back(RSB);///Event|Channel data
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
								IO = (RSB & 0xF) | 0x80;
								*((&Track.back()) - 1) = IO;
							}
							else
								IO = RSB;

							isnoteon = (IO >= 0x90);
							SHORT HoldIndex = (Key << 4) | (RSB & 0xF);
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
								TickTranq = vlv;
								for (int q = -2; q < (INT32)size; q++) {
									Track.pop_back();
								}
								continue;
							}
							else {
								Track.push_back(Vol);
							}
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();///for 2nd parameter
							continue;
						}
					}
					else if (RSB >= 0xE0 && RSB <= 0xEF) {///pitch bending event
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
							Track.push_back(RSB);
							if (this->PitchMapCore) {
								WORD PitchBend = IO << 7;
								PitchBend |= file_input.get() & 0x7F;
								PitchBend = (*PitchMapCore)[PitchBend];
								Track.push_back((PitchBend >> 7) & 0x7F);
								Track.push_back(PitchBend & 0x7F);
							}
							else {
								Track.push_back(IO);
								Track.push_back(file_input.get() & 0x7F);
							}
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();///for 2nd parameter
							continue;
						}
					}
					else if ((RSB >= 0xA0 && RSB <= 0xBF)) {///idk :t
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							Track.push_back(IO);///1st parameter
							Track.push_back(file_input.get());///2nd parameter
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();
							continue;
						}
					}
					else if ((RSB >= 0xC0 && RSB <= 0xCF)) {///program change
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							if (BoolSettings & SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)Track.push_back(file_input.get() & 0);///1st parameter
							else Track.push_back(IO);///////////////////////////////aksjdhkahkfjhak
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							continue;
							//fi.get();///for its single parameter
						}
					}
					else if (RSB >= 0xD0 && RSB <= 0xDF) {///????:D
						if (!(BoolSettings & SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							Track.push_back(IO);
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							continue;
						}
					}
					else {
						ErrorLine = "Track corruption. Pos:" + std::to_string(file_input.tellg());
						//ThrowAlert_Error(ErrorLine);
						RSB = 0;
						DWORD T = 0;
						while (T != 0x2FFF00 && file_input.good() && !file_input.eof()) {
							T = ((T << 8) | file_input.get()) & 0xFFFFFF;
						}
						Track.push_back(0x2F);
						Track.push_back(0xFF);
						Track.push_back(0x00);

						UnLoad.push_back(0x01);
						for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
							for (int i = 0; i < CurHolded[k]; i++) {
								UnLoad.push_back((k & 0xF) | 0x80);
								UnLoad.push_back(k >> 4);
								UnLoad.push_back(0x40);
								UnLoad.push_back(0x00);
							}
							CurHolded[k] = 0;
						}
						UnLoad.pop_back();

						if (!EventCounter && BoolSettings & SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV) {
							WarningLine = "Corrupted track cleared. (" + std::to_string(TrackCount) + ")";
							TrackEnded = true;
						}
						else {
							Track.insert(Track.end() - size - 3, UnLoad.begin(), UnLoad.end());
							UnLoad.clear();
							file_output.put('M'); file_output.put('T'); file_output.put('r'); file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(file_output));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							WarningLine = "An attempt to recover the track. (" + std::to_string(TrackCount++) + ")";
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