#pragma once
#ifndef SAFC_MIDI_TRACK_SCANNER
#define SAFC_MIDI_TRACK_SCANNER

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

/**
 * Parallel open-time scan of a Standard MIDI File that is already in memory.
 *
 * The player's open() used to walk every byte of every track on one thread to
 * collect the tempo map and the track bounds. This scanner locates the track
 * chunks from their declared MTrk lengths and walks them concurrently. A chunk
 * whose walk does not end exactly at its declared end with an end-of-track meta
 * is reported as not clean: the caller's serial walk then resumes from that
 * chunk's header, so damaged files keep the exact legacy behaviour (size
 * mismatch warnings, resynchronisation on the next MTrk, error alerts).
 *
 * While walking, each track can also record seek checkpoints: the byte position
 * of a delta time, the tick there, the running status, the notes still held and
 * the last value of every program, pitch bend, channel pressure and controller
 * seen so far. A seek starts the fast-forward from the latest checkpoint at or
 * before the target instead of from tick zero.
 */
namespace safc
{

struct midi_scan_checkpoint
{
	struct held_note
	{
		std::uint64_t start_tick;
		std::uint32_t order;	// note-on order within the track
		std::uint8_t key;
		std::uint8_t channel;
		std::uint8_t velocity;
	};

	struct state_entry
	{
		std::uint64_t tick;		// when this value was set
		std::uint32_t order;	// arrival order within the track
		std::uint32_t message;  // packed short message (status | d1 << 8 | d2 << 16)
	};

	std::uint64_t tick = 0;						// tick before the delta time at 'position'
	const std::uint8_t* position = nullptr;		// first byte of that delta time
	std::uint8_t running_status = 0;
	std::vector<held_note> held;				// in note-on order
	std::vector<state_entry> state;				// in arrival order
};

struct midi_scanned_track
{
	const std::uint8_t* header = nullptr;		// the 'M' of this chunk's MTrk
	const std::uint8_t* begin = nullptr;		// first delta time
	const std::uint8_t* end = nullptr;			// declared end of the chunk data
	std::uint64_t length_ticks = 0;
	std::uint64_t event_count = 0;		// channel events (meta and sysex excluded)
	std::uint64_t note_on_count = 0;
	// (tick, microseconds per quarter) in file order; later entries at an equal
	// tick win, exactly like the serial walk's map assignment.
	std::vector<std::pair<std::uint64_t, std::uint32_t>> tempos;
	std::vector<midi_scan_checkpoint> checkpoints;  // ascending tick
	bool clean = false;
};

struct midi_scan_options
{
	unsigned threads = 0;							// 0: hardware concurrency
	bool checkpoints = true;
	// Checkpoints are spaced so a fast-forward replays about 1/64 of the file
	// across all tracks, within these per-track bounds.
	std::uint64_t min_checkpoint_bytes = 256u << 10;
	std::uint64_t max_checkpoint_bytes = 8u << 20;
	const std::atomic<bool>* cancel = nullptr;
	std::atomic<std::uint64_t>* scanned_bytes = nullptr;  // progress: data bytes walked
	// Files below this size are walked on the calling thread.
	std::uint64_t parallel_threshold_bytes = 1u << 20;
};

struct midi_scan_result
{
	// Clean, contiguous tracks in file order. Every track here was verified to
	// end at its declared end with an end-of-track meta.
	std::vector<midi_scanned_track> tracks;
	// Where the serial walk has to continue: the header of the first chunk that
	// was not clean, or nullptr when the whole file was consumed.
	const std::uint8_t* resume_at = nullptr;
	bool cancelled = false;
};

namespace midi_scan_detail
{

inline constexpr std::uint32_t MTrk_header = 0x4D54726Bu;

inline const std::uint8_t* find_track_header(const std::uint8_t* cur, const std::uint8_t* end) noexcept
{
	// Same resynchronisation as the serial walk: byte by byte until "MTrk".
	while (end - cur >= 4)
	{
		const void* found = std::memchr(cur, 'M', static_cast<std::size_t>(end - cur - 3));
		if (!found)
			return nullptr;
		cur = static_cast<const std::uint8_t*>(found);
		if (cur[1] == 'T' && cur[2] == 'r' && cur[3] == 'k')
			return cur;
		++cur;
	}
	return nullptr;
}

// State one worker keeps while walking a single track for checkpoints.
struct track_state_tracker
{
	struct held_entry
	{
		std::uint64_t start_tick;
		std::uint32_t order;
		std::uint8_t velocity;
	};

	static constexpr std::size_t program_slot = 128;
	static constexpr std::size_t bend_slot = 129;
	static constexpr std::size_t pressure_slot = 130;
	static constexpr std::size_t slots_per_channel = 131;

	struct state_slot
	{
		std::uint64_t tick = 0;
		std::uint32_t order = 0;
		std::uint32_t message = 0;
		bool set = false;
	};

	std::vector<std::vector<held_entry>> held;  // [(channel << 7) | key]
	std::vector<state_slot> state;				// [channel * slots_per_channel + slot]
	std::uint32_t next_order = 0;

	track_state_tracker() : held(16 * 128), state(16 * slots_per_channel) {}

	void note_on(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity, std::uint64_t tick)
	{
		held[(std::size_t(channel) << 7) | key].push_back({tick, next_order++, velocity});
	}

	void note_off(std::uint8_t channel, std::uint8_t key)
	{
		auto& stack = held[(std::size_t(channel) << 7) | key];
		if (!stack.empty())
			stack.pop_back();
	}

	void set(std::uint8_t channel, std::size_t slot, std::uint32_t message, std::uint64_t tick)
	{
		auto& entry = state[std::size_t(channel) * slots_per_channel + slot];
		entry.tick = tick;
		entry.order = next_order++;
		entry.message = message;
		entry.set = true;
	}

	void snapshot(midi_scan_checkpoint& out) const
	{
		for (std::size_t index = 0; index < held.size(); ++index)
		{
			const auto& stack = held[index];
			const auto channel = static_cast<std::uint8_t>(index >> 7);
			const auto key = static_cast<std::uint8_t>(index & 0x7F);
			for (const auto& note : stack)
				out.held.push_back({note.start_tick, note.order, key, channel, note.velocity});
		}
		std::sort(out.held.begin(), out.held.end(),
			[](const auto& a, const auto& b) { return a.order < b.order; });

		for (const auto& slot : state)
			if (slot.set)
				out.state.push_back({slot.tick, slot.order, slot.message});
		std::sort(out.state.begin(), out.state.end(),
			[](const auto& a, const auto& b) { return a.order < b.order; });
	}
};

inline std::uint32_t pack_message(std::uint8_t status, std::uint8_t d1, std::uint8_t d2 = 0) noexcept
{
	return std::uint32_t(status) | (std::uint32_t(d1) << 8) | (std::uint32_t(d2) << 16);
}

// Walks one chunk. Mirrors the serial walk's grammar; any deviation from a
// well-formed chunk that ends exactly at its declared end leaves clean false.
inline void walk_track(midi_scanned_track& track, const midi_scan_options& options,
	std::uint64_t checkpoint_bytes, std::atomic<bool>& stop)
{
	const std::uint8_t* cur = track.begin;
	const std::uint8_t* const end = track.end;
	const std::uint8_t* last_progress = cur;
	const std::uint8_t* next_checkpoint = checkpoint_bytes ? cur + checkpoint_bytes : nullptr;

	std::uint64_t tick = 0;
	std::uint8_t rsb = 0;
	bool ended = false;
	bool failed = false;
	std::uint32_t check_counter = 0;

	track_state_tracker tracker;
	const bool record_state = checkpoint_bytes != 0;

	auto report_progress = [&](const std::uint8_t* position)
	{
		if (options.scanned_bytes && position > last_progress)
		{
			options.scanned_bytes->fetch_add(
				static_cast<std::uint64_t>(position - last_progress), std::memory_order_relaxed);
			last_progress = position;
		}
	};

	while (cur < end)
	{
		if ((++check_counter & 4095u) == 0)
		{
			if (stop.load(std::memory_order_relaxed) ||
				(options.cancel && options.cancel->load(std::memory_order_relaxed)))
			{
				failed = true;
				break;
			}
			if (cur - last_progress >= (1 << 20))
				report_progress(cur);
		}

		if (next_checkpoint && cur >= next_checkpoint)
		{
			midi_scan_checkpoint checkpoint;
			checkpoint.tick = tick;
			checkpoint.position = cur;
			checkpoint.running_status = rsb;
			tracker.snapshot(checkpoint);
			track.checkpoints.push_back(std::move(checkpoint));
			next_checkpoint = cur + checkpoint_bytes;
		}

		// delta time
		std::uint64_t delta = 0;
		std::uint8_t byte;
		do
		{
			if (cur >= end) { failed = true; break; }
			byte = *cur++;
			delta = (delta << 7) | (byte & 0x7F);
		}
		while (byte & 0x80);
		if (failed)
			break;
		tick += delta;

		if (cur >= end) { failed = true; break; }
		std::uint8_t command = *cur++;
		std::uint8_t param = 0;

		if (command < 0x80)
		{
			param = command;
			command = rsb;
		}
		else
		{
			if (command < 0xF0)
				rsb = command;

			if (command < 0xF0 || command == 0xFF)
			{
				if (cur >= end) { failed = true; break; }
				param = *cur++;
			}
			else
				param = 0xFF;
		}

		// The serial walk reports a zero running status or a read past the end
		// as an error. Either way this chunk is handed to it.
		if (command < 0x80 || cur >= end)
		{
			failed = true;
			break;
		}

		switch (command >> 4)
		{
			case 0x8: case 0x9:
			case 0xA: case 0xB: case 0xE:
			{
				++track.event_count;
				const std::uint8_t data2 = *cur++;
				const auto channel = static_cast<std::uint8_t>(command & 0x0F);
				const auto kind = static_cast<std::uint8_t>(command >> 4);
				if (kind == 0x9 && data2 != 0)
				{
					++track.note_on_count;
					if (record_state)
						tracker.note_on(channel, param, data2, tick);
				}
				else if (kind == 0x9 || kind == 0x8)
				{
					if (record_state)
						tracker.note_off(channel, param);
				}
				else if (record_state && kind == 0xB)
					tracker.set(channel, param & 0x7F, pack_message(command, param, data2), tick);
				else if (record_state && kind == 0xE)
					tracker.set(channel, track_state_tracker::bend_slot,
						pack_message(command, param, data2), tick);
				break;
			}
			case 0xC: case 0xD:
			{
				++track.event_count;
				if (record_state)
					tracker.set(static_cast<std::uint8_t>(command & 0x0F),
						(command >> 4) == 0xC ? track_state_tracker::program_slot
							: track_state_tracker::pressure_slot,
						pack_message(command, param), tick);
				break;
			}
			case 0xF:
			{
				rsb = 0;

				if (command == 0xFF && param == 0x51)
				{
					// The serial walk assumes the three-byte payload after the
					// length byte; keep its reading so both agree on every file.
					if (end - cur < 4) { failed = true; break; }
					++cur; // length byte
					std::uint32_t value = std::uint32_t(cur[0]) << 16;
					value |= std::uint32_t(cur[1]) << 8;
					value |= std::uint32_t(cur[2]);
					cur += 3;
					track.tempos.emplace_back(tick, value);
					break;
				}

				const bool end_of_track = (command == 0xFF && param == 0x2F);

				std::uint64_t length = 0;
				do
				{
					if (cur >= end) { failed = true; break; }
					byte = *cur++;
					length = (length << 7) | (byte & 0x7F);
				}
				while (byte & 0x80);
				if (failed)
					break;

				if (static_cast<std::uint64_t>(end - cur) < length)
				{
					failed = true;
					break;
				}
				cur += length;

				if (end_of_track)
					ended = true;
				break;
			}
			default:
			{
				failed = true;
				break;
			}
		}

		if (failed || ended)
			break;
	}

	report_progress(cur);
	track.length_ticks = tick;
	track.clean = !failed && ended && cur == end;
	if (!track.clean)
		track.checkpoints.clear();
}

} // namespace midi_scan_detail

/**
 * Locates the track chunks after 'first_track' (the byte after the MThd chunk)
 * and walks them. Returns the clean prefix and where a serial walk must resume.
 */
inline midi_scan_result scan_midi_tracks(const std::uint8_t* first_track,
	const std::uint8_t* file_end, const midi_scan_options& options)
{
	using namespace midi_scan_detail;

	midi_scan_result result;
	std::vector<midi_scanned_track> chunks;

	// Chunk discovery from the declared lengths.
	const std::uint8_t* cur = first_track;
	while (cur < file_end)
	{
		const std::uint8_t* header = find_track_header(cur, file_end);
		if (!header)
		{
			cur = file_end;
			break;
		}
		if (file_end - header < 8)
		{
			// Truncated header: let the serial walk report it.
			result.resume_at = header;
			break;
		}
		const std::uint64_t declared =
			(std::uint64_t(header[4]) << 24) | (std::uint64_t(header[5]) << 16) |
			(std::uint64_t(header[6]) << 8) | std::uint64_t(header[7]);
		const std::uint8_t* data = header + 8;
		if (static_cast<std::uint64_t>(file_end - data) < declared)
		{
			// Overruns the file: the serial walk resynchronises from here.
			result.resume_at = header;
			break;
		}
		midi_scanned_track chunk;
		chunk.header = header;
		chunk.begin = data;
		chunk.end = data + declared;
		chunks.push_back(chunk);
		cur = chunk.end;
	}

	if (chunks.empty())
		return result;

	std::uint64_t total_bytes = 0;
	for (const auto& chunk : chunks)
		total_bytes += static_cast<std::uint64_t>(chunk.end - chunk.begin);

	std::uint64_t checkpoint_bytes = 0;
	if (options.checkpoints)
	{
		checkpoint_bytes = total_bytes / (chunks.size() * 64);
		checkpoint_bytes = std::clamp(checkpoint_bytes,
			options.min_checkpoint_bytes, options.max_checkpoint_bytes);
	}

	std::atomic<bool> stop{false};
	std::exception_ptr failure;
	std::atomic<std::size_t> next_chunk{0};

	// Largest chunks first, so one giant track cannot trail the whole pool.
	std::vector<std::size_t> order(chunks.size());
	std::iota(order.begin(), order.end(), std::size_t{0});
	std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
	{
		return (chunks[a].end - chunks[a].begin) > (chunks[b].end - chunks[b].begin);
	});

	unsigned threads = options.threads ? options.threads : std::thread::hardware_concurrency();
	if (threads == 0)
		threads = 1;
	threads = static_cast<unsigned>(std::min<std::size_t>(threads, chunks.size()));
	if (total_bytes < options.parallel_threshold_bytes)
		threads = 1;

	std::mutex failure_mutex;
	auto worker = [&]()
	{
		try
		{
			for (std::size_t index; (index = next_chunk.fetch_add(1, std::memory_order_relaxed)) < order.size();)
			{
				if (stop.load(std::memory_order_relaxed))
					break;
				walk_track(chunks[order[index]], options, checkpoint_bytes, stop);
			}
		}
		catch (...)
		{
			std::lock_guard lock(failure_mutex);
			if (!failure)
				failure = std::current_exception();
			stop.store(true, std::memory_order_relaxed);
		}
	};

	if (threads <= 1)
		worker();
	else
	{
		std::vector<std::thread> pool;
		pool.reserve(threads - 1);
		try
		{
			for (unsigned i = 1; i < threads; ++i)
				pool.emplace_back(worker);
		}
		catch (...)
		{
			stop.store(true, std::memory_order_relaxed);
			for (auto& thread : pool)
				thread.join();
			throw;
		}
		worker();
		for (auto& thread : pool)
			thread.join();
	}

	if (failure)
		std::rethrow_exception(failure);

	if (options.cancel && options.cancel->load(std::memory_order_acquire))
	{
		result.cancelled = true;
		return result;
	}

	// Clean prefix in file order; the serial walk takes over at the first
	// chunk that is not clean (or where discovery stopped).
	std::size_t clean_count = 0;
	while (clean_count < chunks.size() && chunks[clean_count].clean)
		++clean_count;
	if (clean_count < chunks.size())
		result.resume_at = chunks[clean_count].header;

	result.tracks.reserve(clean_count);
	for (std::size_t i = 0; i < clean_count; ++i)
		result.tracks.push_back(std::move(chunks[i]));
	return result;
}

} // namespace safc

#endif // SAFC_MIDI_TRACK_SCANNER
