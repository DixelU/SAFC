#pragma once
#ifndef SAF_MCTM
#define SAF_MCTM 

void ThrowAlert_Error(std::string&& AlertText);
void ThrowAlert_Warning(std::string&& AlertText);

#include <set>
#include <thread>

#include "SMRP.h"

struct MIDICollectionThreadedMerger {
	std::wstring SaveTo;
	bool FirstStageComplete;
	bool IntermediateRegularFlag;
	bool IntermediateInplaceFlag;
	bool CompleteFlag;
	bool RemnantsRemove;
	std::uint16_t FinalPPQN;
	std::uint16_t IRTrackCount;
	std::uint16_t IITrackCount;
	std::vector<SingleMIDIReProcessor*> SMRP, Cur_Processing;
	struct DegeneratedTrackIterator {
		std::int64_t CurPosition;
		std::int64_t CurTick;
		std::uint8_t* TrackData;
		UINT64 TrackSize;
		std::array<std::int64_t, 4096> Holded;
		bool Processing;
		DegeneratedTrackIterator(std::vector<std::uint8_t>& Vec) {
			TrackData = Vec.data();
			TrackSize = Vec.size();
			CurTick = 0;
			CurPosition = 0;
			Processing = true;
			for (auto& a : Holded)
				a = 0;
		}
		std::int64_t TellPoliphony() {
			std::int64_t T = 0;
			for (auto& q : Holded) {
				T += q;
			}
			return T;
		}
		void SingleEventAdvance() {
			if (Processing && CurPosition < TrackSize) {
				std::uint32_t VLV = 0, IO = 0;
				do {
					IO = TrackData[CurPosition];
					CurPosition++;
					VLV = (VLV << 7) | (IO & 0x7F);
				} while (IO & 0x80);
				CurTick += VLV;
				if (TrackData[CurPosition] == 0xFF) {
					CurPosition++;
					if (TrackData[CurPosition] == 0x2F) {
						Processing = false;
						CurPosition += 2;
					}
					else {
						IO = 0;
						CurPosition++;
						do {
							IO = TrackData[CurPosition];
							CurPosition++;
							VLV = (VLV << 7) | (IO & 0x7F);
						} while (IO & 0x80);
						for (int vlvsize = 0; vlvsize < IO; vlvsize++)
							CurPosition++;
					}
				}
				else if (TrackData[CurPosition] >= 0x80 && TrackData[CurPosition] <= 0x9F) {
					std::uint16_t FTD = TrackData[CurPosition];
					CurPosition++;
					std::uint16_t KEY = TrackData[CurPosition];
					CurPosition++;
					CurPosition++;//volume - meh;
					std::uint16_t Argument = (KEY << 4) | (FTD & 0xF);
					if (FTD & 0x10) //if noteon
						Holded[Argument]++;
					else if (Holded[Argument] > 0) {
						Holded[Argument]--;
					}
				}
				else if ((TrackData[CurPosition] >= 0xA0 && TrackData[CurPosition] <= 0xBF) || (TrackData[CurPosition] >= 0xE0 && TrackData[CurPosition] <= 0xEF))
					CurPosition += 3;
				else if ((TrackData[CurPosition] >= 0xC0 && TrackData[CurPosition] <= 0xDF))
					CurPosition += 2;
				else if (true)
					ThrowAlert_Error("DTI Failure at " + std::to_string(CurPosition) + ". Type: " + std::to_string(TrackData[CurPosition]) + ". Tell developer about it and give him source midi\n");
			}
			else Processing = false;
		}
		bool AdvanceUntilReachingPositionOf(std::int64_t Position) {
			while (Processing && CurPosition < Position)
				SingleEventAdvance();
			return Processing;
		}
		void PutCurrentHoldedNotes(std::vector<std::uint8_t>& out, bool output_noteon_wall) {
			constexpr std::uint32_t LocalDeltaTimeTrunkEdge = 0xF000000;//BC808000 in vlv
			//bool first_nonzero_delta_output = output_noteon_wall;
			UINT64 LocalTick = CurTick;
			std::uint8_t DeltaLen = 0;
			while (output_noteon_wall && LocalTick > LocalDeltaTimeTrunkEdge) {//fillers
				SingleMIDIReProcessor::SingleMIDIReProcessor::push_vlv(LocalDeltaTimeTrunkEdge, out);
				out.push_back(0xFF);//empty text event
				out.push_back(0x01);
				out.push_back(0x00);
				LocalTick -= LocalDeltaTimeTrunkEdge;
			}
			if (output_noteon_wall) {
				DeltaLen = SingleMIDIReProcessor::push_vlv(LocalTick, out);
			}
			else
				out.push_back(0);
			for (int i = 0; i < 4096; i++) {
				std::int64_t key = Holded[i];
				while (key) {
					out.push_back((0x10 * output_noteon_wall) | (i & 0xF) | 0x80);//note data;
					out.push_back(i >> 4);//key;
					out.push_back(1);//almost silent
					out.push_back(0);//delta time
					key--;
				}
			}
			if (out.size())
				out.pop_back();
		}
	};
	~MIDICollectionThreadedMerger() {
		ResetEverything();
	}
	MIDICollectionThreadedMerger(std::vector<SingleMIDIReProcessor*> SMRP, std::uint16_t FinalPPQN, std::wstring SaveTo) {
		this->SaveTo = SaveTo;
		this->SMRP = SMRP;
		this->FinalPPQN = FinalPPQN;
		IntermediateRegularFlag = IntermediateInplaceFlag = CompleteFlag = IRTrackCount = IITrackCount = 0;
	}
	void ResetCurProcessing() {
		for (int i = 0; i < Cur_Processing.size(); i++) {
			Cur_Processing[i] = NULL;
		}
	}
	void ResetEverything() {
		IntermediateRegularFlag = IntermediateInplaceFlag = CompleteFlag = IRTrackCount = IITrackCount = 0;
		Cur_Processing.clear();
		for (int i = 0; i < SMRP.size(); i++) {
			delete SMRP[i];
		}
		SMRP.clear();
	}
	bool CheckSMRPFinishedFlags() {
		for (int i = 0; i < SMRP.size(); i++)
			if (!SMRP[i]->Finished)return 0;
		this->ResetCurProcessing();
		this->Start_RI_Merge();
		return 1;
	}
	bool CheckRIMerge() {
		if (IntermediateRegularFlag && IntermediateInplaceFlag)
			FinalMerge();
		else return 0;
		//cout << "Final_Finished\n";
		return 1;
	}
	void ProcessMIDIs() {//
		std::set<std::uint32_t> IDs;
		std::uint32_t Counter = 0;
		std::vector<SingleMIDIReProcessor*> SMRPv;
		for (int i = 0; i < SMRP.size(); i++) {
			IDs.insert((SMRP[i])->ThreadID);
		}
		Cur_Processing = std::vector<SingleMIDIReProcessor*>(IDs.size(), NULL);
		for (auto Y = IDs.begin(); Y != IDs.end(); Y++) {
			for (int i = 0; i < SMRP.size(); i++) {
				if (*Y == (SMRP[i])->ThreadID) {
					SMRPv.push_back(SMRP[i]);
				}
			}
			std::thread TH([this](std::vector<SingleMIDIReProcessor*> SMRPv, std::uint32_t Counter) {
				for (int i = 0; i < SMRPv.size(); i++) {
					this->Cur_Processing[Counter] = SMRPv[i];
					SMRPv[i]->ReProcess();
				}
				this->Cur_Processing[Counter] = NULL;
				}, SMRPv, Counter++);
			TH.detach();
			SMRPv.clear();
		}
	}
	void InplaceMerge() {
		std::vector<SingleMIDIReProcessor*>* InplaceMergeCandidats = new std::vector<SingleMIDIReProcessor*>;
		for (int i = 0; i < SMRP.size(); i++)
			if (SMRP[i]->InQueueToInplaceMerge)InplaceMergeCandidats->push_back(SMRP[i]);
		IITrackCount = 0;
		std::thread IMC_Processor([](std::vector<SingleMIDIReProcessor*>* IMC, bool* FinishedFlag, std::uint16_t PPQN, std::wstring _SaveTo, std::uint16_t* TrackCount) {
			if (IMC->empty()) {
				delete IMC;
				(*FinishedFlag) = 1; /// Will this work?
				return;
			}
			bool ActiveStreamFlag = 1;
			bool ActiveTrackReading = 1;
			std::vector<bbb_ffr*> fiv;
#define pfiv (*fiv[i])
			std::vector<std::int64_t> DecayingDeltaTimes;
#define ddt (DecayingDeltaTimes[i])
			std::vector<std::uint8_t> Track, FrontEdge, BackEdge;
			bbb_ffr* fi;
			std::ofstream file_output(_SaveTo + L".I.mid", std::ios::binary | std::ios::out);
			file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			for (auto Y = IMC->begin(); Y != IMC->end(); Y++) {
				fi = new bbb_ffr(((*Y)->FileName + (*Y)->Postfix).c_str());///
				//if ((*Y)->TrackCount > *TrackCount)*TrackCount = (*Y)->TrackCount;
				for (int i = 0; i < 14; i++)
					fi->get();
				DecayingDeltaTimes.push_back(0);
				fiv.push_back(fi);
			}
			file_output.put(*TrackCount >> 8);
			file_output.put(*TrackCount);
			file_output.put(PPQN >> 8);
			file_output.put(PPQN);
			while (ActiveStreamFlag) {
				///reading tracks
				std::uint32_t Header = 0, DIO, DeltaLen = 0;
				//std::uint32_t POS = 0;
				UINT64 InTrackDelta = 0;
				bool ITD_Flag = 1 /*, NotNoteEvents_ProcessedFlag = 0*/;
				for (int i = 0; i < fiv.size(); i++) {
					while (pfiv.good() && Header != MTrk) {
						Header = (Header << 8) | pfiv.get();
					}
					for (int w = 0; w < 4; w++)
						pfiv.get();///adlakjfkljsdflsdkf
					Header = 0;
					if (pfiv.good())
						ddt = 0;
					else
						ddt = -1;
				}
				for (UINT64 Tick = 0; ActiveTrackReading; Tick++, InTrackDelta++) {
					std::uint8_t IO = 0, EVENTTYPE = 0;///yas
					ActiveTrackReading = 0;
					ActiveStreamFlag = 0;
					//NotNoteEvents_ProcessedFlag = 0;
					for (int i = 0; i < fiv.size(); i++) {
						///every stream
						if (ddt == 0) {///there will be parser...
							if (Tick)goto eventevalution;
						deltatime_reading:
							DIO = 0;
							do {
								IO = pfiv.get();
								DIO = (DIO << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (pfiv.eof())goto trackending;
							if (ddt = DIO)goto escape;
						eventevalution:///becasue why not
							EVENTTYPE = (pfiv.get());

							DeltaLen = SingleMIDIReProcessor::push_vlv(InTrackDelta, Track);
							InTrackDelta = 0;

							if (EVENTTYPE == 0xFF) {///meta
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								if (Track.back() == 0x2F) {
									pfiv.get();
									for (int l = 0; l < DeltaLen + 2; l++)
										Track.pop_back();
									goto trackending;
								}
								else {
									DIO = 0;
									do {
										IO = pfiv.get();
										Track.push_back(IO);
										DIO = (DIO << 7) | (IO & 0x7F);
									} while (IO & 0x80);
									for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
										Track.push_back(pfiv.get());
								}
							}
							else if (EVENTTYPE == 0xF0 || EVENTTYPE == 0xF7) {
								Track.push_back(EVENTTYPE);
								DIO = 0;
								do {
									IO = pfiv.get();
									Track.push_back(IO);
									DIO = (DIO << 7) | (IO & 0x7F);
								} while (IO & 0x80);
								for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
									Track.push_back(pfiv.get());
							}
							else if ((EVENTTYPE >= 0x80 && EVENTTYPE <= 0xBF) || (EVENTTYPE >= 0xE0 && EVENTTYPE <= 0xEF)) {
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								Track.push_back(pfiv.get());
							}
							else if ((EVENTTYPE >= 0xC0 && EVENTTYPE <= 0xDF)) {
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
							}
							else {///RSB CANNOT APPEAR ON THIS STAGE
								printf("Inplace error %X\n", pfiv.tellg());
								Track.push_back(0xCA);
								Track.push_back(0);
								goto trackending;
							}
							goto deltatime_reading;
						trackending:
							ddt = -1;
							if (!pfiv.eof())
								ActiveStreamFlag = 1;
							continue;
						escape:
							ActiveTrackReading = 1;
							ddt--;
							continue;
						//file_eof:
							ddt = -1;
							continue;
						}
						else if (ddt > 0) {
							ActiveTrackReading = 1;
							ddt--;
							continue;
						}
						else {
							if (pfiv.good()) {
								ActiveStreamFlag = 1;
							}
							continue;
						}
						///while it's zero delta time
					}
					if (!ActiveTrackReading && !Track.empty()) {

						DeltaLen = SingleMIDIReProcessor::push_vlv(InTrackDelta, Track);
						InTrackDelta = 0;

						Track.push_back(0xFF);
						Track.push_back(0x2F);
						Track.push_back(0x00);
					}
				}
				ActiveTrackReading = 1;
				constexpr UINT32 EDGE = 0x7F000000;
				if (Track.size() > 0xFFFFFFFFu)
					std::cout << "TrackSize overflow!!!\n";
				else if (Track.empty())continue;
				if (Track.size() > EDGE * 2) {
					std::int64_t TotalShift = EDGE, PrevEdgePos = 0;
					DegeneratedTrackIterator DTI(Track);
					FrontEdge.clear();
					while (DTI.AdvanceUntilReachingPositionOf(TotalShift)) {
						TotalShift = DTI.CurPosition + EDGE;
						BackEdge.clear();
						DTI.PutCurrentHoldedNotes(BackEdge, false);
						UINT64 TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos) + BackEdge.size() + 4;
						file_output << "MTrk";
						file_output.put(TotalSize >> 24);
						file_output.put(TotalSize >> 16);
						file_output.put(TotalSize >> 8);
						file_output.put(TotalSize);

						SingleMIDIReProcessor::ostream_write(FrontEdge, file_output);
						SingleMIDIReProcessor::ostream_write(Track, Track.begin() + PrevEdgePos, Track.begin() + DTI.CurPosition, file_output);
						SingleMIDIReProcessor::ostream_write(BackEdge, BackEdge.begin(), BackEdge.end(), file_output);

						file_output.put(0);//that's why +4
						file_output.put(0xFF);
						file_output.put(0x2F);
						file_output.put(0);
						(*TrackCount)++;
						PrevEdgePos = DTI.CurPosition;
						FrontEdge.clear();
						DTI.PutCurrentHoldedNotes(FrontEdge, true);
					}
					file_output << "MTrk";
					UINT64 TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos);
					//cout << "Outside:" << TotalSize << endl;
					file_output.put(TotalSize >> 24);
					file_output.put(TotalSize >> 16);
					file_output.put(TotalSize >> 8);
					file_output.put(TotalSize);

					SingleMIDIReProcessor::ostream_write(FrontEdge, file_output);
					SingleMIDIReProcessor::ostream_write(Track, Track.begin() + PrevEdgePos, Track.end(), file_output);

					FrontEdge.clear();
					BackEdge.clear();
					(*TrackCount)++;
				}
				else {
					file_output << "MTrk";
					file_output.put(Track.size() >> 24);
					file_output.put(Track.size() >> 16);
					file_output.put(Track.size() >> 8);
					file_output.put(Track.size());

					SingleMIDIReProcessor::ostream_write(Track, file_output);
					//copy(Track.begin(), Track.end(), ostream_iterator<std::uint8_t>(file_output));
					(*TrackCount)++;
				}
				Track.clear();
			}
			for (int i = 0; i < fiv.size(); i++) {
				pfiv.close();
				if ((*IMC)[i]->BoolSettings & _BoolSettings::remove_remnants)
					_wremove(((((*IMC)[i]->FileName) + ((*IMC)[i]->Postfix)).c_str()));
			}
			file_output.seekp(10, std::ios::beg);
			file_output.put((*TrackCount) >> 8);
			file_output.put((*TrackCount) & 0xff);
			file_output.close();
			for (auto& t : fiv) {
				delete t;
			}
			printf("Inplace: finished\n");
			(*FinishedFlag) = 1; /// Will this work?
			delete IMC;
			}, InplaceMergeCandidats, &IntermediateInplaceFlag, FinalPPQN, SaveTo, &IITrackCount);
		IMC_Processor.detach();
	}
	void RegularMerge() {
		std::vector<SingleMIDIReProcessor*>* RegularMergeCandidats = new std::vector<SingleMIDIReProcessor*>;
		for (int i = 0; i < SMRP.size(); i++)
			if (!SMRP[i]->InQueueToInplaceMerge)RegularMergeCandidats->push_back(SMRP[i]);
		IRTrackCount = 0;
		std::thread RMC_Processor([](std::vector<SingleMIDIReProcessor*>* RMC, bool* FinishedFlag, std::uint16_t PPQN, std::wstring _SaveTo, std::uint16_t* TrackCount) {
			if (RMC->empty()) {
				(*FinishedFlag) = 1; /// Will this work?
				return;
			}
			//bool FirstFlag = 1;
			const size_t buffer_size = 20000000;
			std::uint8_t* buffer = new std::uint8_t[buffer_size];
			std::ofstream file_output(_SaveTo + L".R.mid", std::ios::binary | std::ios::out);
			bbb_ffr file_input(((*(RMC->begin()))->FileName + (*(RMC->begin()))->Postfix).c_str());
			std::wstring filename = ((*(RMC->begin()))->FileName + (*(RMC->begin()))->Postfix);
			file_output.rdbuf()->pubsetbuf((char*)buffer, buffer_size);
			file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			file_output.put(0);
			file_output.put(0);
			file_output.put(PPQN >> 8);
			file_output.put(PPQN);
			for (auto Y = RMC->begin(); Y != RMC->end(); Y++) {
				filename = (*Y)->FileName + (*Y)->Postfix;
				if (Y != RMC->begin())
					file_input.reopen_next_file(filename.c_str());
				*TrackCount += (*Y)->TrackCount;
				for (int i = 0; i < 14; i++)
					file_input.get();
				file_input.put_into_ostream(file_output);
				int t;
				if ((*Y)->BoolSettings & _BoolSettings::remove_remnants)
					t = _wremove(filename.c_str());
			}
			file_output.seekp(10, std::ios::beg);
			file_output.put(*TrackCount >> 8);
			file_output.put(*TrackCount);
			(*FinishedFlag) = true; /// Will this work?
			file_output.close();
			delete RMC;
			delete[] buffer;
			}, RegularMergeCandidats, &IntermediateRegularFlag, FinalPPQN, SaveTo, &IRTrackCount);
		RMC_Processor.detach();
	}
	void Start_RI_Merge() {
		RegularMerge();
		InplaceMerge();
	}
	void FinalMerge() {
		std::thread RMC_Processor([this](bool* FinishedFlag, std::wstring _SaveTo) {
			bbb_ffr* IM = new bbb_ffr((_SaveTo + L".I.mid").c_str()),
				* RM = new bbb_ffr((_SaveTo + L".R.mid").c_str());
			std::ofstream F(_SaveTo, std::ios::binary | std::ios::out);
			bool IMgood = !IM->eof(), RMgood = !RM->eof();
			if (!IMgood || !RMgood) {
				IM->close();
				RM->close();
				F.close();

				auto inplaceFilename = _SaveTo + L".I.mid";
				auto regularFilename = _SaveTo + L".R.mid";

				auto remove = _wremove(_SaveTo.c_str());
				auto remove_i = _wrename(inplaceFilename.c_str(), _SaveTo.c_str());
				auto remove_r = _wrename(regularFilename.c_str(), _SaveTo.c_str());//one of these will not work

				printf("S2 status: %i\nI status: %i\nR status: %i\n", remove, remove_i, remove_r);

				delete IM;
				delete RM;
				printf("Escaped last stage\n");
				*FinishedFlag = true;
				return;
			}
			printf("Active merging at last stage (untested)\n");
			std::uint16_t T = 0;
			std::uint8_t A = 0, B = 0;
			F << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			for (int i = 0; i < 10; i++)
				IM->get();
			for (int i = 0; i < 10; i++)
				RM->get();
			A = IM->get();
			B = IM->get();
			T = (A + RM->get()) << 8;
			T += (B + RM->get());
			A = T >> 8;
			B = T;
			F.put(A);
			F.put(B);
			F.put(IM->get());
			F.put(IM->get());
			for (int i = 0; i < 4; i++)
				RM->get();
			IM->put_into_ostream(F);
			RM->put_into_ostream(F);
			IM->close();
			RM->close();
			delete IM;
			delete RM;
			*FinishedFlag = true;
			if (RemnantsRemove) {
				_wremove((_SaveTo + L".I.mid").c_str());
				_wremove((_SaveTo + L".R.mid").c_str());
			}
			}, &CompleteFlag, SaveTo);
		RMC_Processor.detach();
	}
};

#endif 