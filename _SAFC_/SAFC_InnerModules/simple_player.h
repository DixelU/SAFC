#pragma once
#ifndef SAFC_SIMPLE_PLAYER
#define SAFC_SIMPLE_PLAYER

#include "Windows.h"

#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <atomic>

#include "../bbb_ffio.h"

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"

template<typename T>
struct buffered_queue
{
private:
	static constexpr size_t slab_size = 4096;

	struct slab
	{
		std::aligned_storage_t<sizeof(T), alignof(T)> data[slab_size];
		const T* capacity_end;
		T* begin;
		T* end;

		std::unique_ptr<slab> next_slab;

		slab() :
			capacity_end(reinterpret_cast<T*>(data) + slab_size),
			begin(reinterpret_cast<T*>(data)),
			end(reinterpret_cast<T*>(data)),
			next_slab(nullptr)
		{}

		[[nodiscard]] size_t size() const { return end - begin; }
		[[nodiscard]] bool empty() const { return begin == end; }

		T& front() { return *begin; }
		const T& front() const { return *begin; }

		T& back() { return *(end - 1); }
		const T& back() const { return *(end - 1); }

		void pop() { begin->~T(); ++begin; }
		void push(T&& v) { ::new(end) T(std::move(v)); ++end; }

		bool full() const { return end == capacity_end; }

		void reset__unsafe() { begin = end = reinterpret_cast<T*>(data); }

		void clear() { while (begin != end) pop(); }
	};

	std::vector<std::unique_ptr<slab>> unused_slabs;
	std::unique_ptr<slab> head{};
	slab* tail = nullptr;

	std::unique_ptr<slab> get_empty_slab()
	{
		if (!unused_slabs.empty())
		{
			auto ptr = std::move(unused_slabs.back());
			unused_slabs.pop_back();
			return std::move(ptr);
		}

		return std::make_unique<slab>();
	}

	void dispose_of_empty_slab(std::unique_ptr<slab>&& slab_ptr)
	{
		slab_ptr->reset__unsafe();
		unused_slabs.push_back(std::move(slab_ptr));
	}

	struct iterator
	{
		slab* current_slab = nullptr;
		T* cur_obj = nullptr;

		T* operator->() { return cur_obj; }
		T& operator*() { return *cur_obj; }
		iterator& operator++(int)
		{

		}
	};
public:
	buffered_queue() = default;

	void push(T&& value)
	{
		if (!head)
		{
			head = get_empty_slab();
			tail = head.get();
		}

		if (tail->full())
		{
			tail->next_slab = get_empty_slab();
			tail = tail->next_slab.get();
		}

		tail->push(std::move(value));
	}

	void pop()
	{
		if (empty()) [[unlikely]]
			return;

		head->pop();

		if (!head->empty() || !head->full())
			return;

		auto new_head = std::move(head->next_slab);
		head->next_slab = nullptr;
		if (new_head == nullptr)
			tail = nullptr;

		dispose_of_empty_slab(std::move(head));
		head = std::move(new_head);
	}

	bool empty()
	{
		return !head || head->empty();
	}

	void clear()
	{
		while (!empty()) pop();
	}
};


struct simple_player
{
	using tick_type = std::uint64_t;

	struct track_info
	{
		const uint8_t* begining;
		const uint8_t* ending;
	};

	struct buffered_event
	{
		tick_type tick;
		uint64_t time_us;
		uint8_t status;
		uint8_t data1;
		uint8_t data2;

		bool is_note_on() const { return (status & 0xF0) == 0x90 && data2 > 0; }
		bool is_note_off() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
		uint8_t channel() const { return status & 0x0F; }
		uint8_t key() const { return data1; }
		uint8_t velocity() const { return data2; }
	};

	struct track_playback_state
	{
		const uint8_t* position;
		const uint8_t* end;
		tick_type current_tick;
		tick_type next_event_tick;
		uint8_t rsb; // running status byte
		bool done;

		void init(const track_info& track)
		{
			position = track.begining;
			end = track.ending;
			current_tick = 0;
			next_event_tick = 0;
			rsb = 0;
			done = false;
		}
	};

	struct playback_state
	{
		tick_type current_tick;
		uint64_t current_time_us;
		std::chrono::steady_clock::time_point start_time;
		uint64_t start_offset_us;

		std::vector<track_playback_state> track_states;
		size_t active_tracks;

		std::atomic<bool> playing;
		mutable std::atomic<bool> stop_requested;
		mutable std::atomic<bool> paused;

		buffered_queue<buffered_event> event_buffer;

		void reset()
		{
			current_tick = 0;
			current_time_us = 0;
			start_offset_us = 0;
			active_tracks = 0;
			playing = false;
			stop_requested = false;
			paused = false;
			track_states.clear();
			event_buffer.clear();
		}
	};

	struct midi_info
	{
		std::vector<track_info> tracks;
		std::map<uint64_t, uint32_t> tempo_tmp;
		std::vector<std::pair<uint64_t, uint64_t>> time_map_mcsecs;
		uint64_t ticks_length;

		volatile uint64_t scanned{0};
		volatile bool ready{false};
		volatile uint64_t size{0};

		uint16_t ppq{0};
	};

	struct tempo_cache
	{
		size_t current_index;      // index into time_map_mcsecs
		tick_type base_tick;       // tick at current tempo change
		uint64_t base_time_us;     // microseconds at current tempo change
		uint32_t current_tempo;    // current tempo (us per quarter note)
		tick_type next_change_tick; // tick of next tempo change (or max for last)

		void reset()
		{
			current_index = ~0ULL;
			base_tick = 0;
			base_time_us = 0;
			current_tempo = 500000; // default 120 BPM
			next_change_tick = ~0ULL;
		}
	};

	simple_player() = default;

	void init()
	{
		update_devices();
		init_midi_out(devices.size() - 1);

		warnings = std::make_shared<printing_logger>("33");
	}

	void simple_run(std::wstring filename)
	{
		auto res = open(filename);
		if (!res)
		{
			ThrowAlert_Error("Playback failed");
			return;
		}

		playback_thread();
	}

	const playback_state& get_state() const
	{
		return state;
	}

	const midi_info& get_info() const
	{
		return info;
	}

	bool open(std::wstring filename)
	{
		// prerequisite: this is a midi file with valid header;
		mmap = std::make_unique<bbb_mmap>(filename.c_str());
		info = midi_info{};

		if (!mmap || !mmap->good())
		{
			ThrowAlert_Warning("Unable to open MIDI file for playback");
			return false;
		}

		const auto begin = mmap->begin();
		const auto size = mmap->length();
		const auto end = begin + size;

		info.size = size;

		if (size < 18)
		{
			info.scanned = size;
			return false;
		}

		info.ppq = (begin[12] << 8) | (begin[13]);
		
		auto ptr = begin + 14;

		while (ptr < end)
		{
			if(!read_through_one_track(ptr, end))
				return false;

			info.scanned = ptr - begin;
		}

		info.scanned = end - begin;

		if (info.tracks.empty())
			return false;

		single_midi_info_collector::long_time time;
		uint64_t previous_tick = 0;
		uint32_t previous_tempo = 0x07A120;
		time.denominator = info.ppq;

		for (const auto& [tick, tempo_data] : info.tempo_tmp)
		{
			auto interval_seconds_rhs = dixelu::long_uint<0>{tick - previous_tick} * previous_tempo;

			time.numerator += interval_seconds_rhs;
			auto microseconds = time.numerator / time.denominator;

			info.time_map_mcsecs.emplace_back(tick, microseconds);

			previous_tick = tick;
			previous_tempo = tempo_data;
		}

		return true;
	}

	void playback_thread()
	{
		state.reset();
		update_tempo_cache_at(~0ULL); // initialize cache for tick 0
		state.playing = true;

		// initialize track states
		state.track_states.resize(info.tracks.size());
		for (size_t i = 0; i < info.tracks.size(); ++i)
		{
			state.track_states[i].init(info.tracks[i]);
			// read first delta time for each track
			if (state.track_states[i].position < state.track_states[i].end)
			{
				state.track_states[i].next_event_tick =
					get_vlv(state.track_states[i].position, state.track_states[i].end);
			}
			else
			{
				state.track_states[i].done = true;
			}
		}

		state.active_tracks = info.tracks.size();
		state.start_time = std::chrono::steady_clock::now();

		// lookahead for visualization (microseconds)
		constexpr uint64_t lookahead_us = 2000000; // 2 seconds

		while (state.active_tracks > 0 && !state.stop_requested)
		{
			// handle pause
			while (state.paused && !state.stop_requested)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				state.start_time = std::chrono::steady_clock::now();
			}

			if (state.stop_requested)
				break;

			// find track with earliest next event
			size_t next_track = ~0ULL;
			tick_type min_tick = ~0ULL;

			for (size_t i = 0; i < state.track_states.size(); ++i)
			{
				if (state.track_states[i].done)
					continue;

				if (state.track_states[i].next_event_tick < min_tick)
				{
					min_tick = state.track_states[i].next_event_tick;
					next_track = i;
				}
			}

			if (next_track == ~0ULL)
				break;

			auto& track = state.track_states[next_track];

			// update current tick/time
			state.current_tick = min_tick;
			state.current_time_us = tick_to_microseconds(min_tick);

			// wait until it's time to play this event
			auto elapsed = std::chrono::steady_clock::now() - state.start_time;
			auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
			int64_t target_us = static_cast<int64_t>(state.current_time_us - state.start_offset_us);

			if (target_us > elapsed_us)
			{
				auto wait_us = target_us - elapsed_us;
				if (wait_us > 1000)
				{
					std::this_thread::sleep_for(std::chrono::microseconds(wait_us - 500));
				}
				// spin-wait for precision
				while (true)
				{
					elapsed = std::chrono::steady_clock::now() - state.start_time;
					elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
					if (elapsed_us >= target_us)
						break;
				}
			}

			// process the event
			if (track.position >= track.end)
			{
				track.done = true;
				--state.active_tracks;
				continue;
			}

			uint8_t command = get_value_and_increment(track.position, track.end);
			uint8_t data1 = 0;
			uint8_t data2 = 0;

			if (command < 0x80)
			{
				// running status
				data1 = command;
				command = track.rsb;
			}
			else
			{
				if (command < 0xF0)
					track.rsb = command;

				if (command < 0xF0 || command == 0xFF)
					data1 = get_value_and_increment(track.position, track.end);
				else
					data1 = 0xFF;
			}

			if (command < 0x80)
			{
				track.done = true;
				--state.active_tracks;
				continue;
			}

			bool event_sent = false;
			switch (command >> 4)
			{
				case 0x8: // note off
				case 0x9: // note on
				case 0xA: // aftertouch
				case 0xB: // control change
				case 0xE: // pitch bend
				{
					data2 = get_value_and_increment(track.position, track.end);
					if (short_msg)
						short_msg(make_smsg(command, data1, data2));
					event_sent = true;
					break;
				}
				case 0xC: // program change
				case 0xD: // channel pressure
				{
					if (short_msg)
						short_msg(make_smsg(command, data1));
					event_sent = true;
					break;
				}
				case 0xF: // meta/sysex
				{
					uint8_t type = data1;
					bool end_of_track = (type == 0x2F && command == 0xFF);

					if (command == 0xFF)
						track.rsb = 0; // reset RSB on meta events

					if (end_of_track)
					{
						track.done = true;
						--state.active_tracks;
						continue;
					}

					// skip meta/sysex data (tempo already handled during initial parsing)
					auto length = get_vlv(track.position, track.end);
					track.position += length;
					break;
				}
			}

			// buffer note events for visualization with lookahead
			/*if (event_sent && ((command >> 4) == 0x8 || (command >> 4) == 0x9))
			{
				buffered_event ev;
				ev.tick = min_tick;
				ev.time_us = state.current_time_us;
				ev.status = command;
				ev.data1 = data1;
				ev.data2 = data2;
				state.event_buffer.push(std::move(ev));
			}*/

			// read next delta time
			if (track.position < track.end && !track.done)
			{
				auto delta = get_vlv(track.position, track.end);
				track.next_event_tick = min_tick + delta;
			}
			else
			{
				track.done = true;
				--state.active_tracks;
			}
		}

		// playback finished
		all_notes_off();
		state.playing = false;
	}
private:
	void try_init_kdmapi()
	{
		kdmapi_status = nullptr;

		auto moduleHandle = GetModuleHandleW(L"OmniMIDI");
		if (!moduleHandle)
			return;

		kdmapi_status = (decltype(kdmapi_status))GetProcAddress(moduleHandle, "IsKDMAPIAvailable");
		if (!kdmapi_status || !kdmapi_status())
			return;

		short_msg = (decltype(short_msg))GetProcAddress(moduleHandle, "SendDirectData");
	}

	void update_tempo_cache_at(size_t index) const
	{
		tcache.current_index = index;

		if (index == ~0ULL || info.time_map_mcsecs.empty())
		{
			// before first tempo change - use defaults
			tcache.base_tick = 0;
			tcache.base_time_us = 0;
			tcache.current_tempo = 500000;
			tcache.next_change_tick = info.time_map_mcsecs.empty()
				? ~0ULL
				: info.time_map_mcsecs[0].first;
			return;
		}

		const auto& entry = info.time_map_mcsecs[index];
		tcache.base_tick = entry.first;
		tcache.base_time_us = entry.second;

		// get tempo from tempo_tmp
		auto tempo_it = info.tempo_tmp.find(tcache.base_tick);
		tcache.current_tempo = (tempo_it != info.tempo_tmp.end()) ? tempo_it->second : 500000;

		// set next change tick
		if (index + 1 < info.time_map_mcsecs.size())
			tcache.next_change_tick = info.time_map_mcsecs[index + 1].first;
		else
			tcache.next_change_tick = ~0ULL;
	}

	uint64_t tick_to_microseconds(tick_type tick) const
	{
		// fast path: tick is within current cached tempo region
		if (tick >= tcache.base_tick && tick < tcache.next_change_tick)
		{
			uint64_t delta_ticks = tick - tcache.base_tick;
			return tcache.base_time_us + (delta_ticks * tcache.current_tempo) / info.ppq;
		}

		// check if we just crossed into the next tempo region (common case in sequential playback)
		if (tick >= tcache.next_change_tick && tcache.current_index != ~0ULL)
		{
			size_t next_idx = tcache.current_index + 1;
			if (next_idx < info.time_map_mcsecs.size())
			{
				// peek ahead - if tick is before the one after next, we found it
				tick_type after_next = (next_idx + 1 < info.time_map_mcsecs.size())
					? info.time_map_mcsecs[next_idx + 1].first
					: ~0ULL;

				if (tick < after_next)
				{
					update_tempo_cache_at(next_idx);
					uint64_t delta_ticks = tick - tcache.base_tick;
					return tcache.base_time_us + (delta_ticks * tcache.current_tempo) / info.ppq;
				}
			}
		}

		// slow path: binary search (for seeks or jumps)
		if (info.time_map_mcsecs.empty())
		{
			tcache.reset();
			return (tick * 500000ULL) / info.ppq;
		}

		auto it = std::upper_bound(
			info.time_map_mcsecs.begin(),
			info.time_map_mcsecs.end(),
			tick,
			[](tick_type t, const std::pair<uint64_t, uint64_t>& entry) {
				return t < entry.first;
			}
		);

		if (it == info.time_map_mcsecs.begin())
		{
			// tick is before first tempo change
			update_tempo_cache_at(~0ULL);
			return (tick * 500000ULL) / info.ppq;
		}

		--it;
		size_t idx = static_cast<size_t>(it - info.time_map_mcsecs.begin());
		update_tempo_cache_at(idx);

		uint64_t delta_ticks = tick - tcache.base_tick;
		return tcache.base_time_us + (delta_ticks * tcache.current_tempo) / info.ppq;
	}

	bool read_through_one_track(const uint8_t*& cur, const uint8_t* end)
	{
		tick_type current_tick = 0;

		uint32_t header = 0;
		uint32_t rsb = 0;
		uint32_t track_expected_size = 0;

		bool is_good = true;
		bool is_reading = true;

		auto get_byte = [&cur, end]() { return get_value_and_increment(cur, end); };
		while (header != MTrk_header && (cur < end)) //MTrk = 1297379947
			header = (header << 8) | get_byte();

		if (cur >= end)
			return true;

		for (int i = 0; i < 4; i++)
			track_expected_size = (track_expected_size << 8) | get_byte();

		const auto raw_track_data_begin = cur;
		while (cur < end && is_reading && is_good)
		{
			uint8_t command = 0;
			uint8_t param_buffer = 0;

			auto delta_time = get_vlv(cur, end);
			current_tick += delta_time;

			command = get_byte();
			if (command < 0x80)
			{
				param_buffer = command;
				command = rsb;
			}
			else
			{
				if (command < 0xF0)
					rsb = command;

				if (command < 0xF0 || command == 0xFF)
					param_buffer = get_byte();
				else
					param_buffer = 0xFF;
			}

			if (command < 0x80 || cur >= end)
			{
				ThrowAlert_Error("Byte " + std::to_string(cur - mmap->begin()) + ": Unexpected 0 RSB\nAt least one track was skipped!");
				is_good = false;
				break;
			}

			switch (command >> 4)
			{
				case 0x8: case 0x9:
				case 0xA: case 0xB: case 0xE:
				{
					get_byte();
					break;
				}
				case 0xC: case 0xD:
				{
					break;
				}
				case 0xF:
				{
					uint8_t com = command;
					uint8_t type = param_buffer;

					is_reading &= !(type == 0x2F && com == 0xFF);
					if (true /* disable if legacy rsb metas will misbehave again */)
						rsb = 0;

					if (type == 0x51 && com == 0xFF)
					{
						auto length_3 = get_byte();
						uint32_t value = (get_byte() << 16);
						value |= (get_byte() << 8);
						value |= (get_byte());

						info.tempo_tmp[current_tick] = value;
						continue;
					}

					auto length = get_vlv(cur, end);
					for (std::size_t i = 0; i < length; ++i)
						get_byte();

					if (!is_reading)
						continue;

					break;
				}
				default:
				{
					ThrowAlert_Warning("Byte " + (std::to_string(cur - mmap->begin()) + ": Unknown event type " + std::to_string(command)));
					is_good = false;
					break;
				}
			}
		}

		if (!is_good || is_reading)
			return false;

		if (track_expected_size != cur - raw_track_data_begin)
			warnings->report({static_cast<ptrdiff_t>(track_expected_size), cur - raw_track_data_begin}, "Track size mismatch");

		info.ticks_length = std::max(current_tick, info.ticks_length);

		track_info track;
		track.begining = raw_track_data_begin;
		track.ending = cur;

		info.tracks.push_back(std::move(track));

		return true;
	}

	inline static uint8_t get_value_and_increment(const uint8_t*& cur, const uint8_t* end)
	{
		if (cur < end)
			return *(cur++);
		return 0;
	}

	static uint64_t get_vlv(const uint8_t*& cur, const uint8_t* end)
	{
		uint64_t value = 0;

		uint8_t single_byte;
		do
		{
			single_byte = get_value_and_increment(cur, end);
			value = value << 7 | single_byte & 0x7F;
		}
		while (single_byte & 0x80);

		return value;
	}

	void update_devices()
	{
		devices.clear();
		current_device = ~0ULL;

		auto count = midiOutGetNumDevs();
		devices.reserve(count);

		for (int i = 0; i < count; i++)
		{
			MIDIOUTCAPSW out;
			auto ret = midiOutGetDevCapsW(i, &out, sizeof(out));
			if (ret != MMSYSERR_NOERROR)
				continue;

			devices.emplace_back(std::move(out));
		}

		if (devices.size())
			current_device = 0; // select first one
	}

	void close_midi_out()
	{
		int attempts = 0;

		if (!hout)
			return;

		auto hout_copy = hout.load();
		hout = nullptr;

		midiOutReset(hout_copy);
		while (midiOutClose(hout_copy) != MMSYSERR_NOERROR && attempts++ < 8)
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

		if (attempts == 8)
			ThrowAlert_Error("Unable to close the MIDI out");
	}

	void init_midi_out(size_t device)
	{
		auto hout_copy = hout.load();
		if (hout_copy)
			return;

		if (device >= devices.size())
			return;

		try
		{
			if (midiOutOpen(&hout_copy, device, 0, 0, 0) != MMSYSERR_NOERROR)
			{
				std::wstring name = devices[device].szPname;
				auto readable_name = std::string(name.begin(), name.end());
				ThrowAlert_Error("Unable to open MIDI out '" + readable_name + "'!");
				hout = nullptr;
			}
			else
			{
				hout = hout_copy;
				short_msg = [](uint32_t msg) { midiOutShortMsg(hout, msg); };
			}

			if (!kdmapi_status)
				try_init_kdmapi();
		}
		catch (...)
		{
			ThrowAlert_Error("midiOutOpen() horribly failed...\n");
		}
	}

	void all_notes_off_channel(uint8_t channel)
	{
		channel &= 0x0F;

		short_msg(make_smsg(0xB0 | channel, 120));
		short_msg(make_smsg(0xB0 | channel, 121));
		short_msg(make_smsg(0xB0 | channel, 123));
	}

	void all_notes_off()
	{
		for (uint8_t c = 0; c < 16; c++)
			all_notes_off_channel(c);
	}

	inline static uint32_t make_smsg(uint8_t prog, uint8_t arg1, uint8_t arg2 = 0)
	{
		return (uint32_t)prog | (arg1 << 8) | (arg2 << 16);
	}

	std::shared_ptr<logger_base> warnings;

	std::unique_ptr<bbb_mmap> mmap;

	midi_info info;
	playback_state state;
	mutable tempo_cache tcache;

	size_t current_device = ~0ULL;
	std::vector<MIDIOUTCAPSW> devices;
	inline static std::atomic<HMIDIOUT> hout;

	void(WINAPI* short_msg)(uint32_t msg) = nullptr;
	bool(WINAPI* kdmapi_status)() = nullptr;

	constexpr static std::uint32_t MTrk_header = 1297379947;
	constexpr static std::uint32_t MThd_header = 1297377380;
};

#endif