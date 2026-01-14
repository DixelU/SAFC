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
#include <queue>

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

	struct buffered_note
	{
		tick_type tick;
		tick_type length; // may be way larger than u64 though
		uint64_t time_us;
		uint32_t color_n : 24;
		uint32_t key : 8;
	};

	struct send_event
	{
		uint64_t time_us;    // target send time in microseconds from start
		uint32_t short_msg;  // prepared MIDI short message (0 = invalid/empty)
	};

	// Lock-free SPSC ring buffer for pre-parsed MIDI messages
	struct lookahead_buffer
	{
		static constexpr size_t buffer_size = 1 << 20; // must be power of 2
		static constexpr size_t buffer_mask = buffer_size - 1;

		std::unique_ptr<send_event[]> buffer;
		alignas(64) std::atomic<size_t> head{0}; // producer writes here
		alignas(64) std::atomic<size_t> tail{0}; // consumer reads here

		lookahead_buffer() : buffer(std::make_unique<send_event[]>(buffer_size)) {}

		void reset()
		{
			head.store(0, std::memory_order_relaxed);
			tail.store(0, std::memory_order_relaxed);
		}

		// Returns number of events in buffer
		size_t size() const
		{
			size_t h = head.load(std::memory_order_acquire);
			size_t t = tail.load(std::memory_order_acquire);
			return (h - t) & buffer_mask;
		}

		bool empty() const
		{
			return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
		}

		bool full() const
		{
			size_t h = head.load(std::memory_order_relaxed);
			size_t t = tail.load(std::memory_order_acquire);
			return ((h + 1) & buffer_mask) == t;
		}

		// Producer: push event (returns false if full)
		bool try_push(uint64_t time_us, uint32_t msg)
		{
			size_t h = head.load(std::memory_order_relaxed);
			size_t next_h = (h + 1) & buffer_mask;

			if (next_h == tail.load(std::memory_order_acquire))
				return false; // full

			buffer[h] = {time_us, msg};
			head.store(next_h, std::memory_order_release);
			return true;
		}

		// Consumer: peek at front event (returns nullptr if empty)
		const send_event* peek() const
		{
			size_t t = tail.load(std::memory_order_relaxed);
			if (t == head.load(std::memory_order_acquire))
				return nullptr;
			return &buffer[t];
		}

		// Consumer: pop front event
		void pop()
		{
			size_t t = tail.load(std::memory_order_relaxed);
			if (t != head.load(std::memory_order_acquire))
				tail.store((t + 1) & buffer_mask, std::memory_order_release);
		}

		// Consumer: pop and return (returns {0,0} if empty)
		send_event try_pop()
		{
			size_t t = tail.load(std::memory_order_relaxed);
			if (t == head.load(std::memory_order_acquire))
				return {0, 0};

			send_event ev = buffer[t];
			tail.store((t + 1) & buffer_mask, std::memory_order_release);
			return ev;
		}
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

	struct track_event_ref
	{
		tick_type tick;
		size_t track_index;
		bool operator>(const track_event_ref& other) const { return tick > other.tick; }
	};

	using event_queue_type = std::priority_queue<
		track_event_ref,
		std::vector<track_event_ref>,
		std::greater<track_event_ref>
	>;

	struct playback_state
	{
		tick_type current_tick;
		uint64_t current_time_us;
		std::chrono::steady_clock::time_point start_time;
		uint64_t start_offset_us;

		std::vector<track_playback_state> track_states;
		buffered_queue<buffered_note> notes;
		size_t active_tracks;

		std::atomic<bool> playing;
		mutable std::atomic<bool> stop_requested;
		mutable std::atomic<bool> paused;
		std::atomic<uint64_t> pause_position_us{0};  // MIDI time when paused (for proper resume)

		// Lookahead buffer for pre-parsed MIDI messages
		lookahead_buffer send_buffer;
		std::atomic<bool> parser_done{false};       // parser finished all events
		std::atomic<uint64_t> parsed_up_to_us{0};   // how far ahead the parser has reached (in us)
		std::atomic<uint64_t> sender_position_us{0}; // current sender playback position (in us)

		// Lookahead limits: parser throttles when too far ahead
		static constexpr uint64_t max_lookahead_us = 5000000;  // 5 seconds max lookahead
		static constexpr uint64_t min_lookahead_us = 1000000;  // 1 second min before resuming parse

		void reset()
		{
			current_tick = 0;
			current_time_us = 0;
			start_offset_us = 0;
			active_tracks = 0;
			playing = false;
			stop_requested = false;
			paused = false;
			pause_position_us = 0;
			parser_done = false;
			parsed_up_to_us = 0;
			sender_position_us = 0;
			track_states.clear();
			send_buffer.reset();
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

	// Pause playback - silences notes and remembers position
	void pause()
	{
		if (!state.playing || state.paused)
			return;

		// Record where we are in the MIDI timeline
		state.pause_position_us.store(state.current_time_us, std::memory_order_release);
		state.paused.store(true, std::memory_order_release);

		// Silence all notes immediately
		all_notes_off();
	}

	// Resume playback from paused position
	void resume()
	{
		if (!state.playing || !state.paused)
			return;

		// Update timing: set offset to where we paused, reset wall-clock start
		uint64_t pause_pos = state.pause_position_us.load(std::memory_order_acquire);
		state.start_offset_us = pause_pos;
		state.start_time = std::chrono::steady_clock::now();

		// Unpause - sender thread will continue from here
		state.paused.store(false, std::memory_order_release);
	}

	// Toggle pause state
	void toggle_pause()
	{
		if (state.paused.load(std::memory_order_acquire))
			resume();
		else
			pause();
	}

	// Stop playback completely
	void stop()
	{
		state.stop_requested.store(true, std::memory_order_release);

		// If paused, unpause so threads can exit
		if (state.paused.load(std::memory_order_acquire))
			state.paused.store(false, std::memory_order_release);
	}

	bool is_paused() const
	{
		return state.paused.load(std::memory_order_acquire);
	}

	bool is_playing() const
	{
		return state.playing.load(std::memory_order_acquire);
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

	// Parser thread: pre-parses MIDI events into the lookahead buffer
	void parser_thread_func()
	{
		update_tempo_cache_at(~0ULL); // initialize cache for tick 0

		// initialize track states and build priority queue
		event_queue_type event_queue;
		state.track_states.resize(info.tracks.size());

		for (size_t i = 0; i < info.tracks.size(); ++i)
		{
			state.track_states[i].init(info.tracks[i]);
			if (state.track_states[i].position < state.track_states[i].end)
			{
				state.track_states[i].next_event_tick =
					get_vlv(state.track_states[i].position, state.track_states[i].end);
				event_queue.push({state.track_states[i].next_event_tick, i});
			}
			else
			{
				state.track_states[i].done = true;
			}
		}

		state.active_tracks = info.tracks.size();

		while (!event_queue.empty() && !state.stop_requested)
		{
			// get the tick for this batch
			tick_type batch_tick = event_queue.top().tick;
			uint64_t batch_time_us = tick_to_microseconds(batch_tick);

			// Throttle: wait if we're too far ahead of the sender
			while (!state.stop_requested)
			{
				uint64_t sender_pos = state.sender_position_us.load(std::memory_order_acquire);
				uint64_t lookahead = (batch_time_us > sender_pos) ? (batch_time_us - sender_pos) : 0;

				if (lookahead < playback_state::max_lookahead_us)
					break;

				// Too far ahead - sleep until sender catches up closer
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			if (state.stop_requested)
				break;

			// update parsed position for sender thread visibility
			state.parsed_up_to_us.store(batch_time_us, std::memory_order_release);

			// process ALL events at this tick
			while (!event_queue.empty() && event_queue.top().tick == batch_tick)
			{
				if (state.stop_requested)
					break;

				auto ref = event_queue.top();
				event_queue.pop();

				auto& track = state.track_states[ref.track_index];

				if (track.done || track.position >= track.end)
				{
					if (!track.done)
					{
						track.done = true;
						--state.active_tracks;
					}
					continue;
				}

				// process the event
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

				uint32_t msg_to_send = 0;

				switch (command >> 4)
				{
					case 0x8: // note off
					case 0x9: // note on
					case 0xA: // aftertouch
					case 0xB: // control change
					case 0xE: // pitch bend
					{
						data2 = get_value_and_increment(track.position, track.end);
						msg_to_send = make_smsg(command, data1, data2);
						break;
					}
					case 0xC: // program change
					case 0xD: // channel pressure
					{
						msg_to_send = make_smsg(command, data1);
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

				// Push message to lookahead buffer (wait if buffer is full)
				if (msg_to_send != 0)
				{
					while (!state.send_buffer.try_push(batch_time_us, msg_to_send))
					{
						if (state.stop_requested)
							break;
						// Buffer full - yield briefly and retry
						std::this_thread::yield();
					}
				}

				// read next delta time and re-queue track if it has more events
				if (track.position < track.end && !track.done)
				{
					auto delta = get_vlv(track.position, track.end);
					track.next_event_tick = batch_tick + delta;
					event_queue.push({track.next_event_tick, ref.track_index});
				}
				else if (!track.done)
				{
					track.done = true;
					--state.active_tracks;
				}
			}
		}

		state.parser_done.store(true, std::memory_order_release);
	}

	// Sender thread: sends pre-parsed events with precise timing
	void sender_thread_func()
	{
		while (!state.stop_requested)
		{
			// handle pause - wait until unpaused
			if (state.paused.load(std::memory_order_acquire))
			{
				// Wait until resumed or stopped
				while (state.paused.load(std::memory_order_acquire) && !state.stop_requested)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

				if (state.stop_requested)
					break;

				// Resume: recalculate timing based on where we paused
				// start_offset_us was updated by resume(), start_time was reset
				// so we just continue - next event will be timed relative to new start
			}

			if (state.stop_requested)
				break;

			// peek at next event
			const send_event* ev = state.send_buffer.peek();

			if (!ev)
			{
				// buffer empty - check if parser is done
				if (state.parser_done.load(std::memory_order_acquire))
					break; // all done

				// wait for parser to produce more events
				std::this_thread::yield();
				continue;
			}

			uint64_t target_us = ev->time_us - state.start_offset_us;

			// update current position for UI and parser throttling
			state.current_time_us = ev->time_us;
			state.sender_position_us.store(ev->time_us, std::memory_order_release);

			// wait until it's time to send this event
			auto elapsed = std::chrono::steady_clock::now() - state.start_time;
			auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

			if (static_cast<int64_t>(target_us) > elapsed_us)
			{
				auto wait_us = target_us - elapsed_us;
				if (wait_us > 1000)
				{
					// sleep for most of the wait time
					std::this_thread::sleep_for(std::chrono::microseconds(wait_us - 50));
				}
				// spin-wait for final precision
				while (!state.stop_requested)
				{
					elapsed = std::chrono::steady_clock::now() - state.start_time;
					elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
					if (elapsed_us >= static_cast<int64_t>(target_us))
						break;
				}
			}

			// send the event
			if (short_msg && ev->short_msg != 0)
				short_msg(ev->short_msg);

			state.send_buffer.pop();
		}
	}

	void playback_thread()
	{
		state.reset();
		state.playing = true;
		state.start_time = std::chrono::steady_clock::now();

		// launch parser and sender threads
		std::thread parser_thread([this]() { parser_thread_func(); });
		std::thread sender_thread([this]() { sender_thread_func(); });

		// wait for both threads to finish
		parser_thread.join();
		sender_thread.join();

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