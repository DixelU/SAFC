#pragma once
#ifndef SAF_SMIC
#define SAF_SMIC

#include <string>
#include <array>

#include "bbb_ffio.h"
#include <btree/btree_map.h>

constexpr bool STRICT_WARNINGS = true;

struct single_midi_info_collector 
{
	static constexpr uint32_t MTrk_header = 1297379947;
	static constexpr uint32_t MThd_header = 1297377380;

	struct tempo_event
	{
		std::uint8_t A;
		std::uint8_t B;
		std::uint8_t C;

		constexpr tempo_event(std::uint8_t A, std::uint8_t B, std::uint8_t C) : A(A), B(B), C(C) {	}
		constexpr tempo_event() : tempo_event(0x7, 0xA1, 0x20) {}

		constexpr double get_bpm() const noexcept
		{
			return 60000000. / ((this->A << 16) | (this->B << 8) | (this->C));
		}

		constexpr operator double() const noexcept
		{
			return get_bpm();
		}
	};

	struct note_on_off_counter
	{
		std::int64_t note_on;
		std::int64_t note_off;
		note_on_off_counter() : note_on(0), note_off(0) {}
		note_on_off_counter(std::int64_t Base) : note_on_off_counter()
		{
			if (Base > 0)
				this->note_on += Base;
			else
				this->note_off -= Base;
		}
		note_on_off_counter(std::int64_t note_on, std::int64_t note_off) : note_on(note_on), note_off(note_off) {}
		inline operator std::int64_t() const
		{
			return this->note_on - this->note_off;
		}
		note_on_off_counter inline operator +(std::int64_t offset)
		{
			if (offset > 0)
				this->note_on += offset;
			else
				this->note_off -= offset;
			return *this;
		}
		note_on_off_counter inline operator +=(std::int64_t offset)
		{
			return *this = (*this + (offset));
		}
		note_on_off_counter inline operator -(std::int64_t offset)
		{
			return *this + (-offset);
		}
		note_on_off_counter inline operator -=(std::int64_t offset)
		{
			return *this = (*this + (-offset));
		}
		note_on_off_counter inline operator ++()
		{
			this->note_on++;
			return *this;
		}
		note_on_off_counter inline operator --()
		{
			this->note_off++;
			return *this;
		}
	};

	struct track_data 
	{
		std::int64_t MTrk_pos;
	};

	bool processing, finished;
	std_unicode_string FileName;
	std::string log_line;
	std::string error_line;
	using tempo_graph = btree::btree_map<std::int64_t, tempo_event>;
	using der_polyphony_graph = btree::btree_map<std::int64_t, note_on_off_counter>;
	using polyphony_graph = btree::btree_map<std::int64_t, std::int64_t>;
	tempo_graph tempo_map;
	polyphony_graph polyphony_map;
	std::vector<track_data> tracks;
	std::uint16_t ppq;
	bool allow_legacy_meta_running_status_behaviour;
	//Locker<btree::btree_map<std::uint64_t, std::uint64_t>> Polyphony;
	//Locker<btree::btree_map<>>

	single_midi_info_collector(std_unicode_string filename, std::uint16_t ppq, bool allow_legacy_meta_running_status_behaviour = false) :
		FileName(filename),
		log_line(" "),
		processing(0),
		finished(0),
		ppq(ppq),
		allow_legacy_meta_running_status_behaviour(allow_legacy_meta_running_status_behaviour)
	{}

	void lookup() 
	{
		der_polyphony_graph polyphony_finite_difference;
		processing = true;
		bbb_ffr file_input(FileName.c_str());
		error_line = " ";
		log_line = " ";
		//std::array<std::uint32_t, 4096> CurHolded;
		std::uint32_t MTrk = 0, vlv = 0;
		std::uint64_t last_tick = 0;
		std::uint64_t cur_tick = 0;
		std::uint8_t io = 0, rsb = 0;
		track_data track_data;
		tempo_map[0] = tempo_event(0x7, 0xA1, 0x20);
		polyphony_finite_difference[-1] = note_on_off_counter();
		while (file_input.good())
		{
			std::array<std::uint64_t, 4096> polyphony;
			cur_tick = 0;
			MTrk = 0;
			while (MTrk != MTrk_header && file_input.good())
				MTrk = (MTrk << 8) | (file_input.get());

			for (int i = 0; i < 4; i++)
				file_input.get();

			io = rsb = 0;
			while (file_input.good())
			{
				track_data.MTrk_pos = file_input.tellg() - 8;
				vlv = 0;
				do 
				{
					io = file_input.get();
					vlv = (vlv << 7) | (io & 0x7F);
				} while (io & 0x80 && !file_input.eof());
				cur_tick += vlv;
				if (last_tick < cur_tick)
					last_tick = cur_tick;
				std::uint8_t event_type = file_input.get();
				if (event_type == 0xFF)
				{
					if(allow_legacy_meta_running_status_behaviour)
						rsb = 0;
					std::uint8_t type = file_input.get();
					std::uint32_t meta_data_size = 0;
					do 
					{
						io = file_input.get();
						meta_data_size = (meta_data_size << 7) | (io & 0x7F);
					} while (io & 0x80 && !file_input.eof());
					if (type == 0x2F)
					{
						tracks.push_back(track_data);
						log_line = "Passed " + std::to_string(tracks.size()) + " tracks";
						break;
					}
					else if (type == 0x51)
					{
						tempo_event te;
						te.A = file_input.get();
						te.B = file_input.get();
						te.C = file_input.get();
						tempo_map[cur_tick] = te;
					}
					else
					{
						while (meta_data_size--)
							file_input.get();
					}
				}
				else if (event_type >= 0x80 && event_type <= 0x9F)
				{
					rsb = event_type;
					int change = (event_type & 0x10) ? 1 : -1;
					auto it = polyphony_finite_difference.find(cur_tick);
					auto key = file_input.get();
					auto volume = file_input.get();

					if (!volume && change == 1)
						change = -1;

					int index = (event_type & 0xF) | (key << 4);
					if (change == -1)
					{
						if (polyphony[index] == 0)
							continue;
						polyphony[index] -= 1;
					}
					else 
						polyphony[index] += 1;

					if (it == polyphony_finite_difference.end())
						polyphony_finite_difference[cur_tick] = change;
					else
						it->second += change;
				}
				else if ((event_type >= 0xA0 && event_type <= 0xBF) ||
					(event_type >= 0xE0 && event_type <= 0xEF))
				{
					rsb = event_type;
					file_input.get();
					file_input.get();
				}
				else if (event_type >= 0xC0 && event_type <= 0xDF)
				{
					rsb = event_type;
					file_input.get();
				}
				else if (event_type == 0xF0 || event_type == 0xF7)
				{
					if(allow_legacy_meta_running_status_behaviour)
						rsb = 0;
					std::uint32_t meta_data_size = 0;
					do
					{
						io = file_input.get();
						meta_data_size = (meta_data_size << 7) | (io & 0x7F);
					} while (io & 0x80 && !file_input.eof());
					while (meta_data_size--)
						file_input.get();
				}
				else 
				{
					std::uint8_t first_param = event_type;
					rsb = event_type;
					if (event_type >= 0x80 && event_type <= 0x9F)
					{
						int change = (event_type & 0x10) ? 1 : -1;
						auto it = polyphony_finite_difference.find(cur_tick);
						auto volume = file_input.get();

						if (!volume && change == 1)
							change = -1;

						int index = (event_type & 0xF) | (first_param << 4);
						if (change == -1) 
						{
							if (polyphony[index] == 0)
								continue;
							polyphony[index] -= 1;
						}
						else
							polyphony[index] += 1;

						if (it == polyphony_finite_difference.end())
							polyphony_finite_difference[cur_tick] = change;
						else
							it->second += change;
					}
					else if ((event_type >= 0xA0 && event_type <= 0xBF) ||
						(event_type >= 0xE0 && event_type <= 0xEF))
					{
						file_input.get();
					}
					else if (event_type >= 0xC0 && event_type <= 0xDF)
					{
						//nothing
					}
					else
					{ // here throw an error
						error_line = "Corruption detected at: " + std::to_string(file_input.tellg());
						break;
					}
				}
			}
		}
		tempo_map[last_tick] = tempo_map.rbegin()->second;
		polyphony_finite_difference[last_tick + 1] = 0;
		std::int64_t cur_polyphony = 0;
		for (auto cur_pair : polyphony_finite_difference)
			polyphony_map[cur_pair.first] = (cur_polyphony += cur_pair.second);
		finished = true;
		processing = false;
		file_input.close();
	}
};

#endif
