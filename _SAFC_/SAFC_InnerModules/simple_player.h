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
#include <array>
#include <ranges>
#include <mutex>

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

		if (!head->empty())
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

	T& front()
	{
		return head->front();
	}

	const T& front() const
	{
		return head->front();
	}

	T& back()
	{
		return tail->back();
	}

	const T& back() const
	{
		return tail->back();
	}

	// Iterator support for traversal (needed for note_off matching in visuals)
	struct iterator
	{
		slab* current_slab;
		T* cur;

		iterator(slab* s, T* p) : current_slab(s), cur(p) {}

		T& operator*() { return *cur; }
		T* operator->() { return cur; }

		iterator& operator++()
		{
			++cur;
			bool end_of_slab = (cur >= current_slab->end);

			if (end_of_slab && current_slab->next_slab)
			{
				current_slab = current_slab->next_slab.get();
				cur = current_slab->begin;
			}
			else if (end_of_slab)
				return iterator(nullptr, nullptr);

			return *this;
		}

		bool operator!=(const iterator& other) const
		{
			return cur != other.cur /* || current_slab != other.current_slab*/;
		}

		bool operator==(const iterator& other) const
		{
			return cur == other.cur /* && current_slab == other.current_slab */ ;
		}
	};

	iterator begin()
	{
		if (!head)
			return iterator(nullptr, nullptr);
		return iterator(head.get(), head->begin);
	}

	iterator end()
	{
		if (!tail)
			return iterator(nullptr, nullptr);
		return iterator(tail, tail->end);
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

	// Buffered note for notes display
	struct buffered_note
	{
		uint64_t start_time_us;   // note on time
		uint64_t end_time_us;     // note off time (0 = still held/pending)
		uint32_t color_id;        // (track_index << 4) | channel - unique color per track+channel
		//uint8_t velocity;
	};

	// Visuals viewport: manages notes display and keyboard state
	// Thread-safe: parser thread pushes notes, render thread reads state
	struct visuals_viewport
	{
		static constexpr size_t key_count = 128;

		mutable std::mutex mtx;  // protects all mutable state

		// Pending note tracking entry - points to note in queue awaiting its note_off
		struct pending_entry
		{
			uint32_t color_id;
			buffered_note* note_ptr;
		};

		// Fixed-size stack of pending notes per key (LIFO for matching)
		struct pending_list
		{
			std::vector<pending_entry> entries;

			void push(uint32_t color_id, buffered_note* ptr)
			{
				entries.emplace_back(color_id, ptr);
			}

			// Find and remove most recent note with matching color_id (LIFO)
			buffered_note* find_and_remove(uint32_t color_id)
			{
				for (auto it = entries.rbegin(); it != entries.rend(); ++it)
				{
					if (it->color_id != color_id)
						continue;

					buffered_note* result = it->note_ptr;
					// Swap with last and pop
					*it = entries.back();
					entries.pop_back();
					return result;
				}

				return nullptr;
			}

			void clear() { entries.clear(); }
		};

		// Held note info for keyboard coloring
		struct held_note
		{
			uint32_t color_id;
			uint8_t velocity;
		};

		// Per-key queues for falling notes visualization
		std::array<buffered_queue<buffered_note>, key_count> falling_notes;

		// Per-key pending note trackers for note_off matching
		std::array<pending_list, key_count> pending;

		// Generate unique color_id from track index and channel
		static uint32_t make_color_id(size_t track_index, uint8_t channel)
		{
			return static_cast<uint32_t>((track_index << 4) | (channel & 0x0F));
		}

		// Extract track index from color_id
		static size_t get_track_from_color(uint32_t color_id)
		{
			return color_id >> 4;
		}

		// Extract channel from color_id
		static uint8_t get_channel_from_color(uint32_t color_id)
		{
			return static_cast<uint8_t>(color_id & 0x0F);
		}

		// RAII lock guard for render code - hold this while iterating/reading visuals
		[[nodiscard]] std::unique_lock<std::mutex> lock() const
		{
			return std::unique_lock<std::mutex>(mtx);
		}

		// Push a note_on event - creates visual note and tracks as pending
		// Thread-safe: called from parser thread
		void push_note_on(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel, uint8_t velocity)
		{
			if (key >= key_count)
				return;

			std::lock_guard<std::mutex> guard(mtx);

			uint32_t color_id = make_color_id(track_index, channel);

			// Push to falling notes queue
			falling_notes[key].push({time_us, ~0ULL, color_id});

			// Track as pending for note_off matching
			buffered_note* note_ptr = &falling_notes[key].back();
			pending[key].push(color_id, note_ptr);
		}

		// Push a note_off event - finds matching pending note and sets end_time
		// Thread-safe: called from parser thread
		void push_note_off(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel)
		{
			if (key >= key_count)
				return;

			std::lock_guard<std::mutex> guard(mtx);

			uint32_t color_id = make_color_id(track_index, channel);

			// Find matching pending note and set its end time
			buffered_note* note = pending[key].find_and_remove(color_id);
			if (note)
				note->end_time_us = time_us;
		}

		// Remove notes that have fully scrolled past (end_time < cutoff)
		// Caller must hold lock() or ensure no concurrent access
		void cull_expired_unlocked(int64_t cutoff_time_us)
		{
			for (size_t key = 0; key < key_count; ++key)
			{
				auto& queue = falling_notes[key];
				while (!queue.empty())
				{
					auto& front = queue.front();
					// Only cull if note has ended and end is before cutoff
					if (front.end_time_us != ~0ULL && static_cast<int64_t>(front.end_time_us) < cutoff_time_us)
						queue.pop();
					else
						break;  // Queue is ordered by start_time, so stop if this one isn't expired
				}
			}
		}

		// Thread-safe version of cull_expired
		void cull_expired(uint64_t cutoff_time_us)
		{
			std::lock_guard<std::mutex> guard(mtx);
			cull_expired_unlocked(cutoff_time_us);
		}

		// Clear all visual state (unlocked version)
		void clear_unlocked()
		{
			for (auto& q : falling_notes)
				q.clear();
			for (auto& p : pending)
				p.clear();
		}

		// Thread-safe clear
		void clear()
		{
			std::lock_guard<std::mutex> guard(mtx);
			clear_unlocked();
		}

		// Reset for new playback (thread-safe)
		void reset()
		{
			clear();
		}
	};

	struct send_event
	{
		uint64_t time_us;    // target send time in microseconds from start
		uint32_t short_msg;  // prepared MIDI short message (0 = invalid/empty)
	};

	// Lock-free SPSC ring buffer for pre-parsed MIDI messages
	struct lookahead_buffer
	{
		static constexpr size_t buffer_size = 1 << 22; // must be power of 2
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
		visuals_viewport visuals;
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
		static constexpr uint64_t max_lookahead_us = 7500000;  // 5 seconds max lookahead
		static constexpr uint64_t min_lookahead_us = 2500000;  // 2.5 seconds min before resuming parse

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
			visuals.reset();
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

	// Get visuals viewport for rendering (non-const for update_keyboard_state/cull_expired)
	visuals_viewport& get_visuals()
	{
		return state.visuals;
	}

	const visuals_viewport& get_visuals() const
	{
		return state.visuals;
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

			// Throttle: wait if we're too far ahead of the sender, or if paused
			while (!state.stop_requested)
			{
				// Wait while paused (sleep longer to avoid busy-wait)
				if (state.paused.load(std::memory_order_acquire))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

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
					{
						data2 = get_value_and_increment(track.position, track.end);
						msg_to_send = make_smsg(command, data1, data2);

						// Push to visuals viewport
						uint8_t channel = command & 0x0F;
						uint8_t key = data1;

						state.visuals.push_note_off(key, batch_time_us, ref.track_index, channel);
						break;
					}
					case 0x9: // note on
					{
						data2 = get_value_and_increment(track.position, track.end);
						msg_to_send = make_smsg(command, data1, data2);

						// Push to visuals viewport (velocity 0 = note off)
						uint8_t channel = command & 0x0F;
						uint8_t key = data1;
						uint8_t velocity = data2;

						if (velocity > 0)
							state.visuals.push_note_on(key, batch_time_us, ref.track_index, channel, velocity);
						else
							state.visuals.push_note_off(key, batch_time_us, ref.track_index, channel);

						break;
					}
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
		// Set start_time BEFORE playing flag to avoid race with draw()
		state.start_time = std::chrono::steady_clock::now();
		state.playing.store(true, std::memory_order_release);

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

	struct draw_data
	{
		struct point { float x, y; };
		struct quad_geometry { point tl, tr, br, bl; };
		struct color { uint8_t r, g, b; };

		quad_geometry keyboard[128];
		uint8_t key_n[128];
		uint64_t scroll_window_us = 1 << 16;
		std::vector<quad_geometry> quads;
		std::vector<color> colors;
		//std::unordered_map<uint32_t, uint32_t> track_colors;

		constexpr static float WIDTH = 400, HEIGHT = 250;
		constexpr static bool is_white_key(std::uint8_t Key)
		{
			Key %= 12;
			if (Key < 5)
				return !(Key & 1);
			else
				return (Key & 1);
		}

		constexpr static int white_keys_count() 
		{
			int count = 0;
			for (uint8_t key = 0; key < 128; key++)
				count += is_white_key(key);

			return count;
		}

		void init(float keyboard_height, float black_hight, float black_margins)
		{
			constexpr int total = white_keys_count();
			constexpr float white_width = WIDTH / total;
			constexpr float general_connector_width = WIDTH / 128.25f;

			quad_geometry* first = keyboard, *last = keyboard + 127;

			for (uint8_t key = 0; key < 128; key++)
			{
				if (is_white_key(key))
				{
					uint32_t index = first - keyboard;

					first->bl.x = first->tl.x = index * white_width;
					first->br.x = first->tr.x = (index + 1) * white_width;

					first->bl.y = first->br.y = -0.5 * HEIGHT - keyboard_height;
					first->tl.y = first->tr.y = -0.5 * HEIGHT;

					key_n[index] = key;

					++first;
				}
				else
				{
					key_n[last - keyboard] = key;

					last->bl.x = last->tl.x = key * general_connector_width - black_margins;
					last->br.x = last->tr.x = (key + 1) * general_connector_width + black_margins;

					last->bl.y = last->br.y = -0.5 * HEIGHT - black_hight;
					last->tl.y = last->tr.y = -0.5 * HEIGHT;

					--last;
				}
			}
		}

		void move(float dx, float dy)
		{
			for (uint8_t key = 0; key < 128; key++)
			{
				quad_geometry& quad = keyboard[key];
				
				quad.bl.x += dx;
				quad.br.x += dx;
				quad.tl.x += dx;
				quad.tr.x += dx;

				quad.bl.y += dy;
				quad.br.y += dy;
				quad.tl.y += dy;
				quad.tr.y += dy;
			}
		}
	};

	void draw(const draw_data& data)
	{
		constexpr int total_white = draw_data::white_keys_count();

		auto& visuals = get_visuals();

		// Compute current playback position from wall-clock for smooth visualization
		uint64_t current_us;
		const auto& st = get_state();
		if (st.playing.load(std::memory_order_acquire) && !st.paused.load(std::memory_order_acquire))
		{
			auto elapsed = std::chrono::steady_clock::now() - st.start_time;
			current_us = st.start_offset_us + std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		}
		else if (st.paused.load(std::memory_order_acquire))
		{
			current_us = st.pause_position_us.load(std::memory_order_acquire);
		}
		else
		{
			current_us = 0;
		}

		auto guard = visuals.lock();  // hold for entire render frame

		visuals.cull_expired_unlocked(int64_t(current_us) - data.scroll_window_us);

		// Draw falling notes
		draw_data::color keyboard_colors[128];
		memset(keyboard_colors, 0xFF, sizeof(draw_data::color) * total_white);
		memset(keyboard_colors + total_white, 0x00, sizeof(draw_data::color) * (128 - total_white));

		for (uint8_t index = 0; index < 128; ++index)
		{
			uint8_t key = data.key_n[index];

			for (auto it = visuals.falling_notes[key].begin();
				it != visuals.falling_notes[key].end(); ++it)
			{
				auto& note = *it;

				int64_t start_offset = note.start_time_us - current_us;
				int64_t end_offset = note.end_time_us - current_us;

				float begin_y = float(start_offset) / float(data.scroll_window_us);
				float end_y = 1;

				if (note.end_time_us != ~0ULL)
					end_y = float(end_offset) / float(data.scroll_window_us);
				
				if (end_y < -0.01f || begin_y > 1.01f)
					continue;

				auto color_value = rotate(0xFF7F008F, note.color_id);

				if (begin_y <= 0 && end_y >= 0)
				{
					keyboard_colors[index] = draw_data::color{
						.r = uint8_t(color_value >> 24),
						.g = uint8_t(color_value >> 16),
						.b = uint8_t(color_value >> 8),
					};
				}

				begin_y = std::clamp(begin_y, 0.f, 1.f);
				end_y = std::clamp(end_y, 0.f, 1.f);

				begin_y = data.keyboard->tr.y + draw_data::HEIGHT * begin_y;
				end_y = data.keyboard->tr.y + draw_data::HEIGHT * end_y;

				//begin_y = (data.keyboard->tr.y + draw_data::HEIGHT) * (1 - begin_y) + (begin_y)*data.keyboard->tr.y;
				//end_y = (data.keyboard->tr.y + draw_data::HEIGHT) * (1 - end_y) + (end_y)*data.keyboard->tr.y;

				__glcolor(color_value | 0xFF);

				glBegin(GL_QUADS);
				glVertex2f(data.keyboard[index].tl.x, begin_y);
				glVertex2f(data.keyboard[index].tl.x, end_y);
				glVertex2f(data.keyboard[index].tr.x, end_y);
				glVertex2f(data.keyboard[index].tr.x, begin_y);
				glEnd();

				__glcolor(mul255_div_by_factor(color_value, 2) | 0xFF);
				glBegin(GL_LINE_LOOP);
				glVertex2f(data.keyboard[index].tl.x, begin_y);
				glVertex2f(data.keyboard[index].tl.x, end_y);
				glVertex2f(data.keyboard[index].tr.x, end_y);
				glVertex2f(data.keyboard[index].tr.x, begin_y);
				glEnd();
			}
		}

		guard.unlock();
		
		glBegin(GL_QUADS);
		for (uint8_t i = 0; i < 128; ++i)
		{
			const auto& key = data.keyboard[i];
			auto color = keyboard_colors[i];
			
			if (color.r == 0 && color.g == 0 && color.b == 0)
			{
				if (i < total_white)
					color = {1, 1, 1};
				else
					color = {0, 0, 0};
			}

			glColor3ub(color.r, color.g, color.b);
			glVertex2f(key.tl.x, key.tl.y);
			glVertex2f(key.tr.x, key.tr.y);
			glVertex2f(key.br.x, key.br.y);
			glVertex2f(key.bl.x, key.bl.y);
		}
		glEnd();

		auto& black_example = data.keyboard[127];

		glColor3ub(0, 0, 0);
		glBegin(GL_LINES);
		for (uint8_t i = 0; i < total_white - 1; ++i)
		{
			const auto& key = data.keyboard[i];

			auto note = i % 7;

			bool is_full = note == 2 || note == 6;

			glVertex2f(key.tr.x, is_full ? key.tr.y : black_example.br.y);
			glVertex2f(key.br.x, key.br.y);
		}

		for (uint8_t i = total_white; i < 128; ++i)
		{
			const auto& key = data.keyboard[i];

			glVertex2f(key.tr.x, key.tr.y);
			glVertex2f(key.br.x, key.br.y);
			glVertex2f(key.br.x, key.br.y);
			glVertex2f(key.bl.x, key.bl.y);
			glVertex2f(key.bl.x, key.bl.y);
			glVertex2f(key.tl.x, key.tl.y);
		}
		glEnd();
	}

private:

	static uint32_t mul255_div_by_factor(uint32_t rgba, uint32_t divisor)
	{
		// Works well when divisor is small constant: 2,3,4,5,6,8,10,...
		// divisor must be > 0

		constexpr uint32_t MAGIC_R = (1u << 24) / 255u + 1;  // ~ 0x01010101 when using 1<<24

		// 1. unpack
		uint32_t rb = rgba & 0x00FF00FFu;          // R and B in low 8 bits each
		uint32_t ga = (rgba >> 8) & 0x00FF00FFu;   // G and A

		// 2. multiply + add rounding bias
		rb = ((rb * MAGIC_R) >> 24) / divisor;
		ga = ((ga * MAGIC_R) >> 24) / divisor;

		// 3. saturate (optional but strongly recommended)
		rb += ((rb >> 8) & 1) * 0x00FF00FFu;   // cheap saturate to 255
		ga += ((ga >> 8) & 1) * 0x00FF00FFu;

		// 4. repack
		return (ga << 8) | rb;
	}

	static uint32_t rotate(uint32_t color, uint32_t shift)
	{
		auto rem = shift % 32;

		return color << (32 - rem) | color >> rem;
	}

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

struct PlayerViewer : public HandleableUIPart
{
	float xpos, ypos;
	std::unique_ptr<simple_player::draw_data> data;

	PlayerViewer(float xpos, float ypos):
		xpos(xpos),
		ypos(ypos),
		data(std::make_unique<simple_player::draw_data>())
	{
		data->init(40, 22.5f, 0);
		data->move(xpos - 0.5 * simple_player::draw_data::WIDTH, ypos);
	}

	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		player->draw(*data);
	}

	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		
		xpos += dx;
		ypos += dy;

		data->move(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		
		NewX -= xpos;
		NewY -= ypos;

		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * simple_player::draw_data::WIDTH,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * simple_player::draw_data::HEIGHT;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void KeyboardHandler(CHAR CH) override
	{
		return;
	}
	void SafeStringReplace(std::string Meaningless) override
	{
		return;
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		return 0;
	}
};

#endif