#pragma once
#ifndef SAFC_PLAYER_VISUALS
#define SAFC_PLAYER_VISUALS

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include <buffered_queue_spsc.h>

#ifndef SIMPLE_PLAYER_FORCE_NO_INLINE
#define SIMPLE_PLAYER_FORCE_NO_INLINE
#endif

// The falling-notes store of simple_player, kept free of Windows and OpenGL so
// its behaviour can be tested anywhere. simple_player::visuals_viewport is an
// alias of this type.
// Visuals viewport: manages notes display and keyboard state
// lock-free SPSC: parser thread pushes notes, render thread reads state
//
// Notes live in two rings (white keys, black keys) in note-on order, which
// is start-time order because the parser emits events in time order. The
// renderer mirrors each ring into a GPU buffer, so a frame only uploads the
// notes that arrived or ended since the last one; per-frame CPU work no
// longer grows with the number of notes on screen.
struct simple_player_visuals
{
	static constexpr size_t key_count = 128;
	static constexpr uint64_t held_end = ~0ULL;	// end_time_us of a note awaiting its note_off
	static constexpr uint64_t no_note = ~0ULL;	// "no sequence number"

	struct queued_visual_event
	{
		uint64_t time_us;
		uint32_t track_id;
		uint8_t key;
		bool note_on;
	};

	// One displayed note. Owned by the visual consumer after queued events
	// cross the parser/render boundary.
	struct note_record
	{
		uint64_t start_time_us;		// note on time (immutable after creation)
		uint64_t end_time_us;		// note off time (held_end = still held)
		uint32_t track_id;			// (track_index << 4) | channel
		uint8_t key;
		uint8_t dead;				// scrolled past the cull cutoff
	};

	static constexpr size_t ring_of_key(uint8_t key)
	{
		const uint8_t note = key % 12;
		const bool white = note < 5 ? !(note & 1) : (note & 1);
		return white ? 0 : 1;
	}

	// Sequence-numbered circular buffer of note records. A record keeps its
	// sequence number for its whole life; slots move only when the buffer
	// grows, is compacted or cleared, which bumps layout_generation so a
	// GPU mirror re-uploads everything once.
	struct note_ring
	{
		static constexpr size_t page_notes = 256;		// GPU upload granularity
		static constexpr size_t min_capacity = 1024;
		static constexpr size_t compaction_floor = 4096;

		std::vector<note_record> records;	// capacity = records.size(), power of two
		uint64_t tail_seq = 0;				// oldest retained record
		uint64_t head_seq = 0;				// one past the newest
		size_t dead_count = 0;
		uint32_t layout_generation = 1;

		// Pages touched since the GPU mirror last synchronised.
		std::vector<uint8_t> page_dirty;
		std::vector<uint32_t> dirty_pages;

		// Notes with a known end in end order (parser time is monotonic),
		// awaiting the cull cutoff.
		std::vector<std::pair<uint64_t, uint64_t>> ended;	// (end_time_us, seq)
		size_t ended_head = 0;

		// Key-press tracking admits records in start order as time passes.
		uint64_t admit_seq = 0;

		size_t capacity() const noexcept { return records.size(); }
		size_t mask() const noexcept { return records.size() - 1; }
		size_t range() const noexcept { return static_cast<size_t>(head_seq - tail_seq); }
		size_t live() const noexcept { return range() - dead_count; }
		size_t slot_of(uint64_t seq) const noexcept { return static_cast<size_t>(seq & mask()); }
		note_record& at(uint64_t seq) noexcept { return records[slot_of(seq)]; }
		const note_record& at(uint64_t seq) const noexcept { return records[slot_of(seq)]; }
		bool contains(uint64_t seq) const noexcept { return seq >= tail_seq && seq < head_seq; }

		void clear()
		{
			std::vector<note_record>().swap(records);
			tail_seq = head_seq = 0;
			dead_count = 0;
			std::vector<uint8_t>().swap(page_dirty);
			std::vector<uint32_t>().swap(dirty_pages);
			std::vector<std::pair<uint64_t, uint64_t>>().swap(ended);
			ended_head = 0;
			admit_seq = 0;
			++layout_generation;
		}

		void mark_dirty(uint64_t seq)
		{
			const auto page = static_cast<uint32_t>(slot_of(seq) / page_notes);
			if (!page_dirty[page])
			{
				page_dirty[page] = 1;
				dirty_pages.push_back(page);
			}
		}

		void clear_dirty() noexcept
		{
			for (auto page : dirty_pages)
				page_dirty[page] = 0;
			dirty_pages.clear();
		}

		uint64_t push(const note_record& record)
		{
			if (range() == capacity())
				grow();
			const uint64_t seq = head_seq;
			at(seq) = record;
			++head_seq;
			mark_dirty(seq);
			return seq;
		}

		// Undo the latest push (allocation failure after it).
		void pop_back() noexcept
		{
			if (head_seq != tail_seq)
				--head_seq;
		}

		void end_note(uint64_t seq, uint64_t time_us)
		{
			at(seq).end_time_us = time_us;
			mark_dirty(seq);
			ended.emplace_back(time_us, seq);
		}

		void grow()
		{
			const size_t next_capacity = records.empty() ? min_capacity : records.size() * 2;
			std::vector<note_record> next(next_capacity);
			const size_t next_mask = next_capacity - 1;
			for (uint64_t seq = tail_seq; seq != head_seq; ++seq)
				next[static_cast<size_t>(seq & next_mask)] = at(seq);
			records.swap(next);
			page_dirty.assign(next_capacity / page_notes, 0);
			dirty_pages.clear();
			++layout_generation;
		}

		// Marks notes that scrolled past the cutoff dead and releases them
		// from the tail. A long held note cannot pin what came after it:
		// once the dead dominate the retained range, wants_compaction().
		void cull(uint64_t cutoff)
		{
			while (ended_head < ended.size() && ended[ended_head].first < cutoff)
			{
				const uint64_t seq = ended[ended_head].second;
				if (contains(seq))
				{
					auto& record = at(seq);
					if (!record.dead)
					{
						record.dead = 1;
						++dead_count;
					}
				}
				++ended_head;
			}
			if (ended_head >= 4096 && ended_head * 2 >= ended.size())
			{
				ended.erase(ended.begin(), ended.begin() + static_cast<std::ptrdiff_t>(ended_head));
				ended_head = 0;
			}
			while (tail_seq != head_seq && at(tail_seq).dead)
			{
				++tail_seq;
				--dead_count;
			}
			if (tail_seq == head_seq && !records.empty())
			{
				// Nothing retained: release the storage (a dense passage may
				// have grown it) and restart the sequence numbers.
				clear();
			}
		}

		bool wants_compaction() const noexcept
		{
			return dead_count >= compaction_floor && dead_count * 2 > range();
		}

		// Rewrites the live records contiguously from sequence 0. The owner
		// rebuilds the structures that referenced sequence numbers.
		void compact()
		{
			const size_t live_count = live();
			size_t next_capacity = min_capacity;
			while (next_capacity < live_count + live_count / 2 + 1)
				next_capacity *= 2;
			std::vector<note_record> next(next_capacity);
			const size_t next_mask = next_capacity - 1;
			uint64_t next_seq = 0;
			for (uint64_t seq = tail_seq; seq != head_seq; ++seq)
			{
				const auto& record = at(seq);
				if (record.dead)
					continue;
				next[static_cast<size_t>(next_seq & next_mask)] = record;
				++next_seq;
			}
			records.swap(next);
			tail_seq = 0;
			head_seq = next_seq;
			dead_count = 0;

			std::vector<std::pair<uint64_t, uint64_t>> next_ended;
			next_ended.reserve(live_count);
			for (uint64_t seq = 0; seq != head_seq; ++seq)
				if (at(seq).end_time_us != held_end)
					next_ended.emplace_back(at(seq).end_time_us, seq);
			std::stable_sort(next_ended.begin(), next_ended.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });
			ended.swap(next_ended);
			ended_head = 0;

			page_dirty.assign(next_capacity / page_notes, 0);
			dirty_pages.clear();
			admit_seq = 0;
			++layout_generation;
		}
	};

	// Per-track_id LIFO stacks of pending notes per key (sequence numbers).
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
			std::vector<uint64_t> stack; // LIFO of notes awaiting note_off
		};

		std::vector<slot> slots; // power-of-two size
		size_t used = 0;		 // occupied slots (including emptied stacks)

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

		void push(uint32_t track_id, uint64_t seq)
		{
			if (used * 4 >= slots.size() * 3)
				grow();

			slot* s = find_slot(track_id);
			if (s->track_id == empty_id)
			{
				s->track_id = track_id;
				++used;
			}
			s->stack.push_back(seq);
		}

		uint64_t find_and_remove(uint32_t track_id)
		{
			if (slots.empty())
				return no_note;

			slot* s = find_slot(track_id);
			if (s->track_id == empty_id || s->stack.empty())
				return no_note;

			const uint64_t result = s->stack.back();
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
	// consumer drains them into the rings, so ring mutation, pending
	// matching, reclamation and GPU mirroring all stay on one thread.
	dixelu::buffered_queue_spsc<queued_visual_event> queued_events;
	std::array<note_ring, 2> rings;	// [0] white keys, [1] black keys

	// Per-key pending note trackers for note_off matching
	// Visual-consumer-private
	std::array<pending_list, key_count> pending;

	// Key-press tracking. Each key keeps the admitted notes (start <= now)
	// as a stack with increasing start and decreasing end from bottom to
	// top: the top that has not ended yet is the latest-started note under
	// the keyboard, the same note the span scan used to find.
	std::array<std::vector<uint64_t>, key_count> key_stacks;
	int64_t key_state_time_us = std::numeric_limits<int64_t>::min();

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

	note_ring& ring_for(uint8_t key) noexcept { return rings[ring_of_key(key)]; }
	const note_ring& ring_for(uint8_t key) const noexcept { return rings[ring_of_key(key)]; }

	void apply_visual_event(const queued_visual_event& event)
	{
		auto& ring = ring_for(event.key);
		if (event.note_on)
		{
			const uint64_t seq = ring.push({event.time_us, held_end, event.track_id, event.key, 0});
			try
			{
				pending[event.key].push(event.track_id, seq);
			}
			catch (...)
			{
				ring.pop_back();
				throw;
			}
			return;
		}

		const uint64_t seq = pending[event.key].find_and_remove(event.track_id);
		if (seq != no_note)
			ring.end_note(seq, event.time_us);
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

	// Rebuild the pending stacks of one ring from its records (after compaction).
	void rebuild_pending(size_t ring_index)
	{
		for (size_t key = 0; key < key_count; ++key)
			if (ring_of_key(static_cast<uint8_t>(key)) == ring_index)
				pending[key].clear();
		auto& ring = rings[ring_index];
		for (uint64_t seq = ring.tail_seq; seq != ring.head_seq; ++seq)
		{
			const auto& record = ring.at(seq);
			if (record.end_time_us == held_end)
				pending[record.key].push(record.track_id, seq);
		}
	}

	void reset_key_state()
	{
		for (auto& stack : key_stacks)
			stack.clear();
		for (auto& ring : rings)
			ring.admit_seq = ring.tail_seq;
		key_state_time_us = std::numeric_limits<int64_t>::min();
	}

	// Remove every note that has fully scrolled past, including expired notes
	// behind an older note that is still held.
	// Called from the visual consumer only.
	SIMPLE_PLAYER_FORCE_NO_INLINE void cull_expired(int64_t cutoff_time_us)
	{
		drain_queued_events();
		if (cutoff_time_us <= 0)
			return;

		const uint64_t cutoff = static_cast<uint64_t>(cutoff_time_us);
		for (size_t index = 0; index < rings.size(); ++index)
		{
			auto& ring = rings[index];
			const auto layout_before = ring.layout_generation;
			ring.cull(cutoff);
			if (ring.wants_compaction())
				ring.compact();
			// Compaction and release restart the sequence numbers: rebuild
			// everything that referenced them.
			if (ring.layout_generation != layout_before)
			{
				rebuild_pending(index);
				reset_key_state();
			}
		}
	}

	// Admit notes that started by now into the key stacks. Time moving
	// backwards (a restart without reset) rebuilds the stacks.
	SIMPLE_PLAYER_FORCE_NO_INLINE void advance_key_state(int64_t now_us)
	{
		if (now_us < key_state_time_us)
			reset_key_state();
		key_state_time_us = now_us;

		for (auto& ring : rings)
		{
			if (ring.admit_seq < ring.tail_seq)
				ring.admit_seq = ring.tail_seq;
			while (ring.admit_seq < ring.head_seq)
			{
				const auto& record = ring.at(ring.admit_seq);
				if (static_cast<int64_t>(record.start_time_us) > now_us)
					break;
				if (!record.dead)
				{
					auto& stack = key_stacks[record.key];
					// A later note with a known end shadows every top that ends
					// no later. A note whose note_off has not arrived yet shadows
					// nothing: its end may turn out earlier than the notes below
					// it, which must then show again.
					if (record.end_time_us != held_end)
					{
						while (!stack.empty())
						{
							const uint64_t top = stack.back();
							const uint64_t top_end = ring.contains(top) ? ring.at(top).end_time_us : 0;
							if (top_end <= record.end_time_us)
								stack.pop_back();
							else
								break;
						}
					}
					stack.push_back(ring.admit_seq);
				}
				++ring.admit_seq;
			}
		}
	}

	// The latest-started note on 'key' that is sounding at now_us.
	bool pressed_track_id(uint8_t key, int64_t now_us, uint32_t& track_id)
	{
		auto& stack = key_stacks[key];
		const auto& ring = ring_for(key);
		const uint64_t now = now_us < 0 ? 0 : static_cast<uint64_t>(now_us);
		while (!stack.empty())
		{
			const uint64_t seq = stack.back();
			if (!ring.contains(seq))
			{
				stack.pop_back();
				continue;
			}
			const auto& record = ring.at(seq);
			if (record.dead || (record.end_time_us != held_end && record.end_time_us < now))
			{
				stack.pop_back();
				continue;
			}
			track_id = record.track_id;
			return true;
		}
		return false;
	}

	// Clear all visual state
	// Should only be called when no concurrent access (e.g., during reset)
	void clear()
	{
		queued_events.clear();
		for (auto& p : pending)
			p.clear();
		for (auto& ring : rings)
			ring.clear();
		for (auto& stack : key_stacks)
			stack.clear();
		key_state_time_us = std::numeric_limits<int64_t>::min();
	}

	// reset for new playback
	// Acquires access_mutex to synchronize with render thread iteration.
	void reset()
	{
		std::lock_guard<std::mutex> lock(access_mutex);
		clear();
	}

	// Notes still displayed (not yet scrolled past the last cull cutoff).
	std::size_t buffered_note_count()
	{
		std::lock_guard<std::mutex> lock(access_mutex);
		drain_queued_events();
		return rings[0].live() + rings[1].live();
	}

	// Records retained in the rings, including expired notes awaiting compaction.
	std::size_t retained_note_count() const
	{
		std::lock_guard<std::mutex> lock(access_mutex);
		return rings[0].range() + rings[1].range();
	}

	// Live notes on one key in start order (inspection; walks the key's ring).
	std::vector<note_record> notes_for_key(uint8_t key)
	{
		std::lock_guard<std::mutex> lock(access_mutex);
		drain_queued_events();
		std::vector<note_record> result;
		const auto& ring = ring_for(key);
		for (uint64_t seq = ring.tail_seq; seq != ring.head_seq; ++seq)
		{
			const auto& record = ring.at(seq);
			if (!record.dead && record.key == key)
				result.push_back(record);
		}
		return result;
	}
};

#endif // SAFC_PLAYER_VISUALS
