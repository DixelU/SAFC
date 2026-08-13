#pragma once
#ifndef SAF_SMIC
#define SAF_SMIC

#include <string>
#include <array>

#include "midi_file_reader.h"
#include "../btree/btree_map.h"
#include <long_uint.h>

#include <boost/container/deque.hpp>
#include <boost/container/flat_map.hpp>

#define MTrk 1297379947
#define MThd 1297377380

struct single_midi_info_collector 
{
	struct tempo_event
	{
		std::uint8_t a;
		std::uint8_t b;
		std::uint8_t c;

		tempo_event(std::uint8_t a, std::uint8_t b, std::uint8_t c) : 
			a(a), b(b), c(c) 
		{}

		tempo_event() :
			tempo_event(0x7, 0xA1, 0x20)
		{}

		inline static double get_bpm(std::uint8_t a, std::uint8_t b, std::uint8_t c)
		{
			std::uint32_t L = (a << 16) | (b << 8) | (c);
			return 60000000. / L;
		}

		inline operator double() const
		{
			return get_bpm(a, b, c);
		}

		inline bool operator<(const tempo_event& TE) const
		{
			return double(*this) < (double)TE;
		}

		inline uint32_t get_raw() const 
		{
			return (a << 16) | (b << 8) | (c);
		}
	};

	struct note_on_off_counter
	{
		std::uint64_t note_on;
		std::uint64_t note_off;

		note_on_off_counter() :
			note_on(0), note_off(0)
		{}

		note_on_off_counter(std::uint64_t base) :
			note_on_off_counter()
		{
			if (base > 0)
				note_on += base;
			else
				note_off -= base;
		}

		note_on_off_counter(std::int64_t NOn, std::int64_t NOff) :
			note_on(NOn), note_off(NOff)
		{}

		inline operator std::int64_t() const
		{
			return note_on - note_off;
		}

		note_on_off_counter inline operator +(std::int64_t offset)
		{
			if (offset >= 0)
				note_on += offset;
			else
				note_off -= offset;
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
			note_on++;
			return *this;
		}

		note_on_off_counter inline operator --()
		{
			note_off++;
			return *this;
		}
	};

	struct track_data 
	{
		std::int64_t MTrk_pos;
	};

	struct long_time
	{
		dixelu::long_uint<0> numerator;
		std::uint64_t denominator;
	};

	using tempo_graph = btree::btree_map<std::int64_t, tempo_event>;
	using time_graph = boost::container::flat_map<std::int64_t, long_time>;
	using der_polyphony_graph = btree::btree_map<std::int64_t, note_on_off_counter>;
	using polyphony_graph = btree::btree_map<std::int64_t, std::int64_t>;
	using notes_per_second_graph = btree::btree_map<std::int64_t, std::int64_t>;

	bool processing, finished;

	std::wstring filename;
	std::string log_line;
	std::string error_line;

	time_graph internal_time_map;
	tempo_graph tempo_map;
	polyphony_graph polyphony;
	notes_per_second_graph notes_per_second;

	std::vector<track_data> tracks;
	std::uint16_t ppq;

	bool allow_legacy_rsb_meta_interaction;

	single_midi_info_collector(std::wstring filename, std::uint16_t ppq, bool allow_legacy_rsb_meta_interaction = false) : filename(filename), log_line(" "), processing(0), finished(0), ppq(ppq), allow_legacy_rsb_meta_interaction(allow_legacy_rsb_meta_interaction)
	{}

	void fetch_data() 
	{
		der_polyphony_graph poly_differences;
		processing = true;

		midi_file_reader file_input(filename);

		std::uint32_t MTRK = 0, vlv = 0;
		std::uint64_t last_tick = 0;
		std::uint64_t current_tick = 0;
		std::uint8_t IO = 0, rsb_byte = 0; // TODO: REMOVE THE DAMN IO VARIABLE
		track_data track_data{};

		error_line = " ";
		log_line = " ";

		tempo_map[0] = tempo_event(0x7, 0xA1, 0x20);
		poly_differences[-1] = note_on_off_counter();

		for (int i = 0; i < 12 && file_input.good(); i++)
			static_cast<void>(read_midi_byte(file_input));

		ppq = static_cast<std::uint16_t>(read_midi_byte(file_input)) << 8;
		ppq |= static_cast<std::uint16_t>(read_midi_byte(file_input));

		while (file_input.good())
		{
			std::array<std::uint64_t, 4096> polyphony;

			current_tick = 0;
			MTRK = 0;

			while (MTRK != MTrk && file_input.good())
				MTRK = (MTRK << 8) | read_midi_byte(file_input);

			for (int i = 0; i < 4; i++)
				static_cast<void>(read_midi_byte(file_input));

			IO = rsb_byte = 0;
			while (file_input.good())
			{
				track_data.MTrk_pos = file_input.position() - 8;
				vlv = 0;
				do 
				{
					IO = read_midi_byte(file_input);
					vlv = (vlv << 7) | (IO & 0x7F);
				} while (IO & 0x80 && !file_input.eof());

				current_tick += vlv;
				if (last_tick < current_tick)
					last_tick = current_tick;

				auto& current_poly_diff = poly_differences[current_tick];

				std::uint8_t event_type = read_midi_byte(file_input);
				if (event_type == 0xFF)
				{
					if(allow_legacy_rsb_meta_interaction)
						rsb_byte = 0;

					std::uint8_t type = read_midi_byte(file_input);
					std::uint32_t meta_data_size = 0;
					do 
					{
						IO = read_midi_byte(file_input);
						meta_data_size = meta_data_size << 7 | IO & 0x7F;
					} while (IO & 0x80 && !file_input.eof());

					if (type == 0x2F)
					{
						tracks.push_back(track_data);
						log_line = "Track " + std::to_string(tracks.size()) + " parsed";
						break;
					}
					else if (type == 0x51)
					{
						tempo_event TE;
						TE.a = read_midi_byte(file_input);
						TE.b = read_midi_byte(file_input);
						TE.c = read_midi_byte(file_input);
						tempo_map[current_tick] = TE;
					}
					else
					{
						while (meta_data_size--)
							static_cast<void>(read_midi_byte(file_input));
					}
				}
				else if (event_type >= 0x80 && event_type <= 0x9F)
				{
					rsb_byte = event_type;
					int change = (event_type & 0x10) ? 1 : -1;
					auto key = read_midi_byte(file_input);
					auto volume = read_midi_byte(file_input);

					if (!volume && change == 1)
						change = -1;

					int index = event_type & 0xF | (key << 4);
					if (change == -1)
					{
						if (polyphony[index] == 0)
							continue;
						polyphony[index] -= 1;
					}
					else 
						polyphony[index] += 1;

					current_poly_diff += change;
				}
				else if ((event_type >= 0xA0 && event_type <= 0xBF) ||
					(event_type >= 0xE0 && event_type <= 0xEF))
				{
					rsb_byte = event_type;
					static_cast<void>(read_midi_byte(file_input));
					static_cast<void>(read_midi_byte(file_input));
				}
				else if (event_type >= 0xC0 && event_type <= 0xDF)
				{
					rsb_byte = event_type;
					static_cast<void>(read_midi_byte(file_input));
				}
				else if (event_type == 0xF0 || event_type == 0xF7)
				{
					if(allow_legacy_rsb_meta_interaction)
						rsb_byte = 0;

					std::uint32_t meta_data_size = 0;

					do
					{
						IO = read_midi_byte(file_input);
						meta_data_size = (meta_data_size << 7) | (IO & 0x7F);
					} while (IO & 0x80 && !file_input.eof());

					while (meta_data_size--)
						static_cast<void>(read_midi_byte(file_input));
				}
				else 
				{
					std::uint8_t first_param = event_type;
					event_type = rsb_byte;
					if (event_type >= 0x80 && event_type <= 0x9F)
					{
						int change = (event_type & 0x10) ? 1 : -1;
						auto volume = read_midi_byte(file_input);

						if (!volume && change == 1)
							change = -1;

						int index = event_type & 0xF | (first_param << 4);
						if (change == -1) 
						{
							if (polyphony[index] == 0)
								continue;
							polyphony[index] -= 1;
						}
						else
							polyphony[index] += 1;

						current_poly_diff += change;
					}
					else if ((event_type >= 0xA0 && event_type <= 0xBF) || (event_type >= 0xE0 && event_type <= 0xEF))
					{
						static_cast<void>(read_midi_byte(file_input));
					}
					else if (event_type >= 0xC0 && event_type <= 0xDF)
					{
						//nothing
					}
					else
					{
						error_line = "Corruption detected at: " + std::to_string(file_input.position());
						break;
					}
				}
			}
		}

		tempo_map[last_tick + 1] = tempo_map.rbegin()->second;
		poly_differences[last_tick + 1] = 0;

		std::int64_t current_poly = 0;
		for (const auto & cur_pair : poly_differences)
			polyphony[cur_pair.first] = (current_poly += cur_pair.second);

		long_time time;
		uint64_t previous_tick = 0;
		uint32_t previous_tempo = tempo_event{}.get_raw();
		time.denominator = 1000000ull * ppq;

		for (const auto & [tick, tempo_data] : tempo_map)
		{
			auto interval_seconds_rhs = dixelu::long_uint<0>{ tick - previous_tick } * previous_tempo;

			time.numerator += interval_seconds_rhs;
			internal_time_map[tick] = time;

			previous_tick = tick;
			previous_tempo = tempo_data.get_raw();
		}

		using uint128_t = dixelu::long_uint<0>;
		auto get_time_numerator_at_tick = [&](std::int64_t tick)
		{
			auto tempo_it = tempo_map.upper_bound(tick);
			if (tempo_it != tempo_map.begin())
				--tempo_it;

			const auto time_it = internal_time_map.find(tempo_it->first);
			uint128_t result = time_it->second.numerator;

			if (tick > tempo_it->first)
				result += uint128_t{ static_cast<std::uint64_t>(tick - tempo_it->first) } * tempo_it->second.get_raw();

			return result;
		};

		using custom_block_size = boost::container::deque_options<boost::container::block_bytes<(1u << 16)>>::type;
		boost::container::deque<std::pair<uint128_t, std::int64_t>, void, custom_block_size> recent_note_hits;

		const uint128_t one_second = time.denominator;
		std::int64_t rolling_note_hits = 0;

		for (const auto& [tick, poly_diff] : poly_differences)
		{
			const auto current_time = get_time_numerator_at_tick(tick);

			recent_note_hits.emplace_back(current_time, poly_diff.note_on);
			rolling_note_hits += poly_diff.note_on;

			while (!recent_note_hits.empty() && (current_time - recent_note_hits.front().first) > one_second)
			{
				rolling_note_hits -= recent_note_hits.front().second;
				recent_note_hits.pop_front();
			}

			notes_per_second[tick] = rolling_note_hits;
		}

		const auto tail_tick = static_cast<std::int64_t>(last_tick) + 1;
		if (notes_per_second.rbegin()->first < tail_tick)
			notes_per_second[tail_tick] = notes_per_second.rbegin()->second;

		finished = true;
		processing = false;

		file_input.close();
	}
};

#endif
