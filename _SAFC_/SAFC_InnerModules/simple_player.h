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
#include <unordered_map>

#include "../bbb_ffio.h"

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"

#define SIMPLE_PLAYER_FORCE_NO_INLINE 
// __declspec(noinline)
// __declspec(noinline)

// Lock-free SPSC slab-based queue
// Producer (parser): push, back
// Consumer (renderer): pop, front, empty, iteration
template<typename T>
struct buffered_queue_spsc
{
private:
	static constexpr size_t slab_size = 1 << 15;

	struct slab
	{
		alignas(64) std::aligned_storage_t<sizeof(T), alignof(T)> data[slab_size];

		T* begin;                      // consumer-owned, only consumer modifies
		std::atomic<T*> end;           // producer writes, consumer reads
		std::atomic<slab*> next_slab;  // producer writes, consumer reads

		slab() :
			begin(reinterpret_cast<T*>(data)),
			end(reinterpret_cast<T*>(data)),
			next_slab(nullptr)
		{}

		T* capacity_end() const { return reinterpret_cast<T*>(const_cast<slab*>(this)->data) + slab_size; }
		T* data_begin() { return reinterpret_cast<T*>(data); }

		[[nodiscard]] bool empty_consumer() const
		{
			return begin == end.load(std::memory_order_acquire);
		}

		T& front() { return *begin; }
		const T& front() const { return *begin; }

		void pop_consumer() { begin->~T(); ++begin; }

		bool full_producer() const
		{
			return end.load(std::memory_order_relaxed) == capacity_end();
		}

		void push_producer(T&& v)
		{
			T* cur_end = end.load(std::memory_order_relaxed);
			::new(cur_end) T(std::move(v));
			end.store(cur_end + 1, std::memory_order_release);
		}

		T& back_producer()
		{
			return *(end.load(std::memory_order_relaxed) - 1);
		}

		void reset_for_reuse()
		{
			begin = data_begin();
			end.store(data_begin(), std::memory_order_relaxed);
			next_slab.store(nullptr, std::memory_order_relaxed);
		}

		void clear_consumer()
		{
			while (begin != end.load(std::memory_order_acquire))
				pop_consumer();
		}
	};

	// Producer-owned
	alignas(64) slab* tail_ = nullptr;

	// Consumer-owned
	alignas(64) slab* head_ = nullptr;

	// Lock-free recycle stack: consumer pushes, producer pops
	alignas(64) std::atomic<slab*> recycle_head_{nullptr};

	slab* producer_get_slab()
	{
		// Try recycle stack first (lock-free pop)
		slab* r = recycle_head_.load(std::memory_order_acquire);
		while (r)
		{
			slab* next = r->next_slab.load(std::memory_order_relaxed);
			if (recycle_head_.compare_exchange_weak(r, next,
				std::memory_order_acquire, std::memory_order_relaxed))
			{
				r->reset_for_reuse();
				return r;
			}
		}
		return new slab();
	}

	void consumer_recycle_slab(slab* s)
	{
		// Lock-free push to recycle stack
		slab* old_head = recycle_head_.load(std::memory_order_relaxed);
		do {
			s->next_slab.store(old_head, std::memory_order_relaxed);
		} while (!recycle_head_.compare_exchange_weak(old_head, s,
			std::memory_order_release, std::memory_order_relaxed));
	}

	void ensure_initialized_producer()
	{
		if (!tail_)
		{
			tail_ = producer_get_slab();
			head_ = tail_;
		}
	}

public:
	buffered_queue_spsc() = default;

	~buffered_queue_spsc()
	{
		// Clean up main chain
		while (head_)
		{
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			head_->clear_consumer();
			delete head_;
			head_ = next;
		}
		// Clean up recycle stack
		slab* r = recycle_head_.load(std::memory_order_relaxed);
		while (r)
		{
			slab* next = r->next_slab.load(std::memory_order_relaxed);
			delete r;
			r = next;
		}
	}

	// Non-copyable, non-movable (due to internal pointers)
	buffered_queue_spsc(const buffered_queue_spsc&) = delete;
	buffered_queue_spsc& operator=(const buffered_queue_spsc&) = delete;

	// Producer: push element
	SIMPLE_PLAYER_FORCE_NO_INLINE void push(T&& value)
	{
		ensure_initialized_producer();

		if (tail_->full_producer())
		{
			slab* new_slab = producer_get_slab();
			if (!new_slab) [[unlikely]]
				return;

			new_slab->push_producer(std::move(value));
			// Publish new slab - consumer will see it when following next_slab
			tail_->next_slab.store(new_slab, std::memory_order_release);
			tail_ = new_slab;
		}
		else
		{
			tail_->push_producer(std::move(value));
		}
	}

	// Producer: get reference to last pushed element (for pending tracking)
	[[nodiscard]] T& back()
	{
		return tail_->back_producer();
	}

	// Consumer: pop front element
	SIMPLE_PLAYER_FORCE_NO_INLINE void pop()
	{
		if (!head_) [[unlikely]]
			return;

		head_->pop_consumer();

		// If current slab exhausted, try to advance
		if (head_->empty_consumer())
		{
			slab* next = head_->next_slab.load(std::memory_order_acquire);
			if (next)
			{
				slab* old = head_;
				head_ = next;
				consumer_recycle_slab(old);
			}
			// else: keep empty slab as head (producer might add more)
		}
	}

	// Consumer: check if empty
	[[nodiscard]] bool empty() const
	{
		if (!head_)
			return true;

		if (!head_->empty_consumer())
			return false;

		// Current slab empty - check if there's a next one
		return head_->next_slab.load(std::memory_order_acquire) == nullptr;
	}

	// Consumer: get front element
	[[nodiscard]] T& front()
	{
		// Advance past empty slabs if needed
		while (head_->empty_consumer())
		{
			slab* next = head_->next_slab.load(std::memory_order_acquire);
			if (!next) break;
			slab* old = head_;
			head_ = next;
			consumer_recycle_slab(old);
		}
		return head_->front();
	}

	[[nodiscard]] const T& front() const
	{
		return const_cast<buffered_queue_spsc*>(this)->front();
	}

	// Clear all elements (consumer operation, but callable during reset)
	void clear()
	{
		while (head_)
		{
			head_->clear_consumer();
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			if (next)
			{
				consumer_recycle_slab(head_);
				head_ = next;
			}
			else
			{
				// Keep one slab for reuse
				head_->reset_for_reuse();
				tail_ = head_;
				break;
			}
		}
	}

	// Consumer iterator for traversal
	struct iterator
	{
		slab* current_slab;
		T* cur;
		T* slab_end;  // cached end for current slab

		iterator(slab* s, T* p, T* e) : current_slab(s), cur(p), slab_end(e) {}

		T& operator*() { return *cur; }
		T* operator->() { return cur; }

		iterator& operator++()
		{
			++cur;
			if (cur >= slab_end)
			{
				slab* next = current_slab ?
					current_slab->next_slab.load(std::memory_order_acquire) : nullptr;
				if (next)
				{
					current_slab = next;
					cur = current_slab->begin;
					slab_end = current_slab->end.load(std::memory_order_acquire);
				}
				// else: stay at end position
			}
			return *this;
		}

		bool operator!=(const iterator& other) const { return cur != other.cur; }
		bool operator==(const iterator& other) const { return cur == other.cur; }
	};

	// Consumer: begin iterator
	[[nodiscard]] iterator begin()
	{
		if (!head_)
			return iterator(nullptr, nullptr, nullptr);
		T* e = head_->end.load(std::memory_order_acquire);
		return iterator(head_, head_->begin, e);
	}

	// Consumer: end iterator (snapshot of current tail position)
	[[nodiscard]] iterator end()
	{
		if (!tail_)
			return iterator(nullptr, nullptr, nullptr);
		T* e = tail_->end.load(std::memory_order_acquire);
		return iterator(tail_, e, e);
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
	// Note: end_time_us is atomic for lock-free note_off updates
	struct buffered_note
	{
		uint64_t start_time_us;              // note on time (immutable after creation)
		std::atomic<uint64_t> end_time_us;   // note off time (~0 = still held/pending)
		uint32_t track_id;                   // (track_index << 4) | channel (immutable after creation)

		buffered_note() : start_time_us(0), end_time_us(~0ULL), track_id(0) {}

		buffered_note(uint64_t start, uint64_t end, uint32_t track)
			: start_time_us(start), end_time_us(end), track_id(track) {}

		// Move constructor for queue operations
		buffered_note(buffered_note&& other) noexcept
			: start_time_us(other.start_time_us)
			, end_time_us(other.end_time_us.load(std::memory_order_relaxed))
			, track_id(other.track_id) {}

		buffered_note& operator=(buffered_note&& other) noexcept
		{
			start_time_us = other.start_time_us;
			end_time_us.store(other.end_time_us.load(std::memory_order_relaxed), std::memory_order_relaxed);
			track_id = other.track_id;
			return *this;
		}
	};

	// Visuals viewport: manages notes display and keyboard state
	// Lock-free SPSC: parser thread pushes notes, render thread reads state
	struct visuals_viewport
	{
		static constexpr size_t key_count = 128;

		// Pending note tracking entry - points to note in queue awaiting its note_off
		// Only accessed by parser thread - no synchronization needed
		struct pending_entry
		{
			uint32_t track_id;
			buffered_note* note_ptr;
		};

		// Per-track_id LIFO stacks of pending notes per key.
		// O(1) push and find_and_remove via hash map instead of O(n) linear scan.
		// Parser-private: only accessed by parser thread
		struct pending_list
		{
			// track_id -> LIFO stack of note pointers awaiting note_off
			std::unordered_map<uint32_t, std::vector<buffered_note*>> stacks;

			void push(uint32_t track_id, buffered_note* ptr)
			{
				stacks[track_id].push_back(ptr);
			}

			// O(1) average: hash lookup then back-pop (LIFO per track_id)
			buffered_note* find_and_remove(uint32_t track_id)
			{
				auto it = stacks.find(track_id);
				if (it == stacks.end() || it->second.empty())
					return nullptr;
				buffered_note* result = it->second.back();
				it->second.pop_back();
				return result;
			}

			void clear() { stacks.clear(); }
		};

		// Per-key SPSC queues for falling notes visualization
		// Producer: parser thread, Consumer: render thread
		std::array<buffered_queue_spsc<buffered_note>, key_count> falling_notes;

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
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_on(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel, uint8_t velocity)
		{
			if (key >= key_count)
				return;

			uint32_t track_id = make_color_id(track_index, channel);

			// Push to falling notes queue (lock-free)
			falling_notes[key].push({time_us, ~0ULL, track_id});

			// Track as pending for note_off matching (parser-private)
			buffered_note* note_ptr = &falling_notes[key].back();
			pending[key].push(track_id, note_ptr);
		}

		// Push a note_off event - finds matching pending note and sets end_time
		// Called from parser thread only - atomic store for end_time_us
		SIMPLE_PLAYER_FORCE_NO_INLINE void push_note_off(uint8_t key, uint64_t time_us, size_t track_index, uint8_t channel)
		{
			if (key >= key_count)
				return;

			uint32_t track_id = make_color_id(track_index, channel);

			// Find matching pending note and set its end time (atomic store)
			buffered_note* note = pending[key].find_and_remove(track_id);
			if (note)
				note->end_time_us.store(time_us, std::memory_order_release);
		}

		// Remove notes that have fully scrolled past (end_time < cutoff)
		// Called from render thread only - no locking needed
		SIMPLE_PLAYER_FORCE_NO_INLINE void cull_expired(int64_t cutoff_time_us)
		{
			for (size_t key = 0; key < key_count; ++key)
			{
				auto& queue = falling_notes[key];
				while (!queue.empty())
				{
					auto& front = queue.front();
					// Only cull if note has ended and end is before cutoff
					// Atomic load for end_time_us
					uint64_t end_time = front.end_time_us.load(std::memory_order_acquire);
					if (end_time != ~0ULL && static_cast<int64_t>(end_time) < cutoff_time_us)
						queue.pop();
					else
						break;  // Queue is ordered by start_time, so stop if this one isn't expired
				}
			}
		}

		// Clear all visual state
		// Should only be called when no concurrent access (e.g., during reset)
		void clear()
		{
			for (auto& q : falling_notes)
				q.clear();
			for (auto& p : pending)
				p.clear();
		}

		// Reset for new playback
		// Acquires access_mutex to synchronize with render thread iteration.
		void reset()
		{
			std::lock_guard<std::mutex> lock(access_mutex);
			clear();
		}
	};

	struct send_event
	{
		uint64_t tick;
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

		// Seek state (written by UI thread, read by playback_thread after join)
		std::atomic<bool> seek_requested{false};
		std::atomic<uint64_t> seek_target_us{0};
		std::atomic<bool> seek_resume_paused{false};

		// Lookahead buffer for pre-parsed MIDI messages (unbounded SPSC - never stalls parser)
		buffered_queue_spsc<send_event> send_buffer;
		std::atomic<bool> seeking_ff{false};        // parser is fast-forwarding (sender drains immediately)
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
			seek_requested = false;
			seek_target_us = 0;
			seek_resume_paused = false;
			seeking_ff = false;
			parser_done = false;
			parsed_up_to_us = 0;
			sender_position_us = 0;
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
			names.emplace_back(wname.begin(), wname.end());
		}

		return names;
	}

	// Get currently selected device index
	size_t get_current_device() const
	{
		return current_device;
	}

	// Change the current device
	void set_device(size_t device_index)
	{
		if (device_index >= devices.size() || (device_index == current_device && short_msg))
			return;

		// Close current device if open
		close_midi_out();

		// Open new device
		current_device = device_index;
		init_midi_out(device_index);

		// Call callback if set
		if (on_device_changed)
			on_device_changed(device_index);
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
			
			set_device(i);
			
			return true;
		}

		return false;
	}

	// Callback for when device changes (for UI updates)
	void(*on_device_changed)(size_t device_index) = nullptr;

	void simple_run(std::wstring filename)
	{
		auto res = open(filename);
		info.open_complete = true;

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

				// Signal sender to transition from immediate drain to timed playback
				state.seeking_ff.store(false, std::memory_order_release);

				if (pause_after_seek)
				{
					state.pause_position_us.store(skip_to_us, std::memory_order_release);
					state.paused.store(true, std::memory_order_release);
				}
			}

			if (!fast_forwarding)
			{
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
			}

			// process ALL events at this tick
			while (!event_queue.empty() && event_queue.top().tick == batch_tick)
			{
				if (state.stop_requested)
					break;

				auto ref = event_queue.top();
				// pop deferred: see replace_top/pop at bottom of loop body

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
					event_queue.pop();
					continue;
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
							state.visuals.push_note_off(data1, batch_time_us, ref.track_index, channel);
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
							event_queue.pop();
							continue;
						}

						// skip meta/sysex data (tempo already handled during initial parsing)
						auto length = get_vlv(track.position, track.end);
						track.position += length;
						break;
					}
				}

				// Push message to lookahead buffer (unbounded - never stalls)
				if (msg_to_send != 0)
					state.send_buffer.push({batch_tick, batch_time_us, msg_to_send});

				// Re-schedule track or remove from heap.
				// replace_top = one sift-down vs pop+push = O(log n) instead of O(2 log n).
				if (track.position < track.end && !track.done)
				{
					auto delta = get_vlv(track.position, track.end);
					track.next_event_tick = batch_tick + delta;
					event_queue.replace_top({track.next_event_tick, ref.track_index});
				}
				else
				{
					if (!track.done)
					{
						track.done = true;
						--state.active_tracks;
					}
					event_queue.pop();
				}
			}
		}

		state.parser_done.store(true, std::memory_order_release);
	}

	// Sender thread: sends pre-parsed events with precise timing
	// During seek fast-forward (seeking_ff == true), drains buffer immediately
	SIMPLE_PLAYER_FORCE_NO_INLINE void sender_thread_func()
	{
		while (!state.stop_requested)
		{
			// Fast-forward mode: drain buffer immediately, no timing
			if (state.seeking_ff.load(std::memory_order_acquire))
			{
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

				// Resume: recalculate timing based on where we paused
				// start_offset_us was updated by resume(), start_time was reset
				// so we just continue - next event will be timed relative to new start
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
			uint64_t target_us = ev.time_us - state.start_offset_us;

			// update current position for UI and parser throttling
			state.current_time_us = ev.time_us;
			state.current_tick = ev.tick;
			state.sender_position_us.store(ev.time_us, std::memory_order_release);

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
			if (short_msg && ev.short_msg != 0) [[likely]]
				short_msg(ev.short_msg);

			state.send_buffer.pop();
		}
	}

	void playback_thread()
	{
		state.reset();
		state.start_time = std::chrono::steady_clock::now();
		state.playing.store(true, std::memory_order_release);

		// Start paused by default so user can select synth first
		state.paused.store(true, std::memory_order_release);
		state.pause_position_us.store(0, std::memory_order_release);

		uint64_t skip_to_us = 0;
		bool pause_after_seek = false;

		for (;;)
		{
			// Reset per-iteration state
			state.stop_requested.store(false, std::memory_order_relaxed);
			state.seeking_ff.store(false, std::memory_order_relaxed);
			state.parser_done.store(false, std::memory_order_relaxed);
			state.parsed_up_to_us.store(0, std::memory_order_relaxed);
			state.sender_position_us.store(0, std::memory_order_relaxed);
			state.send_buffer.clear();
			state.visuals.reset();
			state.track_states.clear();

			// Launch parser and sender threads
			std::thread parser_thread([this, skip_to_us, pause_after_seek]() {
				parser_thread_func(skip_to_us, pause_after_seek);
			});
			std::thread sender_thread([this]() { sender_thread_func(); });

			parser_thread.join();
			sender_thread.join();

			// Check if this was a seek (threads stopped due to seek_requested)
			if (state.seek_requested.load(std::memory_order_acquire))
			{
				skip_to_us = state.seek_target_us.load(std::memory_order_relaxed);
				pause_after_seek = state.seek_resume_paused.load(std::memory_order_relaxed);

				state.seek_requested.store(false, std::memory_order_relaxed);
				state.paused.store(false, std::memory_order_relaxed);

				all_notes_off();
				continue;
			}

			// Normal exit
			break;
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

		bool enable_simulated_lag = true;

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
			const float general_connector_width = width / 128.25f;

			quad_geometry* first = keyboard, *last = keyboard + 127;

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
				}
				else
				{
					key_n[last - keyboard] = key;

					last->bl.x = last->tl.x = key * general_connector_width - black_margins;
					last->br.x = last->tr.x = (key + 1) * general_connector_width + black_margins;

					last->bl.y = last->br.y = -0.5 * height - black_hight;
					last->tl.y = last->tr.y = -0.5 * height;

					--last;
				}
			}

			last_keyboard_height = keyboard_height;
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

		void reinit(float new_width, float new_height, float keyboard_height, float black_height, float black_margins)
		{
			width = new_width;
			height = new_height;
			init(keyboard_height, black_height, black_margins);
		}
	};

	SIMPLE_PLAYER_FORCE_NO_INLINE void draw(const draw_data& data)
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

		if (data.enable_simulated_lag)
		{
			uint64_t max = state.parsed_up_to_us.load(std::memory_order_relaxed);

			if (max < current_us + data.scroll_window_us)
				current_us = max - data.scroll_window_us;
		}

		// Draw falling notes
		draw_data::color keyboard_colors[128];
		memset(keyboard_colors, 0xFF, sizeof(draw_data::color) * total_white);
		memset(keyboard_colors + total_white, 0x00, sizeof(draw_data::color) * (128 - total_white));

		{
			// Lock against visuals.reset() which may be called by playback_thread on seek/restart.
			std::lock_guard<std::mutex> visuals_lock(visuals.access_mutex);

			visuals.cull_expired(int64_t(current_us) - data.scroll_window_us);

			for (uint8_t index = 0; index < 128; ++index)
			{
				uint8_t key = data.key_n[index];

				for (auto it = visuals.falling_notes[key].begin();
					it != visuals.falling_notes[key].end(); ++it)
				{
					auto& note = *it;

					// Atomic load for end_time_us (may be updated by parser)
					uint64_t end_time = note.end_time_us.load(std::memory_order_acquire);

					int64_t start_offset = note.start_time_us - current_us;
					int64_t end_offset = end_time - current_us;

					float begin_y = float(start_offset) / float(data.scroll_window_us);
					float end_y = 1;

					if (end_time != ~0ULL)
						end_y = float(end_offset) / float(data.scroll_window_us);

					if (end_y < -0.01f || begin_y > 1.01f)
						continue;

					auto color_value = rotate(0xFF7F008F, note.track_id);

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

					begin_y = data.keyboard->tr.y + data.height * begin_y;
					end_y = data.keyboard->tr.y + data.height * end_y;

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
		}
		
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


		glColor3ub(0, 0, 0);
		glLineWidth(4);
		glBegin(GL_LINES);
		glVertex2f(data.keyboard->tl.x, data.keyboard->tl.y);
		glVertex2f(data.keyboard[total_white - 1].br.x, data.keyboard[total_white - 1].tr.y);
		glEnd();

		glColor3ub(191, 31, 0);
		glLineWidth(2);
		glBegin(GL_LINES);
		glVertex2f(data.keyboard->tl.x, data.keyboard->tl.y);
		glVertex2f(data.keyboard[total_white - 1].tr.x, data.keyboard[total_white - 1].tr.y);
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
			ThrowAlert_Error("Unable to close the MIDI out");
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
				auto readable_name = std::string(name.begin(), name.end());
				ThrowAlert_Error("Unable to open MIDI out '" + readable_name + "'!");
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
			ThrowAlert_Error("midiOutOpen() horribly failed...\n");
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
		data->move(xpos - 0.5f * data->width, ypos);
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
			) * data->width,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * data->height;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void RescaleAndReposition(float new_xpos, float new_ypos, float new_width, float new_height)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		
		float width_factor_change = new_width / data->width;
		constexpr float black_relative_height = 22.5f / 40.f;

		xpos = new_xpos;
		ypos = new_ypos;
		data->reinit(new_width, new_height, data->last_keyboard_height * width_factor_change, data->last_keyboard_height * width_factor_change * black_relative_height, 0.f);
		data->move(new_xpos - 0.5f * data->width, new_ypos);
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