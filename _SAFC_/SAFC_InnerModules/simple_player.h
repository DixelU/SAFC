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
#include <new>
#include <limits>

#include <memory_mapped_file_reader.h>

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"
#include "playback_event_source.h"

#define SIMPLE_PLAYER_FORCE_NO_INLINE
#include <buffered_queue_spsc.h>
#include <midi_overlap_filter.h>
#include <midi_visual_note_store.h>
// __declspec(noinline)
// __declspec(noinline)

// lock-free SPSC slab-based queue
// Producer (parser): push, back
// Consumer (renderer): pop, front, empty, iteration
struct simple_player
{
	using tick_type = std::uint64_t;

	struct track_info
	{
		const uint8_t* begining;
		const uint8_t* ending;
	};

	using visual_note_store = dixelu::midi_visual_note_store<128>;
	using buffered_note = visual_note_store::note;

	// Visuals viewport: parser publishes note lifecycle batches while the render
	// thread owns the visible-note lists used for drawing and retirement.
	struct visuals_viewport
	{
		static constexpr size_t key_count = visual_note_store::key_count;

		// Pending note tracking entry - points to note in queue awaiting its note_off
		// Only accessed by parser thread - no synchronization needed
		struct pending_entry
		{
			buffered_note* note_ptr;
			uint32_t track_id;
		};

		// Per-track_id LIFO stacks of pending notes per key.
		// Flat open-addressing hash table (linear probing, power-of-two size):
		// a lookup is one probe into a contiguous array instead of
		// unordered_map's bucket->node pointer chase, and emptied stacks keep
		// their slot and capacity, so steady-state playback doesn't allocate.
		// Parser-private: only accessed by parser thread
		struct pending_list
		{
			// track_id is (track_index << 4) | channel and never reaches ~0u
			static constexpr uint32_t empty_id = ~0u;

			struct slot
			{
				uint32_t track_id = empty_id;
				std::vector<buffered_note*> stack; // LIFO of notes awaiting note_off
			};

			std::vector<slot> slots; // power-of-two size
			size_t used = 0;         // occupied slots (including emptied stacks)

			static size_t hash(uint32_t id)
			{
				// Fibonacci hash: track_ids are dense sequential ints
				return static_cast<size_t>((id * 0x9E3779B97F4A7C15ull) >> 32);
			}

			// Returns the slot holding track_id, or the empty slot where it
			// belongs. Load factor stays below 3/4, so the probe terminates.
			slot* find_slot(uint32_t track_id)
			{
				const size_t mask = slots.size() - 1;
				size_t i = hash(track_id) & mask;
				while (slots[i].track_id != track_id && slots[i].track_id != empty_id)
					i = (i + 1) & mask;
				return &slots[i];
			}

			void grow()
			{
				std::vector<slot> old(slots.empty() ? 16 : slots.size() * 2);
				old.swap(slots);
				for (auto& s : old)
					if (s.track_id != empty_id)
						*find_slot(s.track_id) = std::move(s);
			}

			void push(uint32_t track_id, buffered_note* ptr)
			{
				if (used * 4 >= slots.size() * 3)
					grow();

				slot* s = find_slot(track_id);
				if (s->track_id == empty_id)
				{
					s->track_id = track_id;
					++used;
				}
				s->stack.push_back(ptr);
			}

			buffered_note* find_and_remove(uint32_t track_id)
			{
				if (slots.empty())
					return nullptr;

				slot* s = find_slot(track_id);
				if (s->track_id == empty_id || s->stack.empty())
					return nullptr;

				buffered_note* result = s->stack.back();
				s->stack.pop_back();
				return result;
			}

			void clear()
			{
				std::vector<slot>().swap(slots);
				used = 0;
			}
		};

		// Stable notes are published to a renderer-owned intrusive list. Completed
		// notes retire through an end-time FIFO, so an earlier held note cannot
		// pin every later note on the same key in memory.
		visual_note_store notes;

		// Per-key pending note trackers for note_off matching
		// Parser-private: only accessed by parser thread
		std::array<pending_list, key_count> pending;

		// Mutex held by render thread during cull+iteration and by playback_thread during reset().
		// Prevents data race between visuals.reset() (clear) and concurrent render iteration.
		mutable std::mutex access_mutex;

		// Generate unique track_id from track index and channel
		static uint32_t make_color_id(size_t track_index, uint8_t channel)
		{
			return static_cast<uint32_t>((track_index << 4) | (channel & 0x0F));
		}

		// Extract track index from track_id
		static size_t get_track_from_track_id(uint32_t track_id)
		{
			return track_id >> 4;
		}

		// Extract channel from track_id
		static uint8_t get_channel_from_track_id(uint32_t track_id)
		{
			return static_cast<uint8_t>(track_id & 0x0F);
		}

		// Push a note_on event - creates visual note and tracks as pending
		// Called from parser thread only - no locking needed
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_on(uint8_t key,
			uint64_t time_us, tick_type tick, size_t track_index,
			uint8_t channel, uint8_t velocity)
		{
			if (key >= key_count)
				return;

			uint32_t track_id = make_color_id(track_index, channel);

			buffered_note* note_ptr = notes.create(key, time_us, track_id, tick);
			pending[key].push(track_id, note_ptr);
		}

		// Push a note_off event - finds matching pending note and sets end_time
		// Called from parser thread only - atomic store for end_time_us
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_off(uint8_t key,
			uint64_t time_us, tick_type tick, size_t track_index, uint8_t channel)
		{
			if (key >= key_count)
				return;

			uint32_t track_id = make_color_id(track_index, channel);

			// Find matching pending note and set its end time (atomic store)
			buffered_note* note = pending[key].find_and_remove(track_id);
			if (note)
				notes.finish(note, time_us, tick);
		}

		// Incorporate parser additions and retire completed notes in ending order.
		// Called from the render thread while access_mutex is held.
		SIMPLE_PLAYER_FORCE_NO_INLINE size_t update_visible(int64_t cutoff_time_us)
		{
			return notes.update(cutoff_time_us);
		}

		const buffered_note* first_visible_note(size_t key) const
		{
			return notes.first_visible(key);
		}

		// Parser-side collection of records retired by the renderer.
		void reclaim_retired()
		{
			notes.reclaim();
		}

		void flush_lifecycle()
		{
			notes.flush();
		}

		// Clear all visual state
		// Should only be called when no concurrent access (e.g., during reset)
		void clear()
		{
			for (auto& p : pending)
				p.clear();
			notes.reset();
		}

		// reset for new playback
		// Acquires access_mutex to synchronize with render thread iteration.
		void reset()
		{
			std::lock_guard<std::mutex> lock(access_mutex);
			clear();
		}
	};

	struct send_event
	{
		uint64_t time_us;    // target send time in microseconds from start
		uint32_t short_msg;  // prepared MIDI short message (0 = invalid/empty)
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

	// Custom min-heap for track event scheduling.
	// replace_top() merges pop+push into one sift-down: O(log n) instead of O(2 log n).
	// Used in the parser inner loop where every event does pop+push.
	struct track_event_heap
	{
		std::vector<track_event_ref> data;

		bool empty() const { return data.empty(); }
		const track_event_ref& top() const { return data[0]; }

		void push(track_event_ref val)
		{
			data.push_back(val);
			sift_up(data.size() - 1);
		}

		void pop()
		{
			data[0] = data.back();
			data.pop_back();
			if (!data.empty())
				sift_down(0);
		}

		// Replace root with val and restore heap with one sift-down.
		// Saves the sift-up that push() would require.
		void replace_top(track_event_ref val)
		{
			data[0] = val;
			sift_down(0);
		}

	private:
		// operator> is tick > other.tick; min-heap: parent.tick <= child.tick
		void sift_up(size_t i)
		{
			while (i > 0)
			{
				size_t parent = (i - 1) >> 1;
				if (!(data[parent] > data[i])) break; // parent.tick <= child.tick: OK
				std::swap(data[i], data[parent]);
				i = parent;
			}
		}

		void sift_down(size_t i)
		{
			size_t n = data.size();
			while (true)
			{
				size_t smallest = i;
				size_t left  = 2 * i + 1;
				size_t right = 2 * i + 2;
				if (left  < n && data[smallest] > data[left])  smallest = left;
				if (right < n && data[smallest] > data[right]) smallest = right;
				if (smallest == i) break;
				std::swap(data[i], data[smallest]);
				i = smallest;
			}
		}
	};

	struct playback_state
	{
		// tick_type current_tick;
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

		// Seek state (written by UI thread, read by playback_thread after join)
		std::atomic<bool> seek_requested{false};
		std::atomic<uint64_t> seek_target_us{0};
		std::atomic<bool> seek_resume_paused{false};

		// Lookahead buffer for pre-parsed MIDI messages (SPSC; parser throttles
		// when it gets too far ahead in time or holds too many pending events).
		dixelu::buffered_queue_spsc<send_event> send_buffer;
		std::atomic<bool> seeking_ff{false};        // parser is fast-forwarding (sender drains immediately)
		std::atomic<bool> parser_done{false};       // parser finished all events
		std::atomic<uint64_t> parsed_up_to_us{0};   // how far ahead the parser has reached (in us)
		std::atomic<uint64_t> sender_position_us{0}; // current sender playback position (in us)
		std::atomic<bool> memory_failure{false};

		// Lookahead limits: parser throttles when too far ahead
		static constexpr uint64_t max_lookahead_us = 5000000;  // 5 seconds max lookahead
		static constexpr uint64_t min_lookahead_us = 2500000;  // 2.5 seconds min before resuming parse

		// Event-count throttle for dense tick clusters where the time-based
		// throttle alone cannot fire. Keep the established 64M-event lookahead:
		// reducing this to 1M serializes the parser and sender on black MIDIs.
		static constexpr size_t max_pending_events = 1u << 26;

		void reset()
		{
			current_time_us = 0;
			start_offset_us = 0;
			active_tracks = 0;
			playing = false;
			stop_requested = false;
			paused = false;
			pause_position_us = 0;
			seek_requested = false;
			seek_target_us = 0;
			seek_resume_paused = false;
			seeking_ff = false;
			parser_done = false;
			parsed_up_to_us = 0;
			sender_position_us = 0;
			memory_failure = false;
			track_states.clear();
			send_buffer.clear();
			visuals.reset();
		}
	};

	struct midi_info
	{
		std::vector<track_info> tracks;
		std::map<uint64_t, uint32_t> tempo_tmp;
		std::vector<std::pair<uint64_t, uint64_t>> time_map_mcsecs;
		uint64_t ticks_length;
		uint64_t total_duration_us{0};

		volatile uint64_t scanned{0};
		volatile bool open_complete{false};
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
		//init_midi_out(devices.size() - 1);

		warnings = std::make_shared<printing_logger>("33");
	}

	// Get device names as vector of strings for UI
	std::vector<std::string> get_device_names() const
	{
		std::vector<std::string> names;
		names.reserve(devices.size());

		for (const auto& device : devices)
		{
			std::wstring wname = device.szPname;
			std::string name(wname.size(), '\0');
			std::transform(wname.begin(), wname.end(), name.begin(), [](wchar_t c) { return static_cast<char>(c); });
			names.push_back(std::move(name));
		}

		return names;
	}

	// Get currently selected device index
	size_t get_current_device() const
	{
		return current_device;
	}

	// Change the current device
	bool set_device(size_t device_index)
	{
		// Device teardown/open and playback both mutate the output sink. Refuse a
		// device switch while a run owns the player instead of racing its sender.
		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
			return false;

		if (device_index >= devices.size())
			return false;
		if (device_index == current_device && has_output())
			return true;

		// Close current device if open
		close_midi_out();

		// Open new device
		current_device = device_index;
		init_midi_out(device_index);

		// Call callback if set
		if (on_device_changed)
			on_device_changed(device_index);

		return has_output();
	}

	// Whether a MIDI out sink is ready for immediate messages
	bool has_output() const
	{
		return hout.load(std::memory_order_acquire) != nullptr && short_msg != nullptr;
	}

	// Send an immediate note on/off outside of playback (piano roll audition)
	void preview_note(uint8_t channel, uint8_t key, uint8_t velocity, bool on)
	{
		if (!short_msg)
			return;

		short_msg(make_smsg((on ? 0x90 : 0x80) | (channel & 0x0F), key & 0x7F, on ? (velocity & 0x7F) : 0x40));
	}

	// Restore device by name (for registry persistence)
	bool restore_device_by_name(const std::wstring& device_name)
	{
		if (device_name.empty())
			return false;

		for (size_t i = 0; i < devices.size(); ++i)
		{
			if (devices[i].szPname != device_name)
				continue;

			return set_device(i);
		}

		return false;
	}

	// Make playback usable even when the persisted device name is empty or no
	// longer exists. update_devices() selects index 0 but intentionally does not
	// open it, so callers must fall back to opening that current device.
	bool ensure_output(const std::wstring& preferred_device_name)
	{
		if (has_output())
			return true;
		if (restore_device_by_name(preferred_device_name))
			return true;
		return set_device(current_device);
	}

	// Callback for when device changes (for UI updates)
	void(*on_device_changed)(size_t device_index) = nullptr;

	void simple_run(std::wstring filename, double start_fraction = 0.0)
	{
		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
			return;

		memory_failure_reported.store(false, std::memory_order_release);
		cancel_requested.store(false, std::memory_order_release);

		try
		{
			auto res = open(filename);
			info.open_complete = true;

			if (cancel_requested.load(std::memory_order_acquire))
				return;

			if (!res)
			{
				throw_alert_error("Playback failed");
				mmap.reset();
			return;
		}

			start_fraction = std::clamp(start_fraction, 0.0, 1.0);
		playback_thread(static_cast<uint64_t>(start_fraction * info.total_duration_us));

		// Release the mapping so the caller can delete or replace the file.
		// info.tracks' pointers into it stay dead until the next open().
		mmap.reset();
		}
		catch (const std::bad_alloc&)
		{
			info.open_complete = true;
			handle_memory_failure(true);
		}
	}

	// Play events from an external source (e.g. the MIDI editor's live in-memory
	// notes) instead of a file on disk. Mirrors simple_run: blocks until playback
	// ends. The caller owns 'src' and must keep it alive for the whole call.
	void run_from_external(playback_event_source* src, double start_fraction = 0.0,
		bool start_paused = true)
	{
		if (!src)
			return;

		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
			return;

		memory_failure_reported.store(false, std::memory_order_release);
		cancel_requested.store(false, std::memory_order_release);

		try
		{
			// The external parser converts ticks to time itself, so info only
			// needs the total duration for seek-fraction mapping and the UI bar.
			info = midi_info{};
			info.total_duration_us = src->total_duration_us();
			info.open_complete = true;

			start_fraction = std::clamp(start_fraction, 0.0, 1.0);

			external_source = src;
			playback_thread(static_cast<uint64_t>(start_fraction * info.total_duration_us),
				start_paused);
			external_source = nullptr;
		}
		catch (const std::bad_alloc&)
		{
			external_source = nullptr;
			info.open_complete = true;
			handle_memory_failure(true);
		}
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

	// Seek to a position in the file (0.0 = start, 1.0 = end)
	void seek_to(double fraction)
	{
		if (!state.playing.load(std::memory_order_acquire))
			return;

		fraction = std::clamp(fraction, 0.0, 1.0);
		uint64_t target_us = static_cast<uint64_t>(fraction * info.total_duration_us);

		// Remember whether we were paused so we can restore after seek
		state.seek_resume_paused.store(state.paused.load(std::memory_order_acquire), std::memory_order_relaxed);
		state.seek_target_us.store(target_us, std::memory_order_relaxed);
		state.seek_requested.store(true, std::memory_order_release);

		// Signal threads to stop (unpause if needed so they can exit)
		if (state.paused.load(std::memory_order_acquire))
			state.paused.store(false, std::memory_order_release);
		state.stop_requested.store(true, std::memory_order_release);
	}

	// Stop playback completely
	void stop()
	{
		cancel_requested.store(true, std::memory_order_release);
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

	// True while the parser is fast-forwarding to a seek target. A caller that
	// unpauses after starting playback must wait for this to clear, else it can
	// resume() before the seek's own re-pause and lose the race (stuck paused).
	bool is_fast_forwarding() const
	{
		return state.seeking_ff.load(std::memory_order_acquire);
	}

	const std::wstring& get_filename() const { return current_filename; }

	// Current playback position in microseconds, wall-clock-smooth (same timing
	// the falling-notes visualization uses). Returns the pause point while
	// paused and 0 when stopped.
	uint64_t get_position_us() const
	{
		if (state.playing.load(std::memory_order_acquire) &&
			!state.paused.load(std::memory_order_acquire))
		{
			auto elapsed = std::chrono::steady_clock::now() - state.start_time;
			return state.start_offset_us +
				std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		}
		if (state.paused.load(std::memory_order_acquire))
			return state.pause_position_us.load(std::memory_order_acquire);
		return 0;
	}

	bool open(std::wstring filename)
	{
		current_filename = filename;
		// prerequisite: this is a midi file with valid header;
		mmap = std::make_unique<dixelu::memory_mapped_file_reader>(filename);
		info = midi_info{};

		if (!mmap || !mmap->good())
		{
			throw_alert_warning("Unable to open MIDI file for playback");
			return false;
		}

		const auto begin = reinterpret_cast<const std::uint8_t*>(mmap->data());
		const auto size = mmap->size();
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
			if (cancel_requested.load(std::memory_order_acquire))
				return false;

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

		// Compute total duration from ticks_length using the tempo map
		{
			uint64_t dur_tick = info.ticks_length;
			uint64_t dur_base_tick = previous_tick;
			uint64_t dur_base_us = info.time_map_mcsecs.empty() ? 0 : info.time_map_mcsecs.back().second;
			uint32_t dur_tempo = previous_tempo;
			info.total_duration_us = dur_base_us + (dur_tick - dur_base_tick) * dur_tempo / info.ppq;
		}

		return true;
	}

	// Parser thread: pre-parses MIDI events into the lookahead buffer
	// skip_to_us > 0: fast-forward to that time, pushing channel state to buffer for sender
	SIMPLE_PLAYER_FORCE_NO_INLINE void parser_thread_func(uint64_t skip_to_us = 0, bool pause_after_seek = false)
	{
		update_tempo_cache_at(~0ULL); // initialize cache for tick 0

		bool fast_forwarding = (skip_to_us > 0);
		if (fast_forwarding)
			state.seeking_ff.store(true, std::memory_order_release);

		// initialize track states and build priority queue
		track_event_heap event_queue;
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

		uint32_t cap_check_counter = 0;

		while (!event_queue.empty() && !state.stop_requested)
		{
			// get the tick for this batch
			tick_type batch_tick = event_queue.top().tick;
			uint64_t batch_time_us = tick_to_microseconds(batch_tick);

			// Check if fast-forward is complete
			if (fast_forwarding && batch_time_us >= skip_to_us)
			{
				fast_forwarding = false;

				// Set timing so sender starts from the seek position
				state.start_offset_us = skip_to_us;
				state.start_time = std::chrono::steady_clock::now();
				state.current_time_us = skip_to_us;
				state.sender_position_us.store(skip_to_us, std::memory_order_release);
				state.parsed_up_to_us.store(skip_to_us, std::memory_order_release);

				// Re-pause (if requested) BEFORE clearing seeking_ff, so a waiter
				// that resumes on !is_fast_forwarding() already observes paused ==
				// true and its resume() wins instead of racing the re-pause.
				if (pause_after_seek)
				{
					state.pause_position_us.store(skip_to_us, std::memory_order_release);
					state.paused.store(true, std::memory_order_release);
				}

				// Signal sender to transition from immediate drain to timed playback
				state.seeking_ff.store(false, std::memory_order_release);
			}

			if (!fast_forwarding)
			{
				// Throttle: wait if we're too far ahead in time, have too many
				// pending events buffered, or are paused.
				while (!state.stop_requested)
				{
					state.visuals.reclaim_retired();

					// Wait while paused (sleep longer to avoid busy-wait)
					if (state.paused.load(std::memory_order_acquire))
					{
						state.visuals.flush_lifecycle();
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}

					uint64_t sender_pos = state.sender_position_us.load(std::memory_order_acquire);
					uint64_t lookahead = (batch_time_us > sender_pos) ? (batch_time_us - sender_pos) : 0;
					size_t pending = state.send_buffer.approximate_size();

					if (lookahead < playback_state::max_lookahead_us &&
						pending < playback_state::max_pending_events)
						break;

					// Too far ahead - sleep until sender catches up closer
					state.visuals.flush_lifecycle();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				if (state.stop_requested)
					break;

				// update parsed position for sender thread visibility
				state.parsed_up_to_us.store(batch_time_us, std::memory_order_release);
			}

			// process ALL events at this tick
			while (!event_queue.empty() && event_queue.top().tick == batch_tick)
			{
				if (state.stop_requested)
					break;

				auto ref = event_queue.top();
				// pop/replace_top deferred until the end of this track's run

				auto& track = state.track_states[ref.track_index];

				if (track.done || track.position >= track.end)
				{
					if (!track.done)
					{
						track.done = true;
						--state.active_tracks;
					}
					event_queue.pop();
					continue;
				}

				// Process a run of events from this track. Black MIDIs store
				// chords as consecutive zero-delta events within one track, so
				// while the next delta is zero stay on this track and skip the
				// heap entirely.
				bool reschedule = false;
				for (;;)
				{
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
						break;
					}

					uint32_t msg_to_send = 0;

					switch (command >> 4)
					{
						case 0x8: // note off
						{
							data2 = get_value_and_increment(track.position, track.end);

							if (!fast_forwarding)
							{
								msg_to_send = make_smsg(command, data1, data2);
								uint8_t channel = command & 0x0F;
								state.visuals.push_note_off(
									data1, batch_time_us, batch_tick, ref.track_index, channel);
							}
							break;
						}
						case 0x9: // note on
						{
							data2 = get_value_and_increment(track.position, track.end);

							if (!fast_forwarding)
							{
								msg_to_send = make_smsg(command, data1, data2);
								uint8_t channel = command & 0x0F;

								if (data2 > 0)
									state.visuals.push_note_on(
										data1, batch_time_us, batch_tick, ref.track_index, channel, data2);
								else
									state.visuals.push_note_off(
										data1, batch_time_us, batch_tick, ref.track_index, channel);
							}
							break;
						}
						case 0xA: // aftertouch (transient - skip during fast-forward)
						{
							data2 = get_value_and_increment(track.position, track.end);
							if (!fast_forwarding)
								msg_to_send = make_smsg(command, data1, data2);
							break;
						}
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
								break;
							}

							// skip meta/sysex data (tempo already handled during initial parsing)
							auto length = get_vlv(track.position, track.end);
							track.position += length;
							break;
						}
					}

					if (track.done) // end of track / RSB error above
						break;

					// Push message to lookahead buffer.
					if (msg_to_send != 0)
					{
						state.send_buffer.push({batch_time_us, msg_to_send});

						// Per-batch (outer) throttle only fires between unique
						// ticks, so a single dense tick could otherwise push
						// millions of events before the next check. Enforce the
						// size cap here too, sampled every 1024 pushes: checking
						// the consumer-written pop counter on every push would
						// reintroduce the cross-core traffic the split queue
						// counters removed, and against a 2^26 cap an overshoot
						// of up to 1023 events is noise.
						if ((++cap_check_counter & 1023u) == 0)
						{
							while (!fast_forwarding && !state.stop_requested &&
								state.send_buffer.approximate_size() >= playback_state::max_pending_events)
							{
								if (state.paused.load(std::memory_order_acquire))
									std::this_thread::sleep_for(std::chrono::microseconds(100));
								else
									std::this_thread::sleep_for(std::chrono::microseconds(10));
							}
						}
					}

					if (track.position >= track.end)
					{
						track.done = true;
						--state.active_tracks;
						break;
					}

					auto delta = get_vlv(track.position, track.end);
					if (delta != 0)
					{
						track.next_event_tick = batch_tick + delta;
						reschedule = true;
						break;
					}

					// delta == 0: this track's next event is at the same tick
					if (state.stop_requested)
						break;
				}

				// Re-schedule track or remove from heap.
				// replace_top = one sift-down vs pop+push = O(log n) instead of O(2 log n).
				if (reschedule)
					event_queue.replace_top({track.next_event_tick, ref.track_index});
				else
					event_queue.pop();
			}
		}

		state.visuals.flush_lifecycle();
		state.visuals.reclaim_retired();
		state.parser_done.store(true, std::memory_order_release);
	}

	// Parser variant that pulls from an external event source instead of the
	// mmap'd file. Shares the sender thread, timing, throttle, visuals, pause
	// and seek machinery with the file path — only event production differs.
	// skip_to_us > 0: fast-forward to that time, emitting controller/program
	// state (not notes) so the synth is correct at the seek point.
	SIMPLE_PLAYER_FORCE_NO_INLINE void parser_from_source_thread_func(
		playback_event_source* src, uint64_t skip_to_us = 0, bool pause_after_seek = false)
	{
		bool fast_forwarding = (skip_to_us > 0);
		if (fast_forwarding)
		{
			state.seeking_ff.store(true, std::memory_order_release);
			// Jump the source to the target (skips the note prefix) rather than
			// walking every event up to it — critical in dense regions.
			src->seek(skip_to_us);
		}
		else
			src->rewind();

		uint32_t cap_check_counter = 0;
		generated_event ev;

		while (!state.stop_requested && src->next(ev))
		{
			uint64_t batch_time_us = ev.time_us;

			// Check if fast-forward is complete
			if (fast_forwarding && batch_time_us >= skip_to_us)
			{
				fast_forwarding = false;

				// Set timing so the sender starts from the seek position
				state.start_offset_us = skip_to_us;
				state.start_time = std::chrono::steady_clock::now();
				state.current_time_us = skip_to_us;
				state.sender_position_us.store(skip_to_us, std::memory_order_release);
				state.parsed_up_to_us.store(skip_to_us, std::memory_order_release);

				// Re-pause (if requested) BEFORE clearing seeking_ff, so a waiter
				// that resumes on !is_fast_forwarding() already observes paused ==
				// true and its resume() wins instead of racing the re-pause.
				if (pause_after_seek)
				{
					state.pause_position_us.store(skip_to_us, std::memory_order_release);
					state.paused.store(true, std::memory_order_release);
				}

				// Signal sender to transition from immediate drain to timed playback
				state.seeking_ff.store(false, std::memory_order_release);
			}

			if (!fast_forwarding)
			{
				// Throttle: wait if too far ahead in time, too many pending events
				// buffered, or paused. Identical policy to the file parser.
				while (!state.stop_requested)
				{
					state.visuals.reclaim_retired();

					if (state.paused.load(std::memory_order_acquire))
					{
						state.visuals.flush_lifecycle();
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}

					uint64_t sender_pos = state.sender_position_us.load(std::memory_order_acquire);
					uint64_t lookahead = (batch_time_us > sender_pos) ? (batch_time_us - sender_pos) : 0;
					size_t pending = state.send_buffer.approximate_size();

					if (lookahead < playback_state::max_lookahead_us &&
						pending < playback_state::max_pending_events)
						break;

					state.visuals.flush_lifecycle();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				if (state.stop_requested)
					break;

				state.parsed_up_to_us.store(batch_time_us, std::memory_order_release);
			}

			const bool is_note = (ev.k == generated_event::kind::note_on ||
				ev.k == generated_event::kind::note_off);

			// During fast-forward, drop notes but keep controller/program state so
			// the synth sounds correct at the seek target (matches the file parser).
			if (fast_forwarding && is_note)
				continue;

			// Falling-notes visuals (only in timed playback, matching the file path)
			if (!fast_forwarding)
			{
				if (ev.k == generated_event::kind::note_on)
					state.visuals.push_note_on(
						ev.key, batch_time_us, ev.tick, ev.track_index, ev.channel, ev.velocity);
				else if (ev.k == generated_event::kind::note_off)
					state.visuals.push_note_off(
						ev.key, batch_time_us, ev.tick, ev.track_index, ev.channel);
			}

			if (ev.short_msg != 0)
			{
				state.send_buffer.push({batch_time_us, ev.short_msg});

				// Same intra-batch size cap as the file parser (sampled every 1024).
				if ((++cap_check_counter & 1023u) == 0)
				{
					while (!fast_forwarding && !state.stop_requested &&
						state.send_buffer.approximate_size() >= playback_state::max_pending_events)
					{
						if (state.paused.load(std::memory_order_acquire))
							std::this_thread::sleep_for(std::chrono::microseconds(100));
						else
							std::this_thread::sleep_for(std::chrono::microseconds(10));
					}
				}
			}
		}

		state.visuals.flush_lifecycle();
		state.visuals.reclaim_retired();
		state.parser_done.store(true, std::memory_order_release);
	}

	// Sender thread: sends pre-parsed events with precise timing
	// During seek fast-forward (seeking_ff == true), drains buffer immediately
	SIMPLE_PLAYER_FORCE_NO_INLINE void sender_thread_func()
	{
		// Cached clock reading: dense same-tick bursts send thousands of
		// already-due events back to back; for those the clock isn't
		// re-queried and the position isn't re-published.
		constexpr int64_t clock_unknown = std::numeric_limits<int64_t>::min();
		int64_t cached_elapsed_us = clock_unknown;
		uint64_t last_published_us = ~0ULL;

		while (!state.stop_requested)
		{
			// Fast-forward mode: drain buffer immediately, no timing
			if (state.seeking_ff.load(std::memory_order_acquire))
			{
				// Parser resets start_time/start_offset_us when FF completes,
				// so any cached clock reading is stale.
				cached_elapsed_us = clock_unknown;
				last_published_us = ~0ULL;

				if (!state.send_buffer.empty())
				{
					auto& ev = state.send_buffer.front();
					if (short_msg && ev.short_msg != 0)
						short_msg(ev.short_msg);
					state.send_buffer.pop();
					continue;
				}

				// Buffer empty during FF - parser is still filling it
				if (state.parser_done.load(std::memory_order_acquire))
					break;
				std::this_thread::yield();
				continue;
			}

			// handle pause - wait until unpaused
			if (state.paused.load(std::memory_order_acquire))
			{
				// Wait until resumed or stopped
				while (state.paused.load(std::memory_order_acquire) && !state.stop_requested)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

				if (state.stop_requested)
					break;

				// Resume: resume() updated start_offset_us and reset start_time,
				// so the cached clock reading is stale.
				cached_elapsed_us = clock_unknown;
			}

			if (state.stop_requested)
				break;

			if (state.send_buffer.empty())
			{
				// buffer empty - check if parser is done
				if (state.parser_done.load(std::memory_order_acquire))
					break; // all done

				// wait for parser to produce more events
				std::this_thread::yield();
				continue;
			}

			auto& ev = state.send_buffer.front();

			// update current position for UI and parser throttling
			// (only when it changes - events in a burst share one timestamp)
			if (ev.time_us != last_published_us)
			{
				last_published_us = ev.time_us;
				state.current_time_us = ev.time_us;
				state.sender_position_us.store(ev.time_us, std::memory_order_release);
			}

			uint64_t target_us = ev.time_us - state.start_offset_us;

			// wait until it's time to send this event; if the cached clock
			// already passed the target, send immediately without re-querying
			if (static_cast<int64_t>(target_us) > cached_elapsed_us)
			{
				auto elapsed = std::chrono::steady_clock::now() - state.start_time;
				cached_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

				if (static_cast<int64_t>(target_us) > cached_elapsed_us)
				{
					auto wait_us = target_us - cached_elapsed_us;
					if (wait_us > 200)
					{
						// sleep for most of the wait time
						std::this_thread::sleep_for(std::chrono::microseconds(wait_us - 20));
					}

					// spin-wait for final precision
					while (!state.stop_requested)
					{
						elapsed = std::chrono::steady_clock::now() - state.start_time;
						cached_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
						if (cached_elapsed_us >= static_cast<int64_t>(target_us))
							break;
					}
				}
			}

			// send the event
			if (short_msg && ev.short_msg != 0) [[likely]]
				short_msg(ev.short_msg);

			state.send_buffer.pop();
		}
	}

	void playback_thread(uint64_t initial_skip_to_us = 0, bool start_paused = true)
	{
		try
		{
			state.reset();
			state.start_time = std::chrono::steady_clock::now();

			// Publish fast-forward BEFORE playing, so a waiter that observes
			// is_playing() never sees the brief pre-fast-forward window as "done".
			state.seeking_ff.store(initial_skip_to_us > 0, std::memory_order_release);
			state.playing.store(true, std::memory_order_release);

			// The normal player starts paused so a synth can be selected. Editor
			// playback prepares its output first and requests immediate playback.
			state.paused.store(start_paused, std::memory_order_release);
			state.pause_position_us.store(0, std::memory_order_release);

			uint64_t skip_to_us = initial_skip_to_us;
			bool pause_after_seek = initial_skip_to_us > 0 && start_paused;

			for (;;)
			{
				// reset per-iteration state
				state.stop_requested.store(false, std::memory_order_relaxed);
				// A seek (skip_to_us > 0) fast-forwards; keep seeking_ff set from
				// here through completion so the pre-fast-forward window stays closed.
				state.seeking_ff.store(skip_to_us > 0, std::memory_order_relaxed);
				state.parser_done.store(false, std::memory_order_relaxed);
				state.parsed_up_to_us.store(0, std::memory_order_relaxed);
				state.sender_position_us.store(0, std::memory_order_relaxed);
				state.memory_failure.store(false, std::memory_order_relaxed);
				state.send_buffer.clear();
				state.visuals.reset();
				state.track_states.clear();

				// Launch parser and sender threads
				std::thread parser_thread;
				std::thread sender_thread;

				try
				{
					parser_thread = std::thread([this, skip_to_us, pause_after_seek]() {
						try
						{
							if (external_source)
								parser_from_source_thread_func(external_source, skip_to_us, pause_after_seek);
							else
								parser_thread_func(skip_to_us, pause_after_seek);
						}
						catch (const std::bad_alloc&)
						{
							handle_memory_failure();
						}
					});
					sender_thread = std::thread([this]() { sender_thread_func(); });
				}
				catch (...)
				{
					state.stop_requested.store(true, std::memory_order_release);
					state.parser_done.store(true, std::memory_order_release);

					if (parser_thread.joinable())
						parser_thread.join();
					if (sender_thread.joinable())
						sender_thread.join();

					throw;
				}

				parser_thread.join();
				sender_thread.join();

				if (state.memory_failure.load(std::memory_order_acquire))
				{
					report_memory_failure_once();
					break;
				}

				// Check if this was a seek (threads stopped due to seek_requested)
				if (state.seek_requested.load(std::memory_order_acquire))
				{
					skip_to_us = state.seek_target_us.load(std::memory_order_relaxed);
					pause_after_seek = state.seek_resume_paused.load(std::memory_order_relaxed);

					state.seek_requested.store(false, std::memory_order_relaxed);
					if (pause_after_seek)
						state.pause_position_us.store(skip_to_us, std::memory_order_release);

					state.paused.store(pause_after_seek, std::memory_order_relaxed);

					all_notes_off();
					continue;
				}

				// Normal exit
				break;
			}
		}
		catch (const std::bad_alloc&)
		{
			handle_memory_failure(true);
		}

		all_notes_off();
		state.playing.store(false, std::memory_order_release);
	}

	struct draw_data
	{
		struct point { float x, y; };
		struct quad_geometry { point tl, tr, br, bl; };
		struct color { uint8_t r, g, b; };

		quad_geometry keyboard[128];
		uint8_t key_n[128];
		uint64_t scroll_window_us = 1 << 18;
		std::vector<quad_geometry> quads;
		std::vector<color> colors;
		//std::unordered_map<uint32_t, uint32_t> track_colors;

		// Scratch buffers for batched note rendering (reused each frame)
		struct note_vert  { float x, y; };
		struct note_color { uint8_t r, g, b, a; };

		struct note_span
		{
			uint64_t start_tick;
			uint64_t end_tick;
			float begin_y;
			float end_y;
			uint32_t track_n;
			uint32_t color;
			bool touches_keyboard;
		};

		std::vector<note_vert>  note_verts;
		std::vector<note_color> note_colors;
		std::vector<note_vert>  outline_verts;
		std::vector<note_color> outline_colors;
		std::vector<note_span>  key_note_spans;
		std::vector<note_span>  overlap_reorder_scratch;

		void clear_note_scratch() noexcept
		{
			note_verts.clear();
			note_colors.clear();
			outline_verts.clear();
			outline_colors.clear();
			key_note_spans.clear();
			overlap_reorder_scratch.clear();
		}

		void release_note_scratch() noexcept
		{
			std::vector<note_vert>().swap(note_verts);
			std::vector<note_color>().swap(note_colors);
			std::vector<note_vert>().swap(outline_verts);
			std::vector<note_color>().swap(outline_colors);
			std::vector<note_span>().swap(key_note_spans);
			std::vector<note_span>().swap(overlap_reorder_scratch);
		}

		// Six faces per key, three shadow quads per black key, and three case/rail
		// quads: 1,860 triangles for all 128 keys, independent of the note count.
		// Geometry changes only on init/resize/move; colors only on key changes.
		static constexpr size_t keyboard_vertex_capacity = (128 * 6 + 53 * 3 + 3) * 4;
		struct keyboard_vertex { float x, y; uint8_t r, g, b, a; };
		struct keyboard_tone { uint8_t gain, bias; };
		std::array<keyboard_vertex, keyboard_vertex_capacity> keyboard_vertices;
		std::array<keyboard_tone, keyboard_vertex_capacity> keyboard_tones;
		std::array<size_t, 128> keyboard_face_offsets;
		std::array<color, 128> last_keyboard_colors;
		size_t keyboard_vertex_count = 0;
		bool keyboard_colors_valid = false;

		void init_keyboard_mesh()
		{
			keyboard_vertex_count = 0;
			keyboard_colors_valid = false;
			const float left = keyboard[0].tl.x;
			const float right = keyboard[white_keys_count() - 1].tr.x;
			const float bottom = keyboard[0].bl.y;
			const float top = keyboard[0].tl.y;
			const float white_width = keyboard[0].tr.x - left;
			const float white_height = top - bottom;

			auto quad = [&](point tl, point tr, point br, point bl,
				keyboard_tone ctl, keyboard_tone ctr, keyboard_tone cbr, keyboard_tone cbl)
			{
				const point points[] = {tl, tr, br, bl};
				const keyboard_tone tones[] = {ctl, ctr, cbr, cbl};
				for (size_t v = 0; v < 4; ++v)
				{
					keyboard_vertices[keyboard_vertex_count] = {points[v].x, points[v].y, 0, 0, 0, 255};
					keyboard_tones[keyboard_vertex_count++] = tones[v];
				}
			};
			auto fixed_quad = [&](float l, float r, float b, float t, color c,
				uint8_t atl, uint8_t atr, uint8_t abr, uint8_t abl)
			{
				const uint8_t alpha[] = {atl, atr, abr, abl};
				quad({l, t}, {r, t}, {r, b}, {l, b}, {}, {}, {}, {});
				for (size_t v = 0; v < 4; ++v)
				{
					auto& vertex = keyboard_vertices[keyboard_vertex_count - 4 + v];
					vertex.r = c.r; vertex.g = c.g; vertex.b = c.b; vertex.a = alpha[v];
				}
			};

			// The dark key bed shows through the narrow white-key seams.
			fixed_quad(left, right, bottom, top, {24, 26, 30}, 255, 255, 255, 255);
			for (int i = 0; i < white_keys_count(); ++i)
			{
				const auto& key = keyboard[i];
				const float l = key.tl.x + white_width * 0.025f;
				const float r = key.tr.x - white_width * 0.025f;
				const float edge = white_width * 0.065f;
				const float lip = std::min(white_width * 0.24f, white_height * 0.1f);
				const float bevel = std::min(white_width * 0.09f, white_height * 0.04f);
				const float face = bottom + lip;
				const float surface = face + bevel;
				keyboard_face_offsets[i] = keyboard_vertex_count;
				// Long ivory surface, side bevels, rounded nose, and recessed front.
				quad({l + edge, top}, {r - edge, top}, {r - edge, surface}, {l + edge, surface},
					{204, 0}, {218, 0}, {255, 0}, {247, 0});
				quad({l, top}, {l + edge, top}, {l + edge, surface}, {l, face},
					{183, 0}, {204, 0}, {247, 0}, {226, 0});
				quad({r - edge, top}, {r, top}, {r, face}, {r - edge, surface},
					{218, 0}, {155, 0}, {199, 0}, {255, 0});
				quad({l + edge, surface}, {r - edge, surface}, {r, face}, {l, face},
					{247, 0}, {255, 0}, {219, 0}, {230, 0});
				quad({l, face}, {r, face}, {r, bottom}, {l, bottom},
					{190, 0}, {180, 0}, {132, 0}, {147, 0});
				// Short falloff under the rear rail; no texture or lighting pass.
				const float rear = top - std::min(white_width * 0.45f, white_height * 0.12f);
				quad({l + edge, top}, {r - edge, top}, {r - edge, rear}, {l + edge, rear},
					{144, 0}, {153, 0}, {222, 0}, {209, 0});
			}

			// Draw every shadow before the black keys. Alpha keeps active white-key
			// colors visible underneath, and all shadows stay inside the keyboard.
			for (int i = white_keys_count(); i < 128; ++i)
			{
				const auto& key = keyboard[i];
				const float w = key.tr.x - key.tl.x;
				const float spread = w * 0.19f;
				const float b = std::max(bottom, key.bl.y - spread);
				fixed_quad(std::max(left, key.tl.x - spread), key.tl.x, key.bl.y, top,
					{0, 0, 0}, 0, 48, 48, 0);
				fixed_quad(key.tr.x, std::min(right, key.tr.x + spread), key.bl.y, top,
					{0, 0, 0}, 80, 0, 0, 80);
				fixed_quad(key.tl.x, key.tr.x, b, key.bl.y, {0, 0, 0}, 80, 80, 0, 0);
			}
			for (int i = white_keys_count(); i < 128; ++i)
			{
				const auto& key = keyboard[i];
				const float l = key.tl.x, r = key.tr.x, b = key.bl.y;
				const float w = r - l, h = top - b;
				const float inset = w * 0.14f;
				const float face = b + std::min(w * 0.42f, h * 0.15f);
				const float surface = face + std::min(w * 0.14f, h * 0.05f);
				keyboard_face_offsets[i] = keyboard_vertex_count;
				// Add a small neutral reflection to ebony, also when a key is lit.
				quad({l + inset, top}, {r - inset, top}, {r - inset, surface}, {l + inset, surface},
					{180, 46}, {168, 35}, {190, 12}, {205, 23});
				quad({l, top}, {l + inset, top}, {l + inset, surface}, {l, face},
					{120, 68}, {180, 46}, {205, 23}, {130, 50});
				quad({r - inset, top}, {r, top}, {r, face}, {r - inset, surface},
					{168, 35}, {76, 9}, {85, 5}, {190, 12});
				quad({l + inset, surface}, {r - inset, surface}, {r, face}, {l, face},
					{205, 23}, {190, 12}, {145, 36}, {175, 61});
				quad({l, face}, {r, face}, {r, b}, {l, b},
					{115, 22}, {95, 12}, {65, 4}, {80, 9});
				const float rear = top - std::min(w * 0.09f, h * 0.03f);
				quad({l + inset, top}, {r - inset, top}, {r - inset, rear}, {l + inset, rear},
					{150, 63}, {140, 49}, {168, 35}, {180, 46});
			}
			const float rail = std::min(white_width * 0.18f, white_height * 0.04f);
			fixed_quad(left, right, top - rail, top, {19, 16, 20}, 255, 255, 255, 255);
			fixed_quad(left, right, top - rail * 0.45f, top, {154, 36, 42}, 255, 255, 255, 255);
		}

		void draw_keyboard(const color (&key_colors)[128])
		{
			for (size_t i = 0; i < 128; ++i)
			{
				const auto c = key_colors[i];
				const auto& previous = last_keyboard_colors[i];
				if (keyboard_colors_valid && c.r == previous.r && c.g == previous.g && c.b == previous.b)
					continue;
				for (size_t v = keyboard_face_offsets[i]; v < keyboard_face_offsets[i] + 6 * 4; ++v)
				{
					auto& vertex = keyboard_vertices[v];
					const auto tone = keyboard_tones[v];
					auto shade = [tone](uint8_t channel) {
						return uint8_t(std::min(255, (channel * tone.gain + 127) / 255 + tone.bias));
					};
					vertex.r = shade(c.r); vertex.g = shade(c.g); vertex.b = shade(c.b);
				}
				last_keyboard_colors[i] = c;
			}
			keyboard_colors_valid = true;
			glVertexPointer(2, GL_FLOAT, sizeof(keyboard_vertex), &keyboard_vertices[0].x);
			glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(keyboard_vertex), &keyboard_vertices[0].r);
			glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(keyboard_vertex_count));
		}

		bool enable_simulated_lag = true;
		uint8_t remove_overlaps = 0;

		static constexpr uint8_t MAX_OVERLAPS_REMOVAL_VERSION = 0;
		static constexpr float DEFAULT_WIDTH = 400, DEFAULT_HEIGHT = 250;
		float width = DEFAULT_WIDTH, height = DEFAULT_HEIGHT;
		float last_keyboard_height = 0;
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
			const float white_width = width / total;
			constexpr float sharp_ratio = static_cast<float>(total) / 128.f;
			const float black_half_width = white_width * sharp_ratio * 0.5f + black_margins;

			quad_geometry* first = keyboard, *last = keyboard + 127;
			uint32_t white_keys_seen = 0;

			for (uint8_t key = 0; key < 128; key++)
			{
				if (is_white_key(key))
				{
					uint32_t index = first - keyboard;

					first->bl.x = first->tl.x = index * white_width;
					first->br.x = first->tr.x = (index + 1) * white_width;

					first->bl.y = first->br.y = -0.5 * height - keyboard_height;
					first->tl.y = first->tr.y = -0.5 * height;

					key_n[index] = key;

					++first;
					++white_keys_seen;
				}
				else
				{
					const uint8_t note = key % 12;
					float center_offset = 0.f;
					if (note == 1 || note == 6)       // C sharp, F sharp
						center_offset = -sharp_ratio / 5.f;
					else if (note == 3 || note == 10) // D sharp, A sharp
						center_offset = sharp_ratio / 5.f;

					const float center_x = (white_keys_seen + center_offset) * white_width;
					last->bl.x = last->tl.x = center_x - black_half_width;
					last->br.x = last->tr.x = center_x + black_half_width;

					last->bl.y = last->br.y = -0.5 * height - black_hight;
					last->tl.y = last->tr.y = -0.5 * height;

					key_n[last - keyboard] = key;

					--last;
				}
			}

			last_keyboard_height = keyboard_height;
			init_keyboard_mesh();
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
			for (size_t v = 0; v < keyboard_vertex_count; ++v)
			{
				keyboard_vertices[v].x += dx;
				keyboard_vertices[v].y += dy;
			}
		}

		void reinit(float new_width, float new_height, float keyboard_height, float black_height, float black_margins)
		{
			width = new_width;
			height = new_height;
			init(keyboard_height, black_height, black_margins);
		}
	};

	template <float SCALE>
	static uint8_t scale(uint8_t value)
	{
		// 16-bit fractional part is more than enough for uint8_t → uint8_t scaling
		constexpr uint32_t MAGIC = static_cast<uint32_t>(SCALE * (1u << 16) + 0.5f);

		uint32_t temp = static_cast<uint32_t>(value) * MAGIC;
		return static_cast<uint8_t>(temp >> 16);
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void remove_redundant_overlaps(draw_data& data)
	{
		dixelu::remove_redundant_midi_overlaps(
			data.key_note_spans, data.overlap_reorder_scratch);
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void draw(draw_data& data)
	{
		constexpr int total_white = draw_data::white_keys_count();

		auto& visuals = get_visuals();

		// Compute current playback position from wall-clock for smooth visualization
		uint64_t current_us = get_position_us();

		if (data.enable_simulated_lag)
		{
			uint64_t max = state.parsed_up_to_us.load(std::memory_order_relaxed);

			if (max < current_us + data.scroll_window_us)
				current_us = max > data.scroll_window_us ?
					max - data.scroll_window_us : 0;
		}

		// draw falling notes
		draw_data::color keyboard_colors[128];
		memset(keyboard_colors, 0xFF, sizeof(draw_data::color) * total_white);
		memset(keyboard_colors + total_white, 0x00, sizeof(draw_data::color) * (128 - total_white));

		GLsizei white_fill_verts = 0;
		GLsizei white_outline_verts = 0;
		data.clear_note_scratch();

		try
		{
			// lock against visuals.reset() which may be called by playback_thread on seek/restart.
			std::lock_guard<std::mutex> visuals_lock(visuals.access_mutex);

			const int64_t expiry_cutoff = current_us > data.scroll_window_us ?
				static_cast<int64_t>(current_us - data.scroll_window_us) : -1;
			static_cast<void>(visuals.update_visible(expiry_cutoff));

			auto batch_notes_for_index = [&](int index)
			{
				uint8_t key = data.key_n[index];
				data.key_note_spans.clear();

				for (const auto* note_ptr = visuals.first_visible_note(key);
					note_ptr; note_ptr = note_ptr->visible_next)
				{
					const auto& note = *note_ptr;

					// The acquire load also publishes end_tick, which finish() stores
					// immediately before the completed time.
					uint64_t end_time = note.end_time_us.load(std::memory_order_acquire);
					uint64_t end_tick = end_time == ~0ULL ? ~0ULL :
						note.end_tick.load(std::memory_order_relaxed);

					const int64_t start_offset = static_cast<int64_t>(note.start_time_us) -
						static_cast<int64_t>(current_us);

					float begin_y = float(start_offset) / float(data.scroll_window_us);
					float end_y = 1;

					if (end_time != ~0ULL)
					{
						const int64_t end_offset = static_cast<int64_t>(end_time) -
							static_cast<int64_t>(current_us);
						end_y = float(end_offset) / float(data.scroll_window_us);
					}

					if (begin_y > 1.01f)
						break; // Visible list is ordered by note-on time.

					if (end_y < -0.01f)
						continue;

					data.key_note_spans.push_back({
						note.start_tick,
						end_tick,
						std::clamp(begin_y, 0.f, 1.f),
						std::clamp(end_y, 0.f, 1.f),
						note.track_id,
						rotate(0xFF7F008F, note.track_id),
						begin_y <= 0 && end_y >= 0
					});
				}

				if (data.key_note_spans.empty())
					return;

				const draw_data::note_span* keyboard_top = nullptr;
				for (const auto& span : data.key_note_spans)
				{
					if (!span.touches_keyboard)
						continue;
					if (!keyboard_top || span.start_tick > keyboard_top->start_tick ||
						(span.start_tick == keyboard_top->start_tick &&
							span.track_n > keyboard_top->track_n))
						keyboard_top = &span;
				}
				if (keyboard_top)
				{
					keyboard_colors[index] = {
						uint8_t(keyboard_top->color >> 24),
						uint8_t(keyboard_top->color >> 16),
						uint8_t(keyboard_top->color >> 8),
					};
				}

				if (data.remove_overlaps <= draw_data::MAX_OVERLAPS_REMOVAL_VERSION)
					remove_redundant_overlaps(data);

				const float lx = data.keyboard[index].tl.x;
				const float rx = data.keyboard[index].tr.x;

				auto emit_span = [&](float begin_y, float end_y, uint32_t color_value)
				{
					begin_y = data.keyboard->tr.y + data.height * begin_y;
					end_y = data.keyboard->tr.y + data.height * end_y;

					const draw_data::note_color note_color{
						uint8_t(color_value >> 24),
						uint8_t(color_value >> 16),
						uint8_t(color_value >> 8),
						0xFF
					};

					const draw_data::note_color note_color_shady{
						scale<0.5f>(note_color.r),
						scale<0.5f>(note_color.g),
						scale<0.5f>(note_color.b),
						0xFF
					};

					const draw_data::note_color note_border_col{
						scale<0.25f>(note_color.r),
						scale<0.25f>(note_color.g),
						scale<0.25f>(note_color.b),
						0xFF
					};

					data.note_verts.reserve(data.note_verts.size() + 4);
					data.note_verts.push_back({lx, begin_y});
					data.note_verts.push_back({lx, end_y});
					data.note_verts.push_back({rx, end_y});
					data.note_verts.push_back({rx, begin_y});

					data.note_colors.reserve(data.note_colors.size() + 4);
					data.note_colors.push_back(note_color);
					data.note_colors.push_back(note_color);
					data.note_colors.push_back(note_color_shady);
					data.note_colors.push_back(note_color_shady);

					data.outline_verts.reserve(data.outline_verts.size() + 8);
					data.outline_verts.push_back({lx, begin_y}); data.outline_verts.push_back({lx, end_y});
					data.outline_verts.push_back({lx, end_y});   data.outline_verts.push_back({rx, end_y});
					data.outline_verts.push_back({rx, end_y});   data.outline_verts.push_back({rx, begin_y});
					data.outline_verts.push_back({rx, begin_y}); data.outline_verts.push_back({lx, begin_y});

					for (int e = 0; e < 8; ++e)
						data.outline_colors.push_back(note_border_col);
				};

				for (const auto& span : data.key_note_spans)
					emit_span(span.begin_y, span.end_y, span.color);
			};

			// White key notes first (drawn behind), record split point, then black key notes on top
			for (int index = 0; index < total_white; ++index)
				batch_notes_for_index(index);

			white_fill_verts    = static_cast<GLsizei>(data.note_verts.size());
			white_outline_verts = static_cast<GLsizei>(data.outline_verts.size());
			for (int index = total_white; index < 128; ++index)
				batch_notes_for_index(index);
		}
		catch (const std::bad_alloc&)
		{
			data.release_note_scratch();
			handle_memory_failure(true);
			return;
		}

		// Draw order: white fills, white outlines, black fills, black outlines.
		// Matches original per-note ordering so black notes fully overdraw white note outlines.
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		if (!data.note_verts.empty())
		{
			const GLsizei total_fill    = static_cast<GLsizei>(data.note_verts.size());
			const GLsizei total_outline = static_cast<GLsizei>(data.outline_verts.size());

			glVertexPointer(2, GL_FLOAT, 0, data.note_verts.data());
			glColorPointer(4, GL_UNSIGNED_BYTE, 0, data.note_colors.data());
			glDrawArrays(GL_QUADS, 0, white_fill_verts);                           // white fills

			glVertexPointer(2, GL_FLOAT, 0, data.outline_verts.data());
			glColorPointer(4, GL_UNSIGNED_BYTE, 0, data.outline_colors.data());
			glDrawArrays(GL_LINES, 0, white_outline_verts);                        // white outlines

			glVertexPointer(2, GL_FLOAT, 0, data.note_verts.data());
			glColorPointer(4, GL_UNSIGNED_BYTE, 0, data.note_colors.data());
			glDrawArrays(GL_QUADS, white_fill_verts, total_fill - white_fill_verts);    // black fills

			glVertexPointer(2, GL_FLOAT, 0, data.outline_verts.data());
			glColorPointer(4, GL_UNSIGNED_BYTE, 0, data.outline_colors.data());
			glDrawArrays(GL_LINES, white_outline_verts, total_outline - white_outline_verts); // black outlines

			data.clear_note_scratch();
		}

		data.draw_keyboard(keyboard_colors);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}

private:
	// A simple_player owns one state object, parser buffer and output sink. The
	// editor and SIMPLAYER use different workers, so is_playing() alone cannot
	// close the scheduling window before a worker starts. This mutex makes a
	// second run fail closed instead of corrupting the shared state.
	std::mutex playback_run_mutex;

	void handle_memory_failure(bool report_now = false)
	{
		state.memory_failure.store(true, std::memory_order_release);
		state.stop_requested.store(true, std::memory_order_release);
		state.seek_requested.store(false, std::memory_order_release);
		state.seeking_ff.store(false, std::memory_order_release);
		state.parser_done.store(true, std::memory_order_release);
		state.paused.store(false, std::memory_order_release);
		info.open_complete = true;

		if (report_now)
			report_memory_failure_once();
	}

	void report_memory_failure_once()
	{
		if (memory_failure_reported.exchange(true, std::memory_order_acq_rel))
			return;

		throw_alert_error("Not enough memory to play this MIDI. Playback was stopped.");
	}

	static uint32_t rotate(uint32_t color, uint32_t shift)
	{
		constexpr int base = 31;
		auto rem = shift % base;

		return color << (base - rem) | color >> rem;
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void try_init_kdmapi()
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

	SIMPLE_PLAYER_FORCE_NO_INLINE void update_tempo_cache_at(size_t index) const
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

	SIMPLE_PLAYER_FORCE_NO_INLINE uint64_t tick_to_microseconds(tick_type tick) const
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
				throw_alert_error("Byte " + std::to_string(cur - reinterpret_cast<const std::uint8_t*>(mmap->data())) + ": Unexpected 0 RSB\nAt least one track was skipped!");
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
					throw_alert_warning("Byte " + (std::to_string(cur - reinterpret_cast<const std::uint8_t*>(mmap->data())) + ": Unknown event type " + std::to_string(command)));
					is_good = false;
					break;
				}
			}
		}

		if (!is_good || is_reading)
			return false;

		if (track_expected_size != cur - raw_track_data_begin)
			(*warnings) << log_event{log_event_type::track_size_mismatch, (uint64_t)track_expected_size, (uint64_t)(cur - raw_track_data_begin)};

		info.ticks_length = std::max(current_tick, info.ticks_length);

		track_info track;
		track.begining = raw_track_data_begin;
		track.ending = cur;

		info.tracks.push_back(std::move(track));

		return true;
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE static uint8_t get_value_and_increment(const uint8_t*& cur, const uint8_t* end)
	{
		if (cur < end)
			return *(cur++);
		return 0;
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE static uint64_t get_vlv(const uint8_t*& cur, const uint8_t* end)
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

		kdmapi_status = nullptr;
		set_short_msg_noop();

		auto hout_copy = hout.load();
		hout = nullptr;

		midiOutReset(hout_copy);
		while (midiOutClose(hout_copy) != MMSYSERR_NOERROR && attempts++ < 8)
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

		if (attempts == 8)
			throw_alert_error("Unable to close the MIDI out");
	}

	void set_short_msg_noop()
	{
		short_msg = [](uint32_t msg) {};
	}

	void init_midi_out(size_t device)
	{
		static std::set<std::wstring> kdmapi_allowed
			{ L"OmniMIDI", L"K[q093jfpowe" };

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
				std::string readable_name(name.size(), '\0');
				std::transform(name.begin(), name.end(), readable_name.begin(), [](wchar_t c) { return static_cast<char>(c); });
				throw_alert_error("Unable to open MIDI out '" + readable_name + "'!");
				hout = nullptr;
			}
			else
			{
				hout = hout_copy;
				short_msg = [](uint32_t msg) { midiOutShortMsg(hout, msg); };
			}

			if (!kdmapi_status && kdmapi_allowed.contains(devices[device].szPname))
				try_init_kdmapi();
		}
		catch (...)
		{
			throw_alert_error("midiOutOpen() horribly failed...\n");
		}
	}

	void all_notes_off_channel(uint8_t channel)
	{
		if (!short_msg) [[unlikely]]
			return;

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

	std::wstring current_filename;
	std::unique_ptr<dixelu::memory_mapped_file_reader> mmap;

	midi_info info;
	playback_state state;
	mutable tempo_cache tcache;

	// Non-null only during run_from_external: playback_thread routes the parser
	// through this source instead of the mmap'd file. Owned by the caller.
	playback_event_source* external_source = nullptr;

	size_t current_device = ~0ULL;
	std::vector<MIDIOUTCAPSW> devices;
	inline static std::atomic<HMIDIOUT> hout;
	std::atomic<bool> memory_failure_reported{false};
	std::atomic<bool> cancel_requested{false};

	void(WINAPI* short_msg)(uint32_t msg) = nullptr;
	bool(WINAPI* kdmapi_status)() = nullptr;

	constexpr static std::uint32_t MTrk_header = 1297379947;
	constexpr static std::uint32_t MThd_header = 1297377380;
};

#endif
