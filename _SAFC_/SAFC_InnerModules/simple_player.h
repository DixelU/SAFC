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
#include <set>
#include <list>
#include <memory>
#include <unordered_map>

#include <memory_mapped_file_reader.h>

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"
#include "playback_event_source.h"
#include "syncore_output.h"

#define SIMPLE_PLAYER_FORCE_NO_INLINE
#include <buffered_block_list.h>
#include <buffered_queue_spsc.h>
// __declspec(noinline)
// __declspec(noinline)

// lock-free SPSC slab-based queue
// Producer (parser): push, back
// Consumer (renderer): pop, front, empty, iteration
struct simple_player
{
	using tick_type = std::uint64_t;
	static constexpr std::uint64_t default_start_lead_in_us = 3'000'000ULL;

	struct track_info
	{
		const uint8_t* begining;
		const uint8_t* ending;
	};

	// Buffered note for notes display. All fields are owned by the visual
	// consumer after queued events cross the parser/render boundary.
	struct buffered_note
	{
		uint64_t start_time_us;            // note on time (immutable after creation)
		uint64_t end_time_us;              // note off time (~0 = still held/pending)
		uint32_t track_id;                 // (track_index << 4) | channel

		buffered_note() : start_time_us(0), end_time_us(~0ULL), track_id(0) {}

		buffered_note(uint64_t start, uint64_t end, uint32_t track)
			: start_time_us(start), end_time_us(end), track_id(track)
		{
		}
	};

	// Visuals viewport: manages notes display and keyboard state
	// lock-free SPSC: parser thread pushes notes, render thread reads state
	struct visuals_viewport
	{
		static constexpr size_t key_count = 128;
		static constexpr size_t note_list_slab_bytes = 1u << 8;
		using note_list = dixelu::buffered_block_list<buffered_note, note_list_slab_bytes>;

		struct queued_visual_event
		{
			uint64_t time_us;
			uint32_t track_id;
			uint8_t key;
			bool note_on;
		};

		// Pending note tracking entry - points to a list note awaiting its note_off
		// Only accessed by the visual consumer - no synchronization needed
		// Per-track_id LIFO stacks of pending notes per key.
		// Flat open-addressing hash table (linear probing, power-of-two size):
		// a lookup is one probe into a contiguous array instead of
		// unordered_map's bucket->node pointer chase, and emptied stacks keep
		// their slot and capacity, so steady-state playback doesn't allocate.
		// Visual-consumer-private
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
				slots.clear();
				used = 0;
			}
		};

		// The parser publishes compact events through one SPSC queue. The visual
		// consumer drains them into per-key block lists, so list mutation, pending
		// matching, reclamation and iteration all stay on one thread. Lists are
		// allocated lazily to avoid the pool's default per-container slab cost.
		dixelu::buffered_queue_spsc<queued_visual_event> queued_events;
		std::array<std::unique_ptr<note_list>, key_count> falling_notes;

		// Per-key pending note trackers for note_off matching
		// Visual-consumer-private
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

		void apply_visual_event(const queued_visual_event& event)
		{
			if (event.note_on)
			{
				auto& notes = falling_notes[event.key];
				if (!notes)
					notes = std::make_unique<note_list>();

				buffered_note& note = notes->emplace_back(
					event.time_us, ~0ULL, event.track_id);
				try
				{
					pending[event.key].push(event.track_id, &note);
				}
				catch (...)
				{
					notes->pop_back();
					if (notes->empty())
						notes.reset();
					throw;
				}
				return;
			}

			buffered_note* note = pending[event.key].find_and_remove(event.track_id);
			if (note)
				note->end_time_us = event.time_us;
		}

		// Drain only the boundary visible at entry. A continuously producing parser
		// therefore cannot monopolize one render frame.
		SIMPLE_PLAYER_FORCE_NO_INLINE void drain_queued_events()
		{
			size_t remaining = queued_events.approximate_size();
			while (remaining-- != 0 && !queued_events.empty())
			{
				const queued_visual_event event = queued_events.front();
				queued_events.pop();
				apply_visual_event(event);
			}
		}

		// Push a note_on event to the visual consumer.
		// Called from parser thread only - no locking needed.
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_on(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel, uint8_t velocity)
		{
			if (key >= key_count)
				return;

			(void)velocity;
			queued_events.push({time_us, make_color_id(track_index, channel), key, true});
		}

		// Push a note_off event to the visual consumer.
		// Called from parser thread only - no locking needed.
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_off(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel)
		{
			if (key >= key_count)
				return;

			queued_events.push({time_us, make_color_id(track_index, channel), key, false});
		}

		// Remove every note that has fully scrolled past, including expired notes
		// behind an older note that is still held. Lists are ordered by start time,
		// so no later note can be expired once start_time reaches the cutoff.
		// Called from the visual consumer only.
		SIMPLE_PLAYER_FORCE_NO_INLINE void cull_expired(int64_t cutoff_time_us)
		{
			drain_queued_events();
			if (cutoff_time_us <= 0)
				return;

			const uint64_t cutoff = static_cast<uint64_t>(cutoff_time_us);
			for (size_t key = 0; key < key_count; ++key)
			{
				auto& notes = falling_notes[key];
				if (!notes)
					continue;

				for (auto current = notes->begin(); current != notes->end();)
				{
					if (current->start_time_us >= cutoff)
						break;

					const uint64_t end_time = current->end_time_us;
					if (end_time != ~0ULL && end_time < cutoff)
						current = notes->erase(current);
					else
						++current;
				}

				if (notes->empty())
					notes.reset();
			}
		}

		// Clear all visual state
		// Should only be called when no concurrent access (e.g., during reset)
		void clear()
		{
			queued_events.clear();
			for (auto& p : pending)
				p.clear();
			for (auto& notes : falling_notes)
				notes.reset();
		}

		// reset for new playback
		// Acquires access_mutex to synchronize with render thread iteration.
		void reset()
		{
			std::lock_guard<std::mutex> lock(access_mutex);
			clear();
		}

		std::size_t buffered_note_count()
		{
			std::lock_guard<std::mutex> lock(access_mutex);
			drain_queued_events();
			std::size_t result = 0;
			for (const auto& notes : falling_notes)
				if (notes)
					result += notes->size();
			return result;
		}
	};

	struct send_event
	{
		uint64_t time_us;    // target send time in microseconds from start
		uint32_t short_msg;  // prepared MIDI short message (0 = invalid/empty)
	};

	struct seek_held_note
	{
		uint64_t start_time_us;
		size_t track_index;
		uint8_t key;
		uint8_t channel;
		uint8_t velocity;
	};

	// While the parser walks to a seek target, retain only notes that are still
	// held there. The list preserves note-on order; per-identity stacks provide
	// the same LIFO note-off matching used by the live visual path.
	struct seek_note_tracker
	{
		using note_list = std::list<seek_held_note>;
		using note_iterator = note_list::iterator;

		note_list active;
		std::unordered_map<uint64_t, std::vector<note_iterator>> pending;

		static uint64_t identity(uint8_t key, size_t track_index, uint8_t channel)
		{
			const uint32_t color_id = visuals_viewport::make_color_id(track_index, channel);
			return (static_cast<uint64_t>(color_id) << 7) | key;
		}

		void note_on(uint8_t key, uint64_t time_us, size_t track_index,
			uint8_t channel, uint8_t velocity)
		{
			active.push_back({time_us, track_index, key, channel, velocity});
			auto note = active.end();
			--note;
			pending[identity(key, track_index, channel)].push_back(note);
		}

		void note_off(uint8_t key, size_t track_index, uint8_t channel)
		{
			auto found = pending.find(identity(key, track_index, channel));
			if (found == pending.end() || found->second.empty())
				return;
			active.erase(found->second.back());
			found->second.pop_back();
			if (found->second.empty())
				pending.erase(found);
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
				size_t left = 2 * i + 1;
				size_t right = 2 * i + 2;
				if (left  < n && data[smallest] > data[left])  smallest = left;
				if (right < n && data[smallest] > data[right]) smallest = right;
				if (smallest == i) break;
				std::swap(data[i], data[smallest]);
				i = smallest;
			}
		}
	};

	struct offline_visual_render_state
	{
		playback_event_source* source = nullptr;
		generated_event next_event;
		std::unique_lock<std::mutex> run_lock;
		bool active = false;
		bool has_next_event = false;

		void reset()
		{
			source = nullptr;
			next_event = {};
			active = false;
			has_next_event = false;
			run_lock = {};
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
		std::atomic<uint64_t> pending_start_lead_in_us{0};

		// Lookahead limits: parser throttles when too far ahead
		static constexpr uint64_t max_lookahead_us = 5000000;  // 5 seconds max lookahead
		static constexpr uint64_t min_lookahead_us = 2500000;  // 2.5 seconds min before resuming parse

		// Event-count throttle: caps send_buffer at dense tick clusters where
		// the time-based throttle alone won't fire (millions of events packed
		// into a single tick or sub-millisecond span). The intentionally high
		// 2^26 ceiling preserves exceptionally dense same-tick playback.
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
			pending_start_lead_in_us = 0;
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
		uint64_t ticks_length{};
		uint64_t total_duration_us{0};

		std::atomic_uint64_t scanned{0};
		std::atomic_bool open_complete{false};
		std::atomic_uint64_t size{0};

		uint16_t ppq{0};

		void reset()
		{
			tracks.clear();
			tempo_tmp.clear();
			time_map_mcsecs.clear();
			ticks_length = 0;
			total_duration_us = 0;
			scanned.store(0, std::memory_order_relaxed);
			size.store(0, std::memory_order_relaxed);
			ppq = 0;
			open_complete.store(false, std::memory_order_release);
		}
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
	~simple_player()
	{
		shutdown();
	}

	void init()
	{
		shutdown_requested.store(false, std::memory_order_release);
		update_devices();
		//init_midi_out(devices.size() - 1);

		warnings = std::make_shared<printing_logger>("33");
	}

	// Get device names as vector of strings for UI
	std::vector<std::string> get_device_names() const
	{
		std::vector<std::string> names;
		names.reserve(devices.size() + (syncore_output::available() ? 1 : 0));

		for (const auto& device : devices)
		{
			std::wstring wname = device.szPname;
			std::string name(wname.size(), '\0');
			std::transform(wname.begin(), wname.end(), name.begin(), [](wchar_t c) { return static_cast<char>(c); });
			names.push_back(std::move(name));
		}
		if (syncore_output::available())
			names.emplace_back(syncore_device_name);

		return names;
	}

	static bool syncore_available() noexcept
	{
		return syncore_output::available();
	}

	size_t get_syncore_device_index() const noexcept
	{
		return syncore_output::available() ? devices.size() : ~size_t{0};
	}

	bool set_syncore_bank_path(std::wstring bank_path)
	{
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;
		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock() || shutdown_requested.load(std::memory_order_acquire))
			return false;

		if (syncore_bank_path == bank_path)
			return true;

		const bool reopen = current_device == get_syncore_device_index() && has_output();
		if (reopen)
			close_midi_out();
		syncore_bank_path = std::move(bank_path);
		if (reopen)
			init_midi_out(current_device);
		return !reopen || has_output();
	}

	const std::wstring& get_syncore_bank_path() const noexcept
	{
		return syncore_bank_path;
	}

	bool set_syncore_preferences(syncore_preferences preferences)
	{
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;
		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock() || shutdown_requested.load(std::memory_order_acquire))
			return false;

		if (syncore_preferences_ == preferences)
			return true;

		const bool reopen = current_device == get_syncore_device_index() && has_output();
		if (reopen)
			close_midi_out();
		syncore_preferences_ = preferences;
		if (reopen)
			init_midi_out(current_device);
		return !reopen || has_output();
	}

	const syncore_preferences& get_syncore_preferences() const noexcept
	{
		return syncore_preferences_;
	}

	syncore_runtime_status get_syncore_runtime_status() const
	{
		return syncore.status();
	}

	// Get currently selected device index
	size_t get_current_device() const
	{
		return current_device;
	}

	// Change the current device
	bool set_device(size_t device_index)
	{
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;
		// Device teardown/open and playback both mutate the output sink. Refuse a
		// device switch while a run owns the player instead of racing its sender.
		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
		{
			set_last_output_error("MIDI output is busy; stop playback before changing it");
			return false;
		}
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;

		return set_device_locked(device_index);
	}

	// Whether a MIDI out sink is ready for immediate messages
	bool has_output() const
	{
		return syncore.active() ||
			(hout.load(std::memory_order_acquire) != nullptr && short_msg != nullptr);
	}

	// Send an immediate note on/off outside of playback (piano roll audition)
	void preview_note(uint8_t channel, uint8_t key, uint8_t velocity, bool on)
	{
		send_output_message(make_smsg((on ? 0x90 : 0x80) | (channel & 0x0F),
			key & 0x7F, on ? (velocity & 0x7F) : 0x40));
	}

	// Restore device by name (for registry persistence)
	bool restore_device_by_name(const std::wstring& device_name)
	{
		if (device_name.empty())
			return false;

		const auto names = get_device_names();
		for (size_t i = 0; i < names.size(); ++i)
		{
			const std::wstring candidate(names[i].begin(), names[i].end());
			if (candidate != device_name)
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
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;
		if (has_output())
			return true;

		// Device selection is prepared on a different worker. Its list row is
		// highlighted immediately, so Play can arrive while set_device() is still
		// loading a bank and opening WASAPI. Playback runs off the UI thread and
		// should wait for that transition instead of reporting a false no-output
		// failure from try_to_lock.
		std::unique_lock<std::mutex> run_lock(playback_run_mutex);
		if (shutdown_requested.load(std::memory_order_acquire))
			return false;
		if (has_output())
			return true;

		if (!preferred_device_name.empty())
		{
			const auto names = get_device_names();
			for (size_t i = 0; i < names.size(); ++i)
			{
				const std::wstring candidate(names[i].begin(), names[i].end());
				if (candidate == preferred_device_name)
					return set_device_locked(i);
			}
		}

		return set_device_locked(current_device);
	}

	std::string get_last_output_error() const
	{
		std::lock_guard error_lock(output_error_mutex);
		return last_output_error;
	}

	// Callback for when device changes (for UI updates)
	void(*on_device_changed)(size_t device_index) = nullptr;

	void simple_run(std::wstring filename, double start_fraction = 0.0)
	{
		if (shutdown_requested.load(std::memory_order_acquire))
			return;

		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock() || shutdown_requested.load(std::memory_order_acquire))
			return;

		memory_failure_reported.store(false, std::memory_order_release);
		{
			std::lock_guard seek_lock(seek_request_mutex);
			if (shutdown_requested.load(std::memory_order_acquire))
				return;
			cancel_requested.store(false, std::memory_order_release);
		}

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
		if (!src || shutdown_requested.load(std::memory_order_acquire))
			return;

		std::unique_lock<std::mutex> run_lock(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
			return;

		memory_failure_reported.store(false, std::memory_order_release);
		{
			std::lock_guard seek_lock(seek_request_mutex);
			if (shutdown_requested.load(std::memory_order_acquire))
				return;
			cancel_requested.store(false, std::memory_order_release);
		}

		try
		{
			// The external parser converts ticks to time itself, so info only
			// needs the total duration for seek-fraction mapping and the UI bar.
			info.reset();
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
		const auto pause_position = get_position_us();
		state.pending_start_lead_in_us.store(
			current_start_lead_in_remaining_us(pause_position),
			std::memory_order_release);
		state.pause_position_us.store(pause_position, std::memory_order_release);
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
		set_playback_clock_from_pending_lead(pause_pos);

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
		{
			std::lock_guard seek_lock(seek_request_mutex);
			if (!state.playing.load(std::memory_order_acquire))
				return;

			fraction = std::clamp(fraction, 0.0, 1.0);
			const uint64_t target_us = static_cast<uint64_t>(
				fraction * info.total_duration_us);

			// A seek temporarily unpauses the parser. If another slider request
			// arrives during that restart, preserve the user's original pause state.
			const bool seek_in_progress =
				state.seek_requested.load(std::memory_order_relaxed) ||
				state.seeking_ff.load(std::memory_order_acquire);
			const bool was_paused = seek_in_progress
				? state.seek_resume_paused.load(std::memory_order_relaxed)
				: state.paused.load(std::memory_order_acquire);
			if (!was_paused && !seek_in_progress)
			{
				const auto current_position = get_position_us();
				state.pending_start_lead_in_us.store(
					current_start_lead_in_remaining_us(current_position),
					std::memory_order_release);
			}
			state.seek_resume_paused.store(was_paused, std::memory_order_relaxed);
			state.seek_target_us.store(target_us, std::memory_order_relaxed);
			state.seek_requested.store(true, std::memory_order_release);

			// Signal both playback threads to stop. The restart loop consumes the
			// latest request under this same mutex, so rapid seeks cannot be lost.
			state.paused.store(false, std::memory_order_release);
			state.stop_requested.store(true, std::memory_order_release);
		}
	}

	// Stop playback completely
	void stop()
	{
		{
			std::lock_guard seek_lock(seek_request_mutex);
			cancel_requested.store(true, std::memory_order_release);
			state.seek_requested.store(false, std::memory_order_release);
			state.stop_requested.store(true, std::memory_order_release);

			// If paused, unpause so threads can exit
			state.paused.store(false, std::memory_order_release);
		}
	}

	// The scheduler and the selected output have separate ownership.  Closing
	// the application must retire both while the player is still intact, rather
	// than defer SYNCore's thread joins to member/static destruction.
	void shutdown() noexcept
	{
		shutdown_requested.store(true, std::memory_order_release);
		stop();

		// Stop SYNCore immediately, including during asynchronous bank startup.
		// Its wrapper keeps senders safe while the current player run observes stop.
		syncore.stop();

		try
		{
			// A playback run owns this lock until its parser and sender have joined.
			// Waiting here means close_midi_out() cannot race the last MIDI sends.
			std::unique_lock run_lock(playback_run_mutex);
			close_midi_out();
		}
		catch (...)
		{
			// Destruction/GUI teardown cannot throw.  SYNCore was already stopped.
		}
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

	// Covers both the request-to-restart gap and the parser's fast-forward.
	// UI progress must not overwrite the user's slider target during either.
	bool is_seeking() const
	{
		return state.seek_requested.load(std::memory_order_acquire) ||
			state.seeking_ff.load(std::memory_order_acquire);
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
			const auto audio_floor = state.start_offset_us;
			const auto timeline_position = current_visual_position_us();
			const auto floor_i64 = clamp_to_i64(audio_floor);
			if (timeline_position <= floor_i64)
				return audio_floor;
			return static_cast<uint64_t>(timeline_position);
		}
		if (state.paused.load(std::memory_order_acquire))
			return state.pause_position_us.load(std::memory_order_acquire);
		return 0;
	}

	int64_t get_visual_position_us() const
	{
		return current_visual_position_us();
	}

	uint64_t get_start_lead_in_remaining_us() const
	{
		return current_start_lead_in_remaining_us(get_position_us());
	}

	bool open(std::wstring filename)
	{
		current_filename = filename;
		// prerequisite: this is a midi file with valid header;
		mmap = std::make_unique<dixelu::memory_mapped_file_reader>(filename);
		info.reset();

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

			if (!read_through_one_track(ptr, end))
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

	bool begin_offline_visual_render(playback_event_source& source)
	{
		if (offline_visual.active)
			return false;

		auto run_lock = std::unique_lock<std::mutex>(playback_run_mutex, std::try_to_lock);
		if (!run_lock.owns_lock())
			return false;

		cancel_requested.store(false, std::memory_order_release);
		memory_failure_reported.store(false, std::memory_order_release);

		state.reset();
		source.rewind();
		offline_visual.source = &source;
		offline_visual.has_next_event = source.next(offline_visual.next_event);
		offline_visual.active = true;
		offline_visual.run_lock = std::move(run_lock);
		state.playing.store(true, std::memory_order_release);
		state.paused.store(false, std::memory_order_release);
		state.parser_done.store(!offline_visual.has_next_event, std::memory_order_release);
		return true;
	}

	bool advance_offline_visual_render_to(int64_t current_us, uint64_t lookahead_us,
		bool (*external_cancel)(void*) noexcept = nullptr,
		void* external_cancel_user_data = nullptr)
	{
		if (!offline_visual.active)
			return false;

		state.current_time_us = current_us <= 0 ? 0 : static_cast<uint64_t>(current_us);
		state.sender_position_us.store(state.current_time_us, std::memory_order_release);

		const auto lookahead_i64 = clamp_to_i64(lookahead_us);
		const int64_t target_i64 = current_us > (std::numeric_limits<int64_t>::max)() - lookahead_i64
			? (std::numeric_limits<int64_t>::max)()
			: current_us + lookahead_i64;
		const uint64_t target_us = target_i64 <= 0 ? 0 : static_cast<uint64_t>(target_i64);

		while (offline_visual.has_next_event && !state.stop_requested)
		{
			if (external_cancel && external_cancel(external_cancel_user_data))
				return false;

			const auto event = offline_visual.next_event;
			if (event.time_us > target_us)
				break;

			state.parsed_up_to_us.store(event.time_us, std::memory_order_release);
			if (event.k == generated_event::kind::note_on)
				state.visuals.push_note_on(event.key, event.time_us, event.track_index,
					event.channel, event.velocity);
			else if (event.k == generated_event::kind::note_off)
				state.visuals.push_note_off(event.key, event.time_us, event.track_index,
					event.channel);

			offline_visual.has_next_event =
				offline_visual.source->next(offline_visual.next_event);
		}

		if (!offline_visual.has_next_event)
		{
			state.parser_done.store(true, std::memory_order_release);
		}

		return !state.stop_requested;
	}

	void end_offline_visual_render()
	{
		state.stop_requested.store(true, std::memory_order_release);
		state.playing.store(false, std::memory_order_release);
		state.visuals.reset();
		state.track_states.clear();
		offline_visual.reset();
	}

	// Parser thread: pre-parses MIDI events into the lookahead buffer
	// skip_to_us > 0: reconstruct channel and held-note state at that time.
	SIMPLE_PLAYER_FORCE_NO_INLINE void parser_thread_func(uint64_t skip_to_us = 0, bool pause_after_seek = false)
	{
		update_tempo_cache_at(~0ULL); // initialize cache for tick 0

		bool fast_forwarding = (skip_to_us > 0);
		seek_note_tracker held_notes;
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
				complete_fast_forward(skip_to_us, pause_after_seek, &held_notes);
			}

			if (!fast_forwarding)
			{
				// Throttle: wait if we're too far ahead in time, have too many
				// pending events buffered, or are paused.
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
					size_t pending = state.send_buffer.approximate_size();

					if (lookahead < playback_state::max_lookahead_us &&
						pending < playback_state::max_pending_events)
						break;

					// Too far ahead - sleep until sender catches up closer
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
							uint8_t channel = command & 0x0F;

							if (fast_forwarding)
								held_notes.note_off(data1, ref.track_index, channel);
							else
							{
								msg_to_send = make_smsg(command, data1, data2);
								state.visuals.push_note_off(data1, batch_time_us, ref.track_index, channel);
							}
							break;
						}
						case 0x9: // note on
						{
							data2 = get_value_and_increment(track.position, track.end);
							uint8_t channel = command & 0x0F;

							if (fast_forwarding)
							{
								if (data2 > 0)
									held_notes.note_on(data1, batch_time_us,
										ref.track_index, channel, data2);
								else
									held_notes.note_off(data1, ref.track_index, channel);
							}
							else
							{
								msg_to_send = make_smsg(command, data1, data2);

								if (data2 > 0)
									state.visuals.push_note_on(data1, batch_time_us, ref.track_index, channel, data2);
								else
									state.visuals.push_note_off(data1, batch_time_us, ref.track_index, channel);
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
						// Reconstructed channel state belongs at the seek boundary.
						// Historical timestamps would underflow against start_offset_us
						// if the sender had not drained them before FF completed.
						state.send_buffer.push({
							fast_forwarding ? skip_to_us : batch_time_us, msg_to_send});

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

		// The requested position can be after the final MIDI event. Complete the
		// seek at that position instead of leaving the sender in fast-forward mode.
		if (fast_forwarding && !state.stop_requested)
			complete_fast_forward(skip_to_us, pause_after_seek, &held_notes);

		state.parser_done.store(true, std::memory_order_release);
	}

	// Parser variant that pulls from an external event source instead of the
	// mmap'd file. Shares the sender thread, timing, throttle, visuals, pause
	// and seek machinery with the file path — only event production differs.
	// skip_to_us > 0: fast-forward to that time, reconstructing controller,
	// program, and held-note state so the synth is correct at the seek point.
	SIMPLE_PLAYER_FORCE_NO_INLINE void parser_from_source_thread_func(
		playback_event_source* src, uint64_t skip_to_us = 0, bool pause_after_seek = false)
	{
		bool fast_forwarding = (skip_to_us > 0);
		seek_note_tracker held_notes;
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
				complete_fast_forward(skip_to_us, pause_after_seek, &held_notes);
			}

			if (!fast_forwarding)
			{
				// Throttle: wait if too far ahead in time, too many pending events
				// buffered, or paused. Identical policy to the file parser.
				while (!state.stop_requested)
				{
					if (state.paused.load(std::memory_order_acquire))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}

					uint64_t sender_pos = state.sender_position_us.load(std::memory_order_acquire);
					uint64_t lookahead = (batch_time_us > sender_pos) ? (batch_time_us - sender_pos) : 0;
					size_t pending = state.send_buffer.approximate_size();

					if (lookahead < playback_state::max_lookahead_us &&
						pending < playback_state::max_pending_events)
						break;

					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}

				if (state.stop_requested)
					break;

				state.parsed_up_to_us.store(batch_time_us, std::memory_order_release);
			}

			const bool is_note = (ev.k == generated_event::kind::note_on ||
				ev.k == generated_event::kind::note_off);

			// During fast-forward, retain only notes still held at the target. They
			// are re-struck when the seek completes; ended notes are discarded.
			if (fast_forwarding && is_note)
			{
				if (ev.k == generated_event::kind::note_on && ev.velocity != 0)
					held_notes.note_on(ev.key, batch_time_us, ev.track_index,
						ev.channel, ev.velocity);
				else
					held_notes.note_off(ev.key, ev.track_index, ev.channel);
				continue;
			}

			// Falling-notes visuals (only in timed playback, matching the file path)
			if (!fast_forwarding)
			{
				if (ev.k == generated_event::kind::note_on)
					state.visuals.push_note_on(ev.key, batch_time_us, ev.track_index, ev.channel, ev.velocity);
				else if (ev.k == generated_event::kind::note_off)
					state.visuals.push_note_off(ev.key, batch_time_us, ev.track_index, ev.channel);
			}

			if (ev.short_msg != 0)
			{
				state.send_buffer.push({
					fast_forwarding ? skip_to_us : batch_time_us, ev.short_msg});

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

		if (fast_forwarding && !state.stop_requested)
			complete_fast_forward(skip_to_us, pause_after_seek, &held_notes);

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
					if (ev.short_msg != 0 && !send_playback_message(ev.short_msg))
						continue;
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

				// With no queued event there is nothing else to publish the playback
				// clock. Keep it moving so the parser can cross a long (> lookahead)
				// rest instead of both threads waiting on each other forever.
				const uint64_t clock_position = get_position_us();
				const uint64_t published_position =
					state.sender_position_us.load(std::memory_order_relaxed);
				if (clock_position > published_position)
					state.sender_position_us.store(clock_position, std::memory_order_release);

				std::this_thread::sleep_for(std::chrono::microseconds(100));
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

			const uint64_t target_us = ev.time_us > state.start_offset_us
				? ev.time_us - state.start_offset_us : 0;
			const int64_t target_elapsed_us = clamp_to_i64(target_us);

			// Wait in bounded slices so Stop, Pause, and a new seek remain
			// responsive even when the next event is seconds or hours away.
			while (!state.stop_requested &&
				!state.paused.load(std::memory_order_acquire))
			{
				auto elapsed = std::chrono::steady_clock::now() - state.start_time;
				cached_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
				if (cached_elapsed_us >= target_elapsed_us)
					break;

				const auto wait_us = target_elapsed_us - cached_elapsed_us;
				if (wait_us > 200)
					std::this_thread::sleep_for(std::chrono::microseconds(
						(std::min)(wait_us - 20, int64_t{5'000})));
				else
					std::this_thread::yield();
			}

			if (state.stop_requested)
				break;
			if (state.paused.load(std::memory_order_acquire))
				continue;

			// send the event
			if (ev.short_msg != 0 && !send_playback_message(ev.short_msg))
				continue;

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
			state.pending_start_lead_in_us.store(default_start_lead_in_us,
				std::memory_order_release);
			if (!start_paused && initial_skip_to_us == 0)
				set_playback_clock_from_pending_lead(0);

			uint64_t skip_to_us = initial_skip_to_us;
			bool pause_after_seek = initial_skip_to_us > 0 && start_paused;

			for (;;)
			{
				bool consumed_seek = false;
				bool stop_before_restart = false;
				{
					std::lock_guard seek_lock(seek_request_mutex);
					stop_before_restart = cancel_requested.load(std::memory_order_acquire);
					if (!stop_before_restart &&
						state.seek_requested.load(std::memory_order_acquire))
					{
						skip_to_us = state.seek_target_us.load(std::memory_order_relaxed);
						pause_after_seek = state.seek_resume_paused.load(std::memory_order_relaxed);
						state.seek_requested.store(false, std::memory_order_relaxed);
						state.paused.store(pause_after_seek, std::memory_order_relaxed);
						consumed_seek = true;
					}

					if (!stop_before_restart)
					{
						// Clear the previous parser's stop signal atomically with
						// consuming the newest seek request. A later request cannot
						// be overwritten by this restart.
						state.stop_requested.store(false, std::memory_order_relaxed);
						state.seeking_ff.store(skip_to_us > 0, std::memory_order_relaxed);
					}
				}
				if (stop_before_restart)
					break;

				// Seeking to the start does not enter the parser's fast-forward
				// branch, so reset its playback clock here.
				if (consumed_seek && skip_to_us == 0)
					complete_fast_forward(0, pause_after_seek);

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
					parser_thread = std::thread([this, skip_to_us, pause_after_seek]()
					{
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

				bool restart_for_seek = false;
				{
					std::lock_guard seek_lock(seek_request_mutex);
					restart_for_seek =
						state.seek_requested.load(std::memory_order_acquire) &&
						!cancel_requested.load(std::memory_order_acquire);
					if (!restart_for_seek)
						state.playing.store(false, std::memory_order_release);
				}
				if (restart_for_seek)
				{
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

		if (syncore.active() && !state.stop_requested.load(std::memory_order_acquire))
		{
			// Natural EOF releases notes behind the queued ending automation.
			// Panic/CC120 would cut off the piano's remaining release tail.
			for (uint8_t channel = 0; channel < 16; ++channel)
				if (!send_playback_message(make_smsg(0xB0 | channel, 64)) ||
					!send_playback_message(make_smsg(0xB0 | channel, 123)))
					break;
		}
		else
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
		struct note_vert { float x, y; };
		struct note_color { uint8_t r, g, b, a; };

		struct note_span
		{
			float begin_y;
			float end_y;
			uint32_t track_n;
			uint32_t color;
			bool touches_keyboard;
		};

		struct interval
		{
			float begin_y;
			float end_y;
			uint32_t track;
		};

		std::vector<note_vert>  note_verts;
		std::vector<note_color> note_colors;
		std::vector<note_vert>  outline_verts;
		std::vector<note_color> outline_colors;
		std::vector<note_span>  key_note_spans;
		std::vector<interval>   covered_intervals;

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

		void draw_keyboard(const color(&key_colors)[128])
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
					auto shade = [tone](uint8_t channel)
					{
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

		static constexpr uint8_t MAX_OVERLAPS_REMOVAL_VERSION = 1;
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

			quad_geometry* first = keyboard, * last = keyboard + 127;
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

	SIMPLE_PLAYER_FORCE_NO_INLINE void overlaps_removal_v1(
		std::vector<draw_data::note_span>& note_spans)
	{
		if (note_spans.empty())
			return;

		// Backward redundancy filter, in place. Walk right→left; within each
		// begin_y block, keep an entry iff its end_y exceeds the running max of
		// later kept end_ys in that block. Zero-height entries are always kept
		// and do not contribute to run_max. Survivors are written toward the
		// tail of the vector, then compacted to the front.
		const std::size_t size = note_spans.size();
		std::size_t	write = size;
		float		run_max = -std::numeric_limits<float>::infinity();
		float		prev_a = std::numeric_limits<float>::infinity();

		for (std::size_t k = size; k-- > 0; )
		{
			if (note_spans[k].begin_y != prev_a)
			{
				run_max = -std::numeric_limits<float>::infinity();
				prev_a = note_spans[k].begin_y;
			}

			const bool zero_height = (note_spans[k].begin_y == note_spans[k].end_y);
			const bool extends = (note_spans[k].end_y > run_max);

			if (!zero_height && extends)
			{
				run_max = note_spans[k].end_y;
				--write;
				note_spans[write] = note_spans[k];
			}
			// else: covered by a later kept stripe in this block → drop
		}

		const std::size_t kept = size - write;
		if (write > 0)
			std::move(note_spans.begin() + write, note_spans.end(), note_spans.begin());
		note_spans.resize(kept);
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void overlaps_removal_v0(
		draw_data& data,
		const auto& emit_span)
	{
		data.covered_intervals.clear();
		data.covered_intervals.reserve(data.key_note_spans.size());

		auto add_covered_interval = [&](float begin_y, float end_y)
		{
			if (begin_y >= end_y)
				return;

			auto insert_it = data.covered_intervals.begin();
			while (insert_it != data.covered_intervals.end() && insert_it->end_y < begin_y)
				++insert_it;

			float merged_begin = begin_y;
			float merged_end = end_y;
			while (insert_it != data.covered_intervals.end() && insert_it->begin_y <= merged_end)
			{
				merged_begin = std::min(merged_begin, insert_it->begin_y);
				merged_end = std::max(merged_end, insert_it->end_y);
				insert_it = data.covered_intervals.erase(insert_it);
			}

			data.covered_intervals.insert(insert_it, {merged_begin, merged_end});
		};

		for (auto it = data.key_note_spans.rbegin(); it != data.key_note_spans.rend(); ++it)
		{
			float cursor = it->begin_y;
			for (const auto& covered : data.covered_intervals)
			{
				if (covered.end_y <= cursor)
					continue;
				if (covered.begin_y >= it->end_y)
					break;

				if (covered.begin_y > cursor)
					emit_span(cursor, std::min(covered.begin_y, it->end_y), it->color);

				cursor = std::max(cursor, covered.end_y);
				if (cursor >= it->end_y)
					break;
			}

			if (cursor < it->end_y)
				emit_span(cursor, it->end_y, it->color);

			add_covered_interval(it->begin_y, it->end_y);
		}
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void draw(draw_data& data)
	{
		draw_at(data, get_visual_position_us());
	}

	SIMPLE_PLAYER_FORCE_NO_INLINE void draw_at(draw_data& data, int64_t current_us)
	{
		constexpr int total_white = draw_data::white_keys_count();
		auto& visuals = get_visuals();

		if (data.enable_simulated_lag)
		{
			const auto max = clamp_to_i64(
				state.parsed_up_to_us.load(std::memory_order_relaxed));
			const auto latest_visible_start = max - clamp_to_i64(data.scroll_window_us);

			if (current_us > latest_visible_start)
				current_us = latest_visible_start;
		}

		// draw falling notes
		draw_data::color keyboard_colors[128];
		memset(keyboard_colors, 0xFF, sizeof(draw_data::color) * total_white);
		memset(keyboard_colors + total_white, 0x00, sizeof(draw_data::color) * (128 - total_white));

		GLsizei white_fill_verts = 0;
		GLsizei white_outline_verts = 0;

		try
		{
			// lock against visuals.reset() which may be called by playback_thread on seek/restart.
			std::lock_guard<std::mutex> visuals_lock(visuals.access_mutex);

			visuals.cull_expired(current_us - clamp_to_i64(data.scroll_window_us));

			size_t queued_notes = 0;
			for (const auto& notes : visuals.falling_notes)
				if (notes)
					queued_notes += notes->size();

			const size_t fill_vert_capacity = queued_notes * 4;
			const size_t outline_vert_capacity = queued_notes * 8;
			if (data.note_verts.capacity() < fill_vert_capacity)
				data.note_verts.reserve(fill_vert_capacity);
			if (data.note_colors.capacity() < fill_vert_capacity)
				data.note_colors.reserve(fill_vert_capacity);
			if (data.outline_verts.capacity() < outline_vert_capacity)
				data.outline_verts.reserve(outline_vert_capacity);
			if (data.outline_colors.capacity() < outline_vert_capacity)
				data.outline_colors.reserve(outline_vert_capacity);

			auto batch_notes_for_index = [&](int index)
			{
				uint8_t key = data.key_n[index];
				data.key_note_spans.clear();
				const auto& notes = visuals.falling_notes[key];
				if (!notes)
					return;

				for (auto it = notes->begin(); it != notes->end(); ++it)
				{
					auto& note = *it;

					uint64_t end_time = note.end_time_us;

					int64_t start_offset = clamp_to_i64(note.start_time_us) - current_us;
					int64_t end_offset = 0;

					float begin_y = float(start_offset) / float(data.scroll_window_us);
					float end_y = 1;

					if (end_time != ~0ULL)
					{
						end_offset = clamp_to_i64(end_time) - current_us;
						end_y = float(end_offset) / float(data.scroll_window_us);
					}

					if (begin_y > 1.01f)
						break; // Queue is ordered by start_time_us, so later notes are even further above view.

					if (end_y < -0.01f)
						continue;

					data.key_note_spans.push_back({
						std::clamp(begin_y, 0.f, 1.f),
						std::clamp(end_y, 0.f, 1.f),
						note.track_id,
						rotate(0xFF7F008F, note.track_id),
						begin_y <= 0 && end_y >= 0
						});
				}

				if (data.key_note_spans.empty())
					return;

				for (auto it = data.key_note_spans.rbegin(); it != data.key_note_spans.rend(); ++it)
				{
					if (!it->touches_keyboard)
						continue;

					keyboard_colors[index] = {
						uint8_t(it->color >> 24),
						uint8_t(it->color >> 16),
						uint8_t(it->color >> 8),
					};
					break;
				}

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

					data.outline_verts.reserve(data.outline_verts.size() + 16);
					data.outline_verts.push_back({lx, begin_y}); data.outline_verts.push_back({lx, end_y});
					data.outline_verts.push_back({lx, end_y});   data.outline_verts.push_back({rx, end_y});
					data.outline_verts.push_back({rx, end_y});   data.outline_verts.push_back({rx, begin_y});
					data.outline_verts.push_back({rx, begin_y}); data.outline_verts.push_back({lx, begin_y});

					for (int e = 0; e < 8; ++e)
						data.outline_colors.push_back(note_border_col);
				};

				if (data.remove_overlaps <= draw_data::MAX_OVERLAPS_REMOVAL_VERSION)
				{
					if (data.remove_overlaps == 0)
					{
						overlaps_removal_v0(data, emit_span);
						return;
					}
					else if (data.remove_overlaps == 1)
					{
						overlaps_removal_v1(data.key_note_spans);
						// [[fallthrough]];
					}
				}

				for (const auto& span : data.key_note_spans)
					emit_span(span.begin_y, span.end_y, span.color);
			};

			// White key notes first (drawn behind), record split point, then black key notes on top
			for (int index = 0; index < total_white; ++index)
				batch_notes_for_index(index);

			white_fill_verts = static_cast<GLsizei>(data.note_verts.size());
			white_outline_verts = static_cast<GLsizei>(data.outline_verts.size());
			for (int index = total_white; index < 128; ++index)
				batch_notes_for_index(index);
		}
		catch (const std::bad_alloc&)
		{
			handle_memory_failure(true);
			return;
		}

		// Draw order: white fills, white outlines, black fills, black outlines.
		// Matches original per-note ordering so black notes fully overdraw white note outlines.
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		if (!data.note_verts.empty())
		{
			const GLsizei total_fill = static_cast<GLsizei>(data.note_verts.size());
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

			data.note_verts.clear();
			data.note_colors.clear();
			data.outline_verts.clear();
			data.outline_colors.clear();
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
	mutable std::mutex seek_request_mutex;
	mutable std::mutex output_error_mutex;
	std::string last_output_error;

	void set_last_output_error(std::string message)
	{
		std::lock_guard error_lock(output_error_mutex);
		last_output_error = std::move(message);
	}

	static int64_t clamp_to_i64(uint64_t value) noexcept
	{
		constexpr auto max_i64 = static_cast<uint64_t>((std::numeric_limits<int64_t>::max)());
		return value > max_i64 ? (std::numeric_limits<int64_t>::max)() :
			static_cast<int64_t>(value);
	}

	int64_t current_visual_position_us() const
	{
		if (state.playing.load(std::memory_order_acquire) &&
			!state.paused.load(std::memory_order_acquire))
		{
			const auto elapsed = std::chrono::steady_clock::now() - state.start_time;
			return clamp_to_i64(state.start_offset_us) +
				std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		}
		if (state.paused.load(std::memory_order_acquire))
		{
			return clamp_to_i64(state.pause_position_us.load(std::memory_order_acquire)) -
				clamp_to_i64(state.pending_start_lead_in_us.load(std::memory_order_acquire));
		}
		return 0;
	}

	uint64_t current_start_lead_in_remaining_us(uint64_t audio_position_us) const
	{
		const auto audio_position = clamp_to_i64(audio_position_us);
		const auto visual_position = current_visual_position_us();
		return visual_position < audio_position
			? static_cast<uint64_t>(audio_position - visual_position)
			: 0;
	}

	void set_playback_clock(uint64_t start_offset_us, uint64_t lead_in_us)
	{
		state.start_offset_us = start_offset_us;
		state.start_time = std::chrono::steady_clock::now() +
			std::chrono::microseconds(clamp_to_i64(lead_in_us));
	}

	void set_playback_clock_from_pending_lead(uint64_t start_offset_us)
	{
		set_playback_clock(start_offset_us,
			state.pending_start_lead_in_us.exchange(0, std::memory_order_acq_rel));
	}

	void complete_fast_forward(uint64_t position_us, bool pause_after_seek,
		seek_note_tracker* held_notes = nullptr)
	{
		state.start_offset_us = position_us;
		state.current_time_us = position_us;
		state.sender_position_us.store(position_us, std::memory_order_release);
		state.parsed_up_to_us.store(position_us, std::memory_order_release);

		// Publish the requested pause state before clearing seeking_ff. A Play
		// click waiting on fast-forward then cannot race this re-pause.
		if (pause_after_seek)
		{
			state.pause_position_us.store(position_us, std::memory_order_release);
			state.paused.store(true, std::memory_order_release);
		}
		else
		{
			set_playback_clock_from_pending_lead(position_us);
			state.paused.store(false, std::memory_order_release);
		}

		// Restore visuals before publishing seek completion, so the first frame at
		// the new location already contains notes crossing the seek boundary.
		if (held_notes)
			for (const auto& note : held_notes->active)
				state.visuals.push_note_on(note.key, note.start_time_us,
					note.track_index, note.channel, note.velocity);

		state.seeking_ff.store(false, std::memory_order_release);

		// Audio note-ons are queued only after fast-forward has ended. Otherwise
		// the sender's immediate-drain branch could sound them during a paused seek.
		if (held_notes)
		{
			for (const auto& note : held_notes->active)
				state.send_buffer.push({position_us, make_smsg(
					static_cast<uint8_t>(0x90 | (note.channel & 0x0F)),
					note.key, note.velocity)});
			held_notes->active.clear();
			held_notes->pending.clear();
		}
	}

	bool set_device_locked(size_t device_index)
	{
		if (device_index >= get_device_names().size())
		{
			set_last_output_error("The selected MIDI output device is no longer available");
			return false;
		}
		if (device_index == current_device && has_output())
			return true;

		close_midi_out();
		current_device = device_index;
		set_last_output_error({});
		init_midi_out(device_index);

		if (on_device_changed)
			on_device_changed(device_index);

		if (has_output())
			return true;
		if (get_last_output_error().empty())
			set_last_output_error("The selected MIDI output could not be opened");
		return false;
	}

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

		auto direct_send = reinterpret_cast<void(WINAPI*)(uint32_t)>(
			GetProcAddress(moduleHandle, "SendDirectData"));
		if (direct_send)
			short_msg = direct_send;
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
			[](tick_type t, const std::pair<uint64_t, uint64_t>& entry)
		{
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

		if (devices.size() || syncore_output::available())
			current_device = 0; // select first one
	}

	void close_midi_out()
	{
		int attempts = 0;
		set_short_msg_noop();

		// active() remains false while SYNCore is loading/preparing, but that
		// session still owns a delivery thread and must be stopped on close.
		syncore.stop();

		if (!hout)
			return;

		kdmapi_status = nullptr;

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
		{L"OmniMIDI", L"K[q093jfpowe"};

		auto hout_copy = hout.load();
		if (hout_copy || syncore.active())
			return;

		if (device == get_syncore_device_index())
		{
			std::string error;
			if (!syncore.start(syncore_bank_path, syncore_preferences_, error))
			{
				set_last_output_error("Unable to start SYNCore: " + error);
				throw_alert_error(get_last_output_error());
				return;
			}
			set_last_output_error({});
			return;
		}

		if (device >= devices.size())
			return;

		try
		{
			if (midiOutOpen(&hout_copy, device, 0, 0, 0) != MMSYSERR_NOERROR)
			{
				std::wstring name = devices[device].szPname;
				std::string readable_name(name.size(), '\0');
				std::transform(name.begin(), name.end(), readable_name.begin(), [](wchar_t c) { return static_cast<char>(c); });
				set_last_output_error("Unable to open MIDI out '" + readable_name + "'!");
				throw_alert_error(get_last_output_error());
				hout = nullptr;
			}
			else
			{
				hout = hout_copy;
				short_msg = [](uint32_t msg) { midiOutShortMsg(hout, msg); };
				set_last_output_error({});
			}

			if (!kdmapi_status && kdmapi_allowed.contains(devices[device].szPname))
				try_init_kdmapi();
		}
		catch (...)
		{
			set_last_output_error("midiOutOpen() failed unexpectedly");
			throw_alert_error(get_last_output_error());
		}
	}

	void all_notes_off_channel(uint8_t channel)
	{
		if (!has_output()) [[unlikely]]
			return;

		channel &= 0x0F;

		send_output_message(make_smsg(0xB0 | channel, 120));
		send_output_message(make_smsg(0xB0 | channel, 121));
		send_output_message(make_smsg(0xB0 | channel, 123));
	}

	bool send_playback_message(uint32_t message)
	{
		if (!syncore.active())
			return send_output_message(message);
		// A file sender can wait for capacity. Dropping a dense burst through
		// the live-input API triggers panic recovery and erases held carriers.
		while (!state.stop_requested.load(std::memory_order_acquire))
		{
			if (!state.seeking_ff.load(std::memory_order_acquire) &&
				state.paused.load(std::memory_order_acquire))
				return false;
			switch (syncore.try_send_short_message(message))
			{
			case syncore_send_result::queued:
				return true;
			case syncore_send_result::unavailable:
				set_last_output_error("SYNCore stopped while sending MIDI");
				state.stop_requested.store(true, std::memory_order_release);
				return false;
			case syncore_send_result::full:
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				break;
			}
		}
		return false;
	}

	bool send_output_message(uint32_t message) noexcept
	{
		if (syncore.active())
			return syncore.send_short_message(message);
		if (!short_msg)
			return false;
		short_msg(message);
		return true;
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
	offline_visual_render_state offline_visual;
	mutable tempo_cache tcache;

	// Non-null only during run_from_external: playback_thread routes the parser
	// through this source instead of the mmap'd file. Owned by the caller.
	playback_event_source* external_source = nullptr;

	size_t current_device = ~0ULL;
	std::vector<MIDIOUTCAPSW> devices;
	syncore_output syncore;
	std::wstring syncore_bank_path;
	syncore_preferences syncore_preferences_;
	inline static std::atomic<HMIDIOUT> hout;
	std::atomic<bool> memory_failure_reported{false};
	std::atomic<bool> cancel_requested{false};
	std::atomic<bool> shutdown_requested{false};

	void(WINAPI* short_msg)(uint32_t msg) = nullptr;
	bool(WINAPI* kdmapi_status)() = nullptr;
	inline static constexpr const char* syncore_device_name = "SYNCore (embedded)";

	constexpr static std::uint32_t MTrk_header = 1297379947;
	constexpr static std::uint32_t MThd_header = 1297377380;
};

#endif
