#pragma once
#include "header_utils.h"
#ifndef SAF_MCTM
#define SAF_MCTM

#include <set>
#include <thread>
//#include <ranges>

#include "SMRP2.h"

struct midi_collection_threaded_merger
{
	std_unicode_string save_to;
	std::atomic_bool first_stage_complete;
	std::atomic_bool intermediate_regular_flag;
	std::atomic_bool intermediate_inplace_flag;
	std::atomic_bool complete_flag;
	std::atomic_bool remnants_remove;
	std::uint16_t final_ppqn;
	std::atomic_uint64_t ir_track_count;
	std::atomic_uint64_t ii_track_count;

	using proc_data_ptr = 
		std::shared_ptr<single_midi_processor_2::processing_data>;
	using message_buffer_ptr =
		std::shared_ptr<single_midi_processor_2::message_buffers>;
	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> processing_data;

	std::mutex currently_processed_locker;
	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> currently_processing;

	struct normalised_track_iterator
	{
		std::int64_t current_position;
		std::int64_t current_tick;

		std::uint8_t* track_data;
		std::uint64_t track_size;

		std::array<std::int64_t, 4096> held;
		bool processing;

		normalised_track_iterator(std::vector<std::uint8_t>& track_data) :
			current_position(0),
			current_tick(0),
			track_data(track_data.data()),
			track_size(track_data.size()),
			held(),
			processing(true)
		{
			for (auto& voices : this->held)
				voices = 0;
		}

		std::int64_t tell_polyphony()
		{
			std::int64_t total = 0;
			for (auto& voices : this->held)
				total += voices;
			return total;
		}

		void single_event_advance()
		{
			if (!(this->processing && this->current_position < this->track_size)) 
			{
				this->processing = false;
				return;
			}

			std::uint32_t vlv = 0, io = 0;
			do 
			{
				io = this->track_data[this->current_position];
				this->current_position++;
				vlv = (vlv << 7) | (io & 0x7F);
			} while (io & 0x80);
			
			this->current_tick += vlv;

			if (this->track_data[this->current_position] == 0xFF) 
			{
				current_position++;
				if (this->track_data[this->current_position] == 0x2F)
				{
					this->processing = false;
					this->current_position += 2;
				}
				else
				{
					io = 0;
					this->current_position++;

					do
					{
						io = this->track_data[this->current_position];
						this->current_position++;
						vlv = (vlv << 7) | (io & 0x7F);
					}
					while (io & 0x80);

					for (std::uint32_t vlvsize = 0; vlvsize < io; vlvsize++)
						this->current_position++;
				}
			}
			else if (this->track_data[this->current_position] >= 0x80 && this->track_data[this->current_position] <= 0x9F)
			{
				std::uint16_t ftd = this->track_data[this->current_position];
				this->current_position++;
				std::uint16_t key = this->track_data[this->current_position];
				this->current_position++;
				// volume is not used
				this->current_position++;

				std::uint16_t argument = (key << 4) | (ftd & 0xF);
				if (ftd & 0x10) //if noteon
					this->held[argument]++;
				else if (this->held[argument] > 0)
					this->held[argument]--;

			}
			else if ((this->track_data[this->current_position] >= 0xA0 && this->track_data[this->current_position] <= 0xBF) || (this->track_data[this->current_position] >= 0xE0 && this->track_data[this->current_position] <= 0xEF))
				this->current_position += 3;
			else if ((this->track_data[this->current_position] >= 0xC0 && this->track_data[this->current_position] <= 0xDF))
				this->current_position += 2;
			else if (true)
				ThrowAlert_Error("NTI Failure at " + std::to_string(this->current_position) +
						". Type: " + std::to_string(this->track_data[this->current_position]) +
						". Tell developers about it and give them source midi\n");
		}

		bool advance_until_reaching_position_of(std::int64_t position)
		{
			while (this->processing && this->current_position < position)
				this->single_event_advance();
			return this->processing;
		}

		void put_current_held_notes(std::vector<std::uint8_t>& out, bool output_noteon_wall)
		{
			constexpr std::uint32_t delta_time_trunk_edge = 0xF000000;//BC808000 in vlv
			//bool first_nonzero_delta_output = output_noteon_wall;
			std::uint64_t local_tick = this->current_tick;
			std::uint8_t delta_len = 0;
			while (output_noteon_wall && local_tick > delta_time_trunk_edge) 
			{
				//fillers
				single_midi_processor_2::push_vlv_s(delta_time_trunk_edge, out);
				out.push_back(0xFF);//empty text event
				out.push_back(0x01);
				out.push_back(0x00);
				local_tick -= delta_time_trunk_edge;
			}

			if (output_noteon_wall)
				delta_len = single_midi_processor_2::push_vlv_s(local_tick, out);
			else
				out.push_back(0);

			for (std::uint32_t key = 0; key < 4096; key++)
			{
				std::int64_t voices = this->held[key];
				while (voices)
				{
					// todo: use copy back trait from smrp2
					out.push_back((0x10 * output_noteon_wall) | (key & 0xF) | 0x80); //note data;
					out.push_back(key >> 4);
					out.push_back(1); //almost silent
					out.push_back(0); //delta time (positioned inversely)
					voices--;
				}
			}

			if (!out.empty())
				out.pop_back();
		}
	};

	~midi_collection_threaded_merger() = default;
	midi_collection_threaded_merger(
		std::vector<proc_data_ptr> processing_data, 
		std::uint16_t final_ppq, 
		std_unicode_string save_to,
		bool is_console_oriented)
	{
		for (auto& single_midi_data : processing_data)
			this->processing_data.emplace_back( single_midi_data , std::make_shared<message_buffer_ptr::element_type>(is_console_oriented) );
		this->save_to = std::move(save_to);
		this->first_stage_complete = false;
		this->remnants_remove = true;
		this->final_ppqn = final_ppq;
		this->intermediate_regular_flag = this->intermediate_inplace_flag = this->complete_flag = this->ir_track_count = this->ii_track_count = 0;
	}

	void reset_current_processing()
	{
		this->processing_data.clear();
	}

	// this is like earliest attempt at threads synchronization
	// todo DO THIS PROPERLY
	bool check_smrp_processing()
	{
		for (auto& el : this->processing_data)
			if (!el.second->finished || el.second->processing)
				return 1;
		return 0;
	}

	bool check_smrp_processing_and_start_next_step()
	{
		if (this->check_smrp_processing())
			return 1;
		//this->ResetCurProcessing();
		this->start_ri_merge();
		return 0;
	}

	bool check_ri_merge()
	{
		if (this->intermediate_regular_flag && this->intermediate_inplace_flag)
			this->final_merge();
		else return 0;
		//cout << "Final_Finished\n";
		return 1;
	}

	void start_processing_midis() 
	{
		std::set<std::uint32_t> IDs;

		for (auto& el: this->processing_data) 
			IDs.insert(el.first->settings.details.group_id);

		this->currently_processed_locker.lock();
		this->currently_processing.clear();
		this->currently_processing.resize(IDs.size());
		this->currently_processed_locker.unlock();

		std::uint32_t thread_counter = 0;
		
		for (auto id : IDs)
		{
			std::thread([this](
				std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> processing_data,
				std::uint32_t id,
				std::uint32_t thread_counter) 
			{
				for (auto& el : processing_data)
				{
					if (el.first->settings.details.group_id != id)
						continue;

					this->currently_processed_locker.lock();
					this->currently_processing[thread_counter] = el;
					this->currently_processed_locker.unlock();

					if (el.first->settings.proc_details.channel_split)
						single_midi_processor_2::sync_processing<true>(*el.first, *el.second);
					else
						single_midi_processor_2::sync_processing<false>(*el.first, *el.second);
				}
			}, this->processing_data, id, thread_counter).detach();
			thread_counter++;
		}
	}
	void inplace_merge()
	{
		using mpd_t = decltype(this->processing_data);
		mpd_t inplace_merge_candidates;
		std::copy_if(this->processing_data.begin(), this->processing_data.end(), std::back_inserter(inplace_merge_candidates),
			[](mpd_t::value_type& el) { return el.first->settings.details.inplace_mergable; });
		
		this->ii_track_count = 0;
		std::thread([](mpd_t inplace_merge_candidates, std::uint16_t ppqn, std_unicode_string _save_to,
				std::reference_wrapper<std::atomic_bool> complete_flag,
				std::reference_wrapper<std::atomic_uint64_t> track_count)
		{
			if (inplace_merge_candidates.empty()) 
			{
				complete_flag.get() = true; /// Will this work?
				return;
			}

			bool active_stream_flag = true;
			bool active_track_reading = true;
			std::vector<bbb_ffr*> file_inputs;

			std::vector<std::int64_t> decaying_delta_times;
			std::vector<std::uint8_t> track, front_edge, back_edge;
			track.reserve(1000000);

			auto [file_output_ptr, file_output_fo_ptr] = open_wide_stream<std::ostream>
				(_save_to + to_cchar_t(".I.mid").operator std_unicode_string(), to_cchar_t("wb"));
			
			*file_output_ptr << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;

			for (auto& el : inplace_merge_candidates) 
			{
				auto filename = el.first->filename + el.first->postfix;
				auto file_input_ptr = new bbb_ffr(filename.c_str());

				for (int i = 0; i < 14; i++)
					file_input_ptr->get();

				decaying_delta_times.push_back(0);
				file_inputs.push_back(file_input_ptr);
			}

			file_output_ptr->put(track_count.get() >> 8);
			file_output_ptr->put(track_count.get());
			file_output_ptr->put(ppqn >> 8);
			file_output_ptr->put(ppqn);

			// TODO refactor this clusterfuck 
			while (active_stream_flag) 
			{
				///reading tracks
				std::uint32_t Header = 0, DIO, DeltaLen = 0;
				std::uint64_t InTrackDelta = 0;
				bool ITD_Flag = 1 /*, NotNoteEvents_ProcessedFlag = 0*/;
				for (int i = 0; i < file_inputs.size(); i++)
				{
					while (file_inputs[i]->good() && Header != single_midi_processor_2::MTrk_header) 
						Header = (Header << 8) | file_inputs[i]->get();

					for (int w = 0; w < 4; w++)
						file_inputs[i]->get();
					Header = 0;
					if (file_inputs[i]->good())
						decaying_delta_times[i] = 0;
					else
						decaying_delta_times[i] = -1;
				}
				
				for (std::uint64_t Tick = 0; active_track_reading; Tick++, InTrackDelta++)
				{
					std::uint8_t IO = 0, EVENTTYPE = 0;
					active_track_reading = 0;
					active_stream_flag = 0;
					for (int i = 0; i < file_inputs.size(); i++)
					{
						///every stream
						if (decaying_delta_times[i] == 0)
						{
							if (Tick)
								goto eventevalution;

						deltatime_reading:

							DIO = 0;
							do 
							{
								IO = file_inputs[i]->get();
								DIO = (DIO << 7) | (IO & 0x7F);
							} while (IO & 0x80);

							if (file_inputs[i]->eof())
								goto trackending;
							
							if ((decaying_delta_times[i] = DIO))
								goto escape;

						eventevalution:

							EVENTTYPE = (file_inputs[i]->get());

							DeltaLen = single_midi_processor_2::push_vlv(InTrackDelta, track);
							InTrackDelta = 0;

							if (EVENTTYPE == 0xFF)
							{///meta
								track.push_back(EVENTTYPE);
								track.push_back(file_inputs[i]->get());
								if (track.back() == 0x2F)
								{
									file_inputs[i]->get();
									for (int l = 0; l < DeltaLen + 2; l++)
										track.pop_back();

									goto trackending;
								}
								else 
								{
									DIO = 0;
									do 
									{
										IO = file_inputs[i]->get();
										track.push_back(IO);
										DIO = (DIO << 7) | (IO & 0x7F);
									} while (IO & 0x80);

									for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
										track.push_back(file_inputs[i]->get());
								}
							}
							else if (EVENTTYPE == 0xF0 || EVENTTYPE == 0xF7)
							{
								track.push_back(EVENTTYPE);
								DIO = 0;
								do 
								{
									IO = file_inputs[i]->get();
									track.push_back(IO);
									DIO = (DIO << 7) | (IO & 0x7F);
								} while (IO & 0x80);

								for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
									track.push_back(file_inputs[i]->get());
							}
							else if ((EVENTTYPE >= 0x80 && EVENTTYPE <= 0xBF) || (EVENTTYPE >= 0xE0 && EVENTTYPE <= 0xEF))
							{
								track.push_back(EVENTTYPE);
								track.push_back(file_inputs[i]->get());
								track.push_back(file_inputs[i]->get());
							}
							else if ((EVENTTYPE >= 0xC0 && EVENTTYPE <= 0xDF))
							{
								track.push_back(EVENTTYPE);
								track.push_back(file_inputs[i]->get());
							}
							else 
							{
								///RSB CANNOT APPEAR IN THIS STAGE
								auto pos = file_inputs[i]->tellg();
								std::cout << "Inplace error @" << std::hex << pos << std::dec << std::endl;

								ThrowAlert_Error("DTI Failure at " + std::to_string(pos) + ". Type: " +
									std::to_string(EVENTTYPE) + ". Tell developer about it and give him source midis.\n");

								track.push_back(0xCA);
								track.push_back(0);
								goto trackending;
							}
							goto deltatime_reading;
						trackending:
							decaying_delta_times[i] = -1;
							if (!file_inputs[i]->eof())
								active_stream_flag = 1;
							continue;
						escape:
							active_track_reading = 1;
							decaying_delta_times[i]--;
							continue;
						//file_eof:
							decaying_delta_times[i] = -1;
							continue;
						}
						else if (decaying_delta_times[i] > 0) 
						{
							active_track_reading = 1;
							decaying_delta_times[i]--;
							continue;
						}
						else 
						{
							if (file_inputs[i]->good())
							{
								active_stream_flag = 1; 
							}
							continue;
						}
						///while it's zero delta time
					}
					if (!active_track_reading && !track.empty()) 
					{

						DeltaLen = single_midi_processor_2::push_vlv(InTrackDelta, track);
						InTrackDelta = 0;

						track.push_back(0xFF);
						track.push_back(0x2F);
						track.push_back(0x00);
					}
				}
				active_track_reading = 1;
				constexpr std::uint32_t EDGE = 0x7F000000;

				if (track.size() > 0xFFFFFFFFu)
					std::cout << "TrackSize overflow!!!\n";
				else if (track.empty())
					continue;

				if (track.size() > EDGE * 2) 
				{
					std::int64_t total_shift = EDGE, prev_edge_pos = 0;
					normalised_track_iterator iterator(track);
					front_edge.clear();
					while (iterator.advance_until_reaching_position_of(total_shift))
					{
						total_shift = iterator.current_position + EDGE;
						back_edge.clear();
						iterator.put_current_held_notes(back_edge, false);
						std::uint64_t total_size = front_edge.size() + (iterator.current_position - prev_edge_pos) + back_edge.size() + 4;
						(*file_output_ptr) << "MTrk";
						file_output_ptr->put(total_size >> 24);
						file_output_ptr->put(total_size >> 16);
						file_output_ptr->put(total_size >> 8);
						file_output_ptr->put(total_size);

						single_midi_processor_2::ostream_write(front_edge, *file_output_ptr);
						single_midi_processor_2::ostream_write(track, track.begin() + prev_edge_pos, track.begin() + iterator.current_position, *file_output_ptr);
						single_midi_processor_2::ostream_write(back_edge, back_edge.begin(), back_edge.end(), *file_output_ptr);

						file_output_ptr->put(0); //that's why +4 // ???????
						file_output_ptr->put(0xFF);
						file_output_ptr->put(0x2F);
						file_output_ptr->put(0);
						(track_count.get())++;

						prev_edge_pos = iterator.current_position;
						front_edge.clear();
						iterator.put_current_held_notes(front_edge, true);
					}
					(*file_output_ptr) << "MTrk";
					std::uint64_t total_size = front_edge.size() + (iterator.current_position - prev_edge_pos);
					//cout << "Outside:" << TotalSize << endl;
					file_output_ptr->put(total_size >> 24);
					file_output_ptr->put(total_size >> 16);
					file_output_ptr->put(total_size >> 8);
					file_output_ptr->put(total_size);

					single_midi_processor_2::ostream_write(front_edge, *file_output_ptr);
					single_midi_processor_2::ostream_write(track, track.begin() + prev_edge_pos, track.end(), *file_output_ptr);

					front_edge.clear();
					back_edge.clear();
					++(track_count.get());
				}
				else
				{
					*file_output_ptr << "MTrk";
					file_output_ptr->put(track.size() >> 24);
					file_output_ptr->put(track.size() >> 16);
					file_output_ptr->put(track.size() >> 8);
					file_output_ptr->put(track.size());

					single_midi_processor_2::ostream_write(track, *file_output_ptr);
					//copy(Track.begin(), Track.end(), ostream_iterator<std::uint8_t>(file_output));
					++(track_count.get());
				}
				track.clear();
			}
			for (int i = 0; i < file_inputs.size(); i++)
			{
				file_inputs[i]->close();
				auto& imc_i = inplace_merge_candidates[i];

				if (imc_i.first->settings.proc_details.remove_remnants)
#ifdef _MSC_VER
					_wremove
#else
					remove
#endif
						((inplace_merge_candidates[i].first->filename + inplace_merge_candidates[i].first->postfix).c_str());
			}
			file_output_ptr->seekp(10, std::ios::beg);
			file_output_ptr->put((track_count.get()) >> 8);
			file_output_ptr->put((track_count.get()) & 0xff);

			if (file_output_fo_ptr)
				fclose(file_output_fo_ptr);
			else
				file_output_ptr->flush();

			for (auto& t : file_inputs)
				delete t;

			delete file_output_ptr;
			printf("Inplace: finished\n");
			complete_flag.get() = true;

		},
		inplace_merge_candidates, final_ppqn, save_to, std::ref(complete_flag), std::ref(ii_track_count)).detach();
	}

	void regular_merge()
	{
		using mpd_t = decltype(processing_data);
		mpd_t regular_merge_candidates;
		std::copy_if(processing_data.begin(), processing_data.end(), std::back_inserter(regular_merge_candidates),
			[](mpd_t::value_type& el) { return !el.first->settings.details.inplace_mergable; });

		if (regular_merge_candidates.size() == 1)
		{
			auto filename = 
				regular_merge_candidates.front().first->filename + 
				regular_merge_candidates.front().first->postfix;
			auto save_to_with_postfix = save_to + 
				to_cchar_t(".R.mid").operator std_unicode_string();

			auto remove_functor =
#ifdef _MSC_VER
					_wremove;
#else
					remove;
#endif

			auto rename_functor =
#ifdef _MSC_VER
					_wrename;
#else
					rename;
#endif

			remove_functor(save_to_with_postfix.c_str());
			auto result = rename_functor(filename.c_str(), save_to_with_postfix.c_str());

			ir_track_count += regular_merge_candidates.front().first->tracks_count;
			intermediate_regular_flag = true; /// Will this work?
		}
		else
		{
		std::thread([](mpd_t regular_merge_candidates, std::uint16_t ppqn, std_unicode_string save_to,
			std::reference_wrapper<std::atomic_bool> complete_flag, std::reference_wrapper<std::atomic_uint64_t> track_count)
		{
			if (regular_merge_candidates.empty())
			{
				complete_flag.get() = true;
				return;
			}
			//bool FirstFlag = 1;
			const size_t buffer_size = 20000000;
			std::uint8_t* buffer = new std::uint8_t[buffer_size];
			auto [file_output_ptr, file_output_fo_ptr] = open_wide_stream<std::ostream>
				(save_to + to_cchar_t(".R.mid").operator std_unicode_string(), to_cchar_t("wb"));

			std_unicode_string filename = regular_merge_candidates.front().first->filename + regular_merge_candidates.front().first->postfix;
			bbb_ffr file_input(filename.c_str());
			file_output_ptr->rdbuf()->pubsetbuf((char*)buffer, buffer_size);

			*file_output_ptr << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			file_output_ptr->put(0);
			file_output_ptr->put(0);
			file_output_ptr->put(ppqn >> 8);
			file_output_ptr->put(ppqn);

			for (auto Y = regular_merge_candidates.begin(); Y != regular_merge_candidates.end(); Y++)
			{
				filename = Y->first->filename + Y->first->postfix;
				if (Y != regular_merge_candidates.begin())
					file_input.reopen_next_file(filename.c_str());

				for (int i = 0; i < 14; i++)
					file_input.get();

				file_input.put_into_ostream(*file_output_ptr);
				track_count.get() += Y->first->tracks_count;
				std::int32_t t;

				if (Y->first->settings.proc_details.remove_remnants)
					t =
#ifdef _MSC_VER
					_wremove
#else
					remove
#endif
						(filename.c_str());
			}

			file_output_ptr->seekp(10, std::ios::beg);
			file_output_ptr->put(track_count.get() >> 8);
			file_output_ptr->put(track_count.get());
			complete_flag.get() = true; /// Will this work?
			file_output_ptr->flush();

			if (file_output_fo_ptr)
				fclose(file_output_fo_ptr);

			delete[] buffer;
			delete file_output_ptr;
			}, regular_merge_candidates, final_ppqn, save_to, std::ref(complete_flag), std::ref(ir_track_count)).detach();
		}
	}

	void start_ri_merge() 
	{
		this->regular_merge();
		this->inplace_merge();
	}

	void final_merge() 
	{
		std::thread([this](std::reference_wrapper<std::atomic_bool> complete_flag, std_unicode_string save_to)
		{
			bbb_ffr* im_ptr = new bbb_ffr((save_to + to_cchar_t(".I.mid").operator std_unicode_string()).c_str());
			bbb_ffr* rm_ptr = new bbb_ffr((save_to + to_cchar_t(".R.mid").operator std_unicode_string()).c_str());

			auto [file_output_ptr, file_output_fo_ptr] = open_wide_stream<std::ostream>
				(save_to + to_cchar_t(".F.mid").operator std_unicode_string(), to_cchar_t("wb"));

			bool im_good = !im_ptr->eof(), rm_good = !rm_ptr->eof();

			auto total_tracks = ii_track_count.load() + ir_track_count.load();
			if ((~0xFFFFULL) & total_tracks)
			{
				printf("Track count overflow: %llu\n", total_tracks);
				total_tracks = 0xFFFF;
			}

			if (!im_good || !rm_good)
			{
				im_ptr->close();
				rm_ptr->close();
				file_output_ptr->flush();

				if (file_output_fo_ptr)
					fclose(file_output_fo_ptr);

				auto inplace_filename = save_to + to_cchar_t(".I.mid").operator std_unicode_string();
				auto regular_filename = save_to + to_cchar_t(".R.mid").operator std_unicode_string();

				auto remove_orig =
#ifdef _MSC_VER
					_wremove
#else
					remove
#endif
				 		(save_to.c_str());

				auto remove_i =
#ifdef _MSC_VER
					_wrename
#else
					rename
#endif
						(inplace_filename.c_str(), save_to.c_str());

				auto remove_r =
#ifdef _MSC_VER
					_wrename
#else
					rename
#endif
						(regular_filename.c_str(), save_to.c_str());//one of these will not work

				printf("S2 status: %i\nI status: %i\nR status: %i\n", remove_orig, remove_i, remove_r);

				delete im_ptr;
				delete rm_ptr;
				delete file_output_ptr;

				printf("Escaped last stage\n");
				complete_flag.get() = true;

				return;
			}

			printf("Active merging at last stage (untested)\n");

			std::uint16_t T = 0;
			std::uint8_t A = 0, B = 0;

			file_output_ptr->put('M');
			file_output_ptr->put('T');
			file_output_ptr->put('h');
			file_output_ptr->put('d');
			file_output_ptr->put(0);
			file_output_ptr->put(0);
			file_output_ptr->put(0);
			file_output_ptr->put(6);
			file_output_ptr->put(0);
			file_output_ptr->put(1);
			for (int i = 0; i < 12; i++)
				im_ptr->get();
			for (int i = 0; i < 12; i++)
				rm_ptr->get();
			T = total_tracks;
			A = T >> 8;
			B = T;
			file_output_ptr->put(A);
			file_output_ptr->put(B);

			im_ptr->get();
			im_ptr->get();
			file_output_ptr->put(rm_ptr->get());
			file_output_ptr->put(rm_ptr->get());

			im_ptr->put_into_ostream(*file_output_ptr);
			rm_ptr->put_into_ostream(*file_output_ptr);

			im_ptr->close();
			rm_ptr->close();
			file_output_ptr->flush();

			if (file_output_fo_ptr)
				fclose(file_output_fo_ptr);

			delete im_ptr;
			delete rm_ptr;
			delete file_output_ptr;

			complete_flag.get() = true;
			if (remnants_remove)
			{
				auto inplace_filename = save_to + to_cchar_t(".I.mid").operator std_unicode_string();
				auto regular_filename = save_to + to_cchar_t(".R.mid").operator std_unicode_string();
				auto remove_functor =
#ifdef _MSC_VER
					_wremove;
#else
					remove;
#endif

				remove_functor(inplace_filename.c_str());
				remove_functor(regular_filename.c_str());
			}
		}, std::ref(complete_flag), save_to).detach();
	}
};

#endif
