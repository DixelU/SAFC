#pragma once
#ifndef SAF_MCTM
#define SAF_MCTM 

void ThrowAlert_Error(std::string&& AlertText);
void ThrowAlert_Warning(std::string&& AlertText);

#include <set>
#include <thread>
#include <ranges>

#include "SMRP2.h"

struct MIDICollectionThreadedMerger
{
	std::wstring SaveTo;
	std::atomic_bool FirstStageComplete;
	std::atomic_bool IntermediateRegularFlag;
	std::atomic_bool IntermediateInplaceFlag;
	std::atomic_bool CompleteFlag;
	std::atomic_bool RemnantsRemove;
	std::uint16_t FinalPPQN;
	std::atomic_uint64_t IRTrackCount;
	std::atomic_uint64_t IITrackCount;

	using proc_data_ptr = 
		std::shared_ptr<single_midi_processor_2::processing_data>;
	using message_buffer_ptr =
		std::shared_ptr<single_midi_processor_2::message_buffers>;
	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> midi_processing_data;

	std::mutex currently_processed_locker;
	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> currently_processed;

	struct DegeneratedTrackIterator
	{
		std::int64_t CurPosition;
		std::int64_t CurTick;
		std::uint8_t* TrackData;
		std::uint64_t TrackSize;
		std::array<std::int64_t, 4096> Holded;
		bool Processing;
		DegeneratedTrackIterator(std::vector<std::uint8_t>& Vec) 
		{
			TrackData = Vec.data();
			TrackSize = Vec.size();
			CurTick = 0;
			CurPosition = 0;
			Processing = true;
			for (auto& a : Holded)
				a = 0;
		}
		std::int64_t TellPoliphony()
		{
			std::int64_t T = 0;
			for (auto& q : Holded)
			{
				T += q;
			}
			return T;
		}
		void SingleEventAdvance()
		{
			if (Processing && CurPosition < TrackSize) 
			{
				std::uint32_t VLV = 0, IO = 0;
				do 
				{
					IO = TrackData[CurPosition];
					CurPosition++;
					VLV = (VLV << 7) | (IO & 0x7F);
				} while (IO & 0x80);
				CurTick += VLV;
				if (TrackData[CurPosition] == 0xFF) 
				{
					CurPosition++;
					if (TrackData[CurPosition] == 0x2F)
					{
						Processing = false;
						CurPosition += 2;
					}
					else
					{
						IO = 0;
						CurPosition++;
						do
						{
							IO = TrackData[CurPosition];
							CurPosition++;
							VLV = (VLV << 7) | (IO & 0x7F);
						} while (IO & 0x80);
						for (int vlvsize = 0; vlvsize < IO; vlvsize++)
							CurPosition++;
					}
				}
				else if (TrackData[CurPosition] >= 0x80 && TrackData[CurPosition] <= 0x9F)
				{
					std::uint16_t FTD = TrackData[CurPosition];
					CurPosition++;
					std::uint16_t KEY = TrackData[CurPosition];
					CurPosition++;
					CurPosition++;//volume - meh;
					std::uint16_t Argument = (KEY << 4) | (FTD & 0xF);
					if (FTD & 0x10) //if noteon
						Holded[Argument]++;
					else if (Holded[Argument] > 0)
					{
						Holded[Argument]--;
					}
				}
				else if ((TrackData[CurPosition] >= 0xA0 && TrackData[CurPosition] <= 0xBF) || (TrackData[CurPosition] >= 0xE0 && TrackData[CurPosition] <= 0xEF))
					CurPosition += 3;
				else if ((TrackData[CurPosition] >= 0xC0 && TrackData[CurPosition] <= 0xDF))
					CurPosition += 2;
				else if (true)
					ThrowAlert_Error("DTI Failure at " + std::to_string(CurPosition) + ". Type: " + 
						std::to_string(TrackData[CurPosition]) + ". Tell developer about it and give him source midi\n");
			}
			else Processing = false;
		}
		bool AdvanceUntilReachingPositionOf(std::int64_t Position)
		{
			while (Processing && CurPosition < Position)
				SingleEventAdvance();
			return Processing;
		}
		void PutCurrentHoldedNotes(std::vector<std::uint8_t>& out, bool output_noteon_wall)
		{
			constexpr std::uint32_t LocalDeltaTimeTrunkEdge = 0xF000000;//BC808000 in vlv
			//bool first_nonzero_delta_output = output_noteon_wall;
			std::uint64_t LocalTick = CurTick;
			std::uint8_t DeltaLen = 0;
			while (output_noteon_wall && LocalTick > LocalDeltaTimeTrunkEdge) 
			{//fillers
				single_midi_processor_2::push_vlv_s(LocalDeltaTimeTrunkEdge, out);
				out.push_back(0xFF);//empty text event
				out.push_back(0x01);
				out.push_back(0x00);
				LocalTick -= LocalDeltaTimeTrunkEdge;
			}
			if (output_noteon_wall)
			{
				DeltaLen = single_midi_processor_2::push_vlv_s(LocalTick, out);
			}
			else
				out.push_back(0);
			for (int i = 0; i < 4096; i++)
			{
				std::int64_t key = Holded[i];
				while (key)
				{
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
	~MIDICollectionThreadedMerger() = default;
	MIDICollectionThreadedMerger(
		std::vector<proc_data_ptr> processing_data, 
		std::uint16_t FinalPPQN, 
		std::wstring SaveTo,
		bool is_console_oriented)
	{
		for (auto& single_midi_data : processing_data)
			midi_processing_data.emplace_back( single_midi_data , std::make_shared<message_buffer_ptr::element_type>(is_console_oriented) );
		this->SaveTo = std::move(SaveTo);
		this->FirstStageComplete = false;
		this->RemnantsRemove = true;
		this->FinalPPQN = FinalPPQN;
		IntermediateRegularFlag = IntermediateInplaceFlag = CompleteFlag = IRTrackCount = IITrackCount = 0;
	}
	void ResetCurProcessing()
	{
		midi_processing_data.clear();
	}
	bool CheckSMRPProcessing()
	{
		for (auto& el : midi_processing_data)
			if (!el.second->finished || el.second->processing)
				return 1;
		return 0;
	}
	bool CheckSMRPProcessingAndStartNextStep()
	{
		if (CheckSMRPProcessing())
			return 1;
		//this->ResetCurProcessing();
		this->Start_RI_Merge();
		return 0;
	}
	bool CheckRIMerge()
	{
		if (IntermediateRegularFlag && IntermediateInplaceFlag)
			FinalMerge();
		else return 0;
		//cout << "Final_Finished\n";
		return 1;
	}
	void StartProcessingMIDIs() 
	{//
		std::set<std::uint32_t> IDs;

		for (auto& el: midi_processing_data) 
			IDs.insert(el.first->settings.details.group_id);

		currently_processed_locker.lock();
		currently_processed.clear();
		currently_processed.resize(IDs.size());
		currently_processed_locker.unlock();
		uint32_t thread_counter = 0;
		
		for (auto Y = IDs.begin(); Y != IDs.end(); Y++)
		{
			std::thread([this](
				std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> processing_data,
				std::uint32_t ID,
				std::uint32_t thread_counter) 
			{
				for (auto& el : processing_data)
				{
					if (el.first->settings.details.group_id != ID)
						continue;

					currently_processed_locker.lock();
					currently_processed[thread_counter] = el;
					currently_processed_locker.unlock();

					if (el.first->settings.proc_details.channel_split)
						single_midi_processor_2::sync_processing<true>(*el.first, *el.second);
					else
						single_midi_processor_2::sync_processing<false>(*el.first, *el.second);
				}
			}, midi_processing_data, *Y, thread_counter).detach();
			thread_counter++;
		}
	}
	void InplaceMerge()
	{
		using mpd_t = decltype(midi_processing_data);
		mpd_t inplace_merge_candidates;
		std::copy_if(midi_processing_data.begin(), midi_processing_data.end(), std::back_inserter(inplace_merge_candidates),
			[](mpd_t::value_type& el) { return el.first->settings.details.inplace_mergable; });
		
		IITrackCount = 0;
		std::thread([](mpd_t IMC, std::uint16_t PPQN, std::wstring _SaveTo, 
				std::reference_wrapper<std::atomic_bool> FinishedFlag,
				std::reference_wrapper<std::atomic_uint64_t> TrackCount)
		{
			if (IMC.empty()) 
			{
				FinishedFlag.get() = true; /// Will this work?
				return;
			}

			bool ActiveStreamFlag = true;
			bool ActiveTrackReading = true;
			std::vector<bbb_ffr*> fiv;

#define pfiv (*fiv[i])

			std::vector<std::int64_t> DecayingDeltaTimes;
#define ddt (DecayingDeltaTimes[i])

			std::vector<std::uint8_t> Track, FrontEdge, BackEdge;
			Track.reserve(1000000);

			bbb_ffr* fi;
			std::ofstream file_output(_SaveTo + L".I.mid", std::ios::binary | std::ios::out);
			file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			for (auto Y = IMC.begin(); Y != IMC.end(); Y++) 
			{
				fi = new bbb_ffr((Y->first->filename + Y->first->postfix).c_str());///
				for (int i = 0; i < 14; i++)
					fi->get();
				DecayingDeltaTimes.push_back(0);
				fiv.push_back(fi);
			}

			file_output.put(TrackCount.get() >> 8);
			file_output.put(TrackCount.get());
			file_output.put(PPQN >> 8);
			file_output.put(PPQN);

			while (ActiveStreamFlag) 
			{
				///reading tracks
				std::uint32_t Header = 0, DIO, DeltaLen = 0;
				std::uint64_t InTrackDelta = 0;
				bool ITD_Flag = 1 /*, NotNoteEvents_ProcessedFlag = 0*/;
				for (int i = 0; i < fiv.size(); i++)
				{
					while (pfiv.good() && Header != MTrk) 
						Header = (Header << 8) | pfiv.get();

					for (int w = 0; w < 4; w++)
						pfiv.get();
					Header = 0;
					if (pfiv.good())
						ddt = 0;
					else
						ddt = -1;
				}

				for (std::uint64_t Tick = 0; ActiveTrackReading; Tick++, InTrackDelta++)
				{
					std::uint8_t IO = 0, EVENTTYPE = 0;///yas
					ActiveTrackReading = 0;
					ActiveStreamFlag = 0;
					for (int i = 0; i < fiv.size(); i++)
					{
						///every stream
						if (ddt == 0) {///there will be parser...
							if (Tick)
								goto eventevalution;

						deltatime_reading:

							DIO = 0;
							do 
							{
								IO = pfiv.get();
								DIO = (DIO << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (pfiv.eof())
								goto trackending;
							if (ddt = DIO)
								goto escape;

						eventevalution:

							EVENTTYPE = (pfiv.get());

							DeltaLen = single_midi_processor_2::push_vlv(InTrackDelta, Track);
							InTrackDelta = 0;

							if (EVENTTYPE == 0xFF)
							{///meta
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								if (Track.back() == 0x2F)
								{
									pfiv.get();
									for (int l = 0; l < DeltaLen + 2; l++)
										Track.pop_back();

									goto trackending;
								}
								else 
								{
									DIO = 0;
									do 
									{
										IO = pfiv.get();
										Track.push_back(IO);
										DIO = (DIO << 7) | (IO & 0x7F);
									} while (IO & 0x80);

									for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
										Track.push_back(pfiv.get());
								}
							}
							else if (EVENTTYPE == 0xF0 || EVENTTYPE == 0xF7)
							{
								Track.push_back(EVENTTYPE);
								DIO = 0;
								do 
								{
									IO = pfiv.get();
									Track.push_back(IO);
									DIO = (DIO << 7) | (IO & 0x7F);
								} while (IO & 0x80);

								for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
									Track.push_back(pfiv.get());
							}
							else if ((EVENTTYPE >= 0x80 && EVENTTYPE <= 0xBF) || (EVENTTYPE >= 0xE0 && EVENTTYPE <= 0xEF))
							{
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								Track.push_back(pfiv.get());
							}
							else if ((EVENTTYPE >= 0xC0 && EVENTTYPE <= 0xDF))
							{
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
							}
							else 
							{   ///RSB CANNOT APPEAR IN THIS STAGE
								auto pos = pfiv.tellg();
								std::cout << "Inplace error @" << std::hex << pos << std::dec << std::endl;

								ThrowAlert_Error("DTI Failure at " + std::to_string(pos) + ". Type: " +
									std::to_string(EVENTTYPE) + ". Tell developer about it and give him source midis.\n");

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
						else if (ddt > 0) 
						{
							ActiveTrackReading = 1;
							ddt--;
							continue;
						}
						else 
						{
							if (pfiv.good())
							{
								ActiveStreamFlag = 1; 
							}
							continue;
						}
						///while it's zero delta time
					}
					if (!ActiveTrackReading && !Track.empty()) 
					{

						DeltaLen = single_midi_processor_2::push_vlv(InTrackDelta, Track);
						InTrackDelta = 0;

						Track.push_back(0xFF);
						Track.push_back(0x2F);
						Track.push_back(0x00);
					}
				}
				ActiveTrackReading = 1;
				constexpr std::uint32_t EDGE = 0x7F000000;

				if (Track.size() > 0xFFFFFFFFu)
					std::cout << "TrackSize overflow!!!\n";
				else if (Track.empty())
					continue;

				if (Track.size() > EDGE * 2) 
				{
					std::int64_t TotalShift = EDGE, PrevEdgePos = 0;
					DegeneratedTrackIterator DTI(Track);
					FrontEdge.clear();
					while (DTI.AdvanceUntilReachingPositionOf(TotalShift))
					{
						TotalShift = DTI.CurPosition + EDGE;
						BackEdge.clear();
						DTI.PutCurrentHoldedNotes(BackEdge, false);
						std::uint64_t TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos) + BackEdge.size() + 4;
						file_output << "MTrk";
						file_output.put(TotalSize >> 24);
						file_output.put(TotalSize >> 16);
						file_output.put(TotalSize >> 8);
						file_output.put(TotalSize);

						single_midi_processor_2::ostream_write(FrontEdge, file_output);
						single_midi_processor_2::ostream_write(Track, Track.begin() + PrevEdgePos, Track.begin() + DTI.CurPosition, file_output);
						single_midi_processor_2::ostream_write(BackEdge, BackEdge.begin(), BackEdge.end(), file_output);

						file_output.put(0);//that's why +4
						file_output.put(0xFF);
						file_output.put(0x2F);
						file_output.put(0);
						++(TrackCount.get());
						PrevEdgePos = DTI.CurPosition;
						FrontEdge.clear();
						DTI.PutCurrentHoldedNotes(FrontEdge, true);
					}
					file_output << "MTrk";
					std::uint64_t TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos);
					//cout << "Outside:" << TotalSize << endl;
					file_output.put(TotalSize >> 24);
					file_output.put(TotalSize >> 16);
					file_output.put(TotalSize >> 8);
					file_output.put(TotalSize);

					single_midi_processor_2::ostream_write(FrontEdge, file_output);
					single_midi_processor_2::ostream_write(Track, Track.begin() + PrevEdgePos, Track.end(), file_output);

					FrontEdge.clear();
					BackEdge.clear();
					++(TrackCount.get());
				}
				else
				{
					file_output << "MTrk";
					file_output.put(Track.size() >> 24);
					file_output.put(Track.size() >> 16);
					file_output.put(Track.size() >> 8);
					file_output.put(Track.size());

					single_midi_processor_2::ostream_write(Track, file_output);
					//copy(Track.begin(), Track.end(), ostream_iterator<std::uint8_t>(file_output));
					++(TrackCount.get());
				}
				Track.clear();
			}
			for (int i = 0; i < fiv.size(); i++)
			{
				pfiv.close();
				auto& imc_i = IMC[i];
				if (imc_i.first->settings.proc_details.remove_remnants)
					_wremove((imc_i.first->filename + imc_i.first->postfix).c_str());
			}
			file_output.seekp(10, std::ios::beg);
			file_output.put((TrackCount.get()) >> 8);
			file_output.put((TrackCount.get()) & 0xff);
			file_output.close();
			for (auto& t : fiv)
				delete t;
			printf("Inplace: finished\n");
			FinishedFlag.get() = true;
		}, inplace_merge_candidates, FinalPPQN, SaveTo, std::ref(IntermediateInplaceFlag), std::ref(IITrackCount)).detach();
	}
	void RegularMerge()
	{
		using mpd_t = decltype(midi_processing_data);
		mpd_t regular_merge_candidates;
		std::copy_if(midi_processing_data.begin(), midi_processing_data.end(), std::back_inserter(regular_merge_candidates),
			[](mpd_t::value_type& el) { return !el.first->settings.details.inplace_mergable; });

		if (regular_merge_candidates.size() == 1)
		{
			auto& current_merge_candidate = regular_merge_candidates.front();
			std::wstring filename = 
				current_merge_candidate.first->filename +
				current_merge_candidate.first->postfix;
			auto save_to_with_postfix = SaveTo + L".R.mid";
			_wremove(save_to_with_postfix.c_str());
			auto result = _wrename(filename.c_str(), save_to_with_postfix.c_str());

			IRTrackCount += current_merge_candidate.first->tracks_count;
			IntermediateRegularFlag = true; /// Will this work?
		}
		else
		{
			std::thread([](mpd_t RMC, std::uint16_t PPQN, std::wstring _SaveTo,
				std::reference_wrapper<std::atomic_bool> FinishedFlag, std::reference_wrapper<std::atomic_uint64_t> TrackCount)
				{
					if (RMC.empty())
					{
						FinishedFlag.get() = true;
						return;
					}
					//bool FirstFlag = 1;
					const size_t buffer_size = 20000000;
					std::uint8_t* buffer = new std::uint8_t[buffer_size];
					std::ofstream file_output(_SaveTo + L".R.mid", std::ios::binary | std::ios::out);
					std::wstring filename = RMC.front().first->filename + RMC.front().first->postfix;
					bbb_ffr file_input(filename.c_str());
					file_output.rdbuf()->pubsetbuf((char*)buffer, buffer_size);
					file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
					file_output.put(0);
					file_output.put(0);
					file_output.put(PPQN >> 8);
					file_output.put(PPQN);
					for (auto Y = RMC.begin(); Y != RMC.end(); Y++)
					{
						filename = Y->first->filename + Y->first->postfix;
						if (Y != RMC.begin())
							file_input.reopen_next_file(filename.c_str());
						for (int i = 0; i < 14; i++)
							file_input.get();
						file_input.put_into_ostream(file_output);
						TrackCount.get() += Y->first->tracks_count;
						int t;
						if (Y->first->settings.proc_details.remove_remnants)
							t = _wremove(filename.c_str());
					}
					file_output.seekp(10, std::ios::beg);
					file_output.put(TrackCount.get() >> 8);
					file_output.put(TrackCount.get());
					FinishedFlag.get() = true; /// Will this work?
					file_output.flush();
					file_output.close();
					delete[] buffer;
				}, regular_merge_candidates, FinalPPQN, SaveTo, std::ref(IntermediateRegularFlag), std::ref(IRTrackCount)).detach();
		}

	}
	void Start_RI_Merge() 
	{
		RegularMerge();
		InplaceMerge();
	}
	void FinalMerge() 
	{
		std::thread([this](std::reference_wrapper<std::atomic_bool> FinishedFlag, std::wstring _SaveTo)
		{
			bbb_ffr 
				*IM = new bbb_ffr((_SaveTo + L".I.mid").c_str()),
				*RM = new bbb_ffr((_SaveTo + L".R.mid").c_str());
			std::ofstream F(_SaveTo, std::ios::binary | std::ios::out);
			bool IMgood = !IM->eof(), RMgood = !RM->eof();

			auto TotalTracks = IITrackCount.load() + IRTrackCount.load();
			if ((~0xFFFFULL) & TotalTracks)
			{
				printf("Track count overflow: %llu\n", TotalTracks);
				TotalTracks = 0xFFFF;
			}

			if (!IMgood || !RMgood)
			{
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
				FinishedFlag.get() = true;
				return;
			}

			printf("Active merging at last stage (untested)\n");

			std::uint16_t T = 0;
			std::uint8_t A = 0, B = 0;

			F.put('M');
			F.put('T');
			F.put('h');
			F.put('d');
			F.put(0);
			F.put(0);
			F.put(0);
			F.put(6);
			F.put(0);
			F.put(1);
			for (int i = 0; i < 12; i++)
				IM->get();
			for (int i = 0; i < 12; i++)
				RM->get();
			T = TotalTracks;
			A = T >> 8;
			B = T;
			F.put(A);
			F.put(B);

			IM->get();
			IM->get();
			F.put(RM->get());
			F.put(RM->get());

			IM->put_into_ostream(F);
			RM->put_into_ostream(F);

			IM->close();
			RM->close();

			delete IM;
			delete RM;

			FinishedFlag.get() = true;
			if (RemnantsRemove)
			{
				_wremove((_SaveTo + L".I.mid").c_str());
				_wremove((_SaveTo + L".R.mid").c_str());
			}
		}, std::ref(CompleteFlag), SaveTo).detach();
	}
};

#endif 