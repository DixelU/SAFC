#include "prepared_midi_event_source.h"

#include "midi_track_scanner.h"

#include <long_uint.h>
#include <memory_mapped_file_reader.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t default_tempo = 500000;

struct compact_event
{
	std::uint64_t tick;
	std::uint32_t short_msg;
	std::uint8_t kind;
	std::uint8_t key;
	std::uint8_t velocity;
	std::uint8_t channel;
};

inline std::uint32_t pack(std::uint8_t status, std::uint8_t d1, std::uint8_t d2 = 0) noexcept
{
	return std::uint32_t(status) | (std::uint32_t(d1) << 8) | (std::uint32_t(d2) << 16);
}

inline std::uint8_t kind_of(std::uint8_t status, std::uint8_t d2) noexcept
{
	const auto family = status >> 4;
	if (family == 0x8 || (family == 0x9 && d2 == 0))
		return static_cast<std::uint8_t>(generated_event::kind::note_off);
	if (family == 0x9)
		return static_cast<std::uint8_t>(generated_event::kind::note_on);
	return static_cast<std::uint8_t>(generated_event::kind::control);
}

// The decoder's position in one track. Copied to a worker, written back only
// when the pipeline generation it was taken from is still current.
struct decode_cursor
{
	const std::uint8_t* position = nullptr;
	const std::uint8_t* end = nullptr;
	std::uint64_t tick = 0;
	std::uint8_t running_status = 0;
	bool exhausted = true;
};

inline std::uint8_t next_byte(const std::uint8_t*& cur, const std::uint8_t* end) noexcept
{
	return cur < end ? *(cur++) : 0;
}

inline std::uint64_t next_vlv(const std::uint8_t*& cur, const std::uint8_t* end) noexcept
{
	std::uint64_t value = 0;
	std::uint8_t byte;
	do
	{
		byte = next_byte(cur, end);
		value = (value << 7) | (byte & 0x7F);
	}
	while ((byte & 0x80) && cur < end);
	return value;
}

// Same grammar as simple_player::parser_thread_func, for one track and at
// most max_events channel events.
void decode_page(decode_cursor& cursor, std::vector<compact_event>& out, std::size_t max_events)
{
	out.clear();
	out.reserve(max_events);
	auto& cur = cursor.position;
	const auto end = cursor.end;

	while (!cursor.exhausted && out.size() < max_events)
	{
		if (cur >= end)
		{
			cursor.exhausted = true;
			break;
		}

		cursor.tick += next_vlv(cur, end);

		std::uint8_t command = next_byte(cur, end);
		std::uint8_t data1 = 0;
		std::uint8_t data2 = 0;

		if (command < 0x80)
		{
			data1 = command;
			command = cursor.running_status;
		}
		else
		{
			if (command < 0xF0)
				cursor.running_status = command;

			if (command < 0xF0 || command == 0xFF)
				data1 = next_byte(cur, end);
			else
				data1 = 0xFF;
		}

		if (command < 0x80)
		{
			cursor.exhausted = true;
			break;
		}

		switch (command >> 4)
		{
			case 0x8: case 0x9:
			case 0xA: case 0xB: case 0xE:
			{
				data2 = next_byte(cur, end);
				out.push_back({cursor.tick, pack(command, data1, data2), kind_of(command, data2),
					data1, data2, static_cast<std::uint8_t>(command & 0x0F)});
				break;
			}
			case 0xC: case 0xD:
			{
				out.push_back({cursor.tick, pack(command, data1),
					static_cast<std::uint8_t>(generated_event::kind::control),
					data1, 0, static_cast<std::uint8_t>(command & 0x0F)});
				break;
			}
			case 0xF:
			{
				if (command == 0xFF)
					cursor.running_status = 0;
				if (command == 0xFF && data1 == 0x2F)
				{
					cursor.exhausted = true;
					break;
				}
				const std::uint64_t length = next_vlv(cur, end);
				if (static_cast<std::uint64_t>(end - cur) < length)
				{
					cursor.exhausted = true;
					break;
				}
				cur += length;
				break;
			}
			default:
				cursor.exhausted = true;
				break;
		}
	}
}

// Tick <-> microsecond conversion over the tempo map, with the same
// sequential fast path as the player's tempo cache.
struct tempo_clock
{
	const std::map<std::uint64_t, std::uint32_t>* tempo = nullptr;
	const std::vector<std::pair<std::uint64_t, std::uint64_t>>* time_map = nullptr;
	std::uint16_t ppq = 480;

	std::size_t index = ~std::size_t{0};
	std::uint64_t base_tick = 0;
	std::uint64_t base_us = 0;
	std::uint32_t current_tempo = default_tempo;
	std::uint64_t next_change = ~std::uint64_t{0};

	void cache(std::size_t at)
	{
		index = at;
		if (at == ~std::size_t{0} || time_map->empty())
		{
			base_tick = 0;
			base_us = 0;
			current_tempo = default_tempo;
			next_change = time_map->empty() ? ~std::uint64_t{0} : (*time_map)[0].first;
			return;
		}
		base_tick = (*time_map)[at].first;
		base_us = (*time_map)[at].second;
		auto found = tempo->find(base_tick);
		current_tempo = found != tempo->end() ? found->second : default_tempo;
		next_change = at + 1 < time_map->size() ? (*time_map)[at + 1].first : ~std::uint64_t{0};
	}

	std::uint64_t to_us(std::uint64_t tick)
	{
		if (tick >= base_tick && tick < next_change)
			return base_us + ((tick - base_tick) * current_tempo) / ppq;

		if (tick >= next_change && index != ~std::size_t{0} && index + 1 < time_map->size())
		{
			const std::size_t next = index + 1;
			const std::uint64_t after = next + 1 < time_map->size() ? (*time_map)[next + 1].first : ~std::uint64_t{0};
			if (tick < after)
			{
				cache(next);
				return base_us + ((tick - base_tick) * current_tempo) / ppq;
			}
		}

		if (time_map->empty())
		{
			cache(~std::size_t{0});
			return (tick * std::uint64_t{default_tempo}) / ppq;
		}
		auto it = std::upper_bound(time_map->begin(), time_map->end(), tick,
			[](std::uint64_t t, const std::pair<std::uint64_t, std::uint64_t>& entry) { return t < entry.first; });
		if (it == time_map->begin())
		{
			cache(~std::size_t{0});
			return (tick * std::uint64_t{default_tempo}) / ppq;
		}
		--it;
		cache(static_cast<std::size_t>(it - time_map->begin()));
		return base_us + ((tick - base_tick) * current_tempo) / ppq;
	}

	std::uint64_t to_tick(std::uint64_t time_us) const
	{
		const std::uint64_t default_tick = (time_us * ppq) / default_tempo;
		if (time_map->empty())
			return default_tick;
		auto it = std::upper_bound(time_map->begin(), time_map->end(), time_us,
			[](std::uint64_t t, const std::pair<std::uint64_t, std::uint64_t>& entry) { return t < entry.second; });
		if (it == time_map->begin())
			return default_tick;
		--it;
		auto found = tempo->find(it->first);
		const std::uint64_t at_tempo = found != tempo->end() ? found->second : default_tempo;
		if (at_tempo == 0)
			return it->first;
		return it->first + ((time_us - it->second) * ppq) / at_tempo;
	}
};

// Min-heap keyed by tick; replace_top() merges pop+push into one sift-down.
struct merge_ref
{
	std::uint64_t tick;
	std::size_t index;
};

struct merge_heap
{
	std::vector<merge_ref> data;

	bool empty() const noexcept { return data.empty(); }
	const merge_ref& top() const noexcept { return data[0]; }

	void push(merge_ref value)
	{
		data.push_back(value);
		std::size_t i = data.size() - 1;
		while (i > 0)
		{
			const std::size_t parent = (i - 1) >> 1;
			if (data[parent].tick <= data[i].tick)
				break;
			std::swap(data[i], data[parent]);
			i = parent;
		}
	}

	void pop()
	{
		data[0] = data.back();
		data.pop_back();
		if (!data.empty())
			sift_down();
	}

	void replace_top(merge_ref value)
	{
		data[0] = value;
		sift_down();
	}

	void clear() noexcept { data.clear(); }

private:
	void sift_down()
	{
		const std::size_t n = data.size();
		std::size_t i = 0;
		for (;;)
		{
			std::size_t smallest = i;
			const std::size_t left = 2 * i + 1;
			const std::size_t right = 2 * i + 2;
			if (left < n && data[left].tick < data[smallest].tick) smallest = left;
			if (right < n && data[right].tick < data[smallest].tick) smallest = right;
			if (smallest == i)
				break;
			std::swap(data[i], data[smallest]);
			i = smallest;
		}
	}
};
} // namespace

struct prepared_midi_event_source::shared
{
	dixelu::memory_mapped_file_reader mapping;
	std::uint16_t ppq = 480;
	std::vector<safc::midi_scanned_track> tracks;
	std::map<std::uint64_t, std::uint32_t> tempo;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> time_map;
	std::uint64_t ticks_length = 0;
	std::uint64_t total_duration_us = 0;
	std::uint64_t event_count = 0;
	std::uint64_t note_count = 0;
	std::size_t checkpoint_count = 0;
};

struct prepared_midi_event_source::impl
{
	struct track_pipeline
	{
		decode_cursor cursor;								// decoder side
		std::deque<std::vector<compact_event>> ready;		// decoded pages (under mutex)
		bool job_pending = false;							// queued or running (under mutex)
		std::vector<compact_event> page;					// consumer side
		std::size_t index = 0;
		std::vector<compact_event> virtual_events;			// seek checkpoint state, by order
		std::size_t virtual_index = 0;
	};

	std::shared_ptr<const shared> data;
	options settings;
	std::size_t page_events = 4096;
	tempo_clock clock;

	std::vector<track_pipeline> tracks;
	merge_heap heap;
	bool started = false;

	std::mutex mutex;
	std::condition_variable work_available;
	std::condition_variable page_ready;
	std::deque<std::size_t> jobs;
	std::vector<std::thread> workers;
	std::uint64_t generation = 0;
	bool stopping = false;

	impl(std::shared_ptr<const shared> shared_data, options opts)
		: data(std::move(shared_data)), settings(opts)
	{
		clock.tempo = &data->tempo;
		clock.time_map = &data->time_map;
		clock.ppq = data->ppq;
		clock.cache(~std::size_t{0});

		tracks.resize(data->tracks.size());

		page_events = settings.page_events;
		if (page_events == 0)
		{
			const std::size_t count = std::max<std::size_t>(1, data->tracks.size());
			page_events = std::clamp<std::size_t>((std::size_t{2} << 20) / count, 512, 32768);
		}

		unsigned threads = settings.decode_threads;
		if (threads == 0)
		{
			const unsigned hardware = std::thread::hardware_concurrency();
			threads = hardware > 2 ? std::min(hardware / 2, 6u) : 1u;
		}
		workers.reserve(threads);
		try
		{
			for (unsigned i = 0; i < threads; ++i)
				workers.emplace_back([this] { worker_loop(); });
		}
		catch (...)
		{
			shutdown();
			throw;
		}
	}

	~impl()
	{
		shutdown();
	}

	void shutdown() noexcept
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
			++generation;
			jobs.clear();
		}
		work_available.notify_all();
		page_ready.notify_all();
		for (auto& worker : workers)
			if (worker.joinable())
				worker.join();
		workers.clear();
	}

	void worker_loop()
	{
		std::unique_lock lock(mutex);
		std::vector<compact_event> page;
		for (;;)
		{
			work_available.wait(lock, [this] { return stopping || !jobs.empty(); });
			if (stopping)
				return;
			const std::size_t index = jobs.front();
			jobs.pop_front();
			const std::uint64_t job_generation = generation;
			decode_cursor cursor = tracks[index].cursor;
			lock.unlock();

			decode_page(cursor, page, page_events);

			lock.lock();
			if (job_generation != generation)
			{
				// The pipeline was reset meanwhile; this page belongs to the past.
				continue;
			}
			auto& track = tracks[index];
			track.cursor = cursor;
			track.ready.push_back(std::move(page));
			page = std::vector<compact_event>();
			if (track.ready.size() < settings.pages_ahead && !cursor.exhausted)
				jobs.push_back(index);
			else
				track.job_pending = false;
			page_ready.notify_all();
		}
	}

	// Takes the next decoded page of a track, decoding on this thread when no
	// worker exists. Returns false once the track is exhausted. Mutex held.
	bool take_page(std::size_t index, std::unique_lock<std::mutex>& lock)
	{
		auto& track = tracks[index];
		for (;;)
		{
			if (!track.ready.empty())
			{
				track.page = std::move(track.ready.front());
				track.ready.pop_front();
				track.index = 0;
				if (!track.job_pending && !track.cursor.exhausted)
				{
					track.job_pending = true;
					jobs.push_back(index);
					work_available.notify_one();
				}
				if (!track.page.empty())
					return true;
				continue;
			}
			if (track.cursor.exhausted)
				return false;
			if (workers.empty())
			{
				decode_cursor cursor = track.cursor;
				std::vector<compact_event> page;
				decode_page(cursor, page, page_events);
				track.cursor = cursor;
				track.ready.push_back(std::move(page));
				continue;
			}
			if (!track.job_pending)
			{
				track.job_pending = true;
				jobs.push_front(index);
				work_available.notify_one();
			}
			else
			{
				// Needed now: let it jump the queue if it is still waiting.
				auto waiting = std::find(jobs.begin(), jobs.end(), index);
				if (waiting != jobs.end() && waiting != jobs.begin())
				{
					jobs.erase(waiting);
					jobs.push_front(index);
				}
			}
			page_ready.wait(lock);
		}
	}

	// Positions every track (at its start, or at its latest checkpoint at or
	// before target_tick) and restarts the pipeline.
	void restart(std::uint64_t target_tick, bool use_checkpoints)
	{
		std::unique_lock lock(mutex);
		++generation;
		jobs.clear();
		heap.clear();
		clock.cache(~std::size_t{0});

		const std::size_t real_count = tracks.size();
		for (std::size_t i = 0; i < real_count; ++i)
		{
			auto& track = tracks[i];
			const auto& scanned = data->tracks[i];
			track.ready.clear();
			track.page.clear();
			track.index = 0;
			track.job_pending = false;
			track.virtual_events.clear();
			track.virtual_index = 0;

			track.cursor = {};
			track.cursor.position = scanned.begin;
			track.cursor.end = scanned.end;
			track.cursor.tick = 0;
			track.cursor.running_status = 0;
			track.cursor.exhausted = scanned.begin >= scanned.end;

			if (use_checkpoints && !scanned.checkpoints.empty())
			{
				auto next = std::upper_bound(scanned.checkpoints.begin(), scanned.checkpoints.end(), target_tick,
					[](std::uint64_t t, const safc::midi_scan_checkpoint& c) { return t < c.tick; });
				if (next != scanned.checkpoints.begin())
				{
					const auto& checkpoint = *std::prev(next);
					track.cursor.position = checkpoint.position;
					track.cursor.tick = checkpoint.tick;
					track.cursor.running_status = checkpoint.running_status;
					track.cursor.exhausted = checkpoint.position >= scanned.end;

					// Snapshot entries at their original ticks, in arrival order:
					// the player's fast-forward then rebuilds channel state and
					// held notes exactly as a replay from tick zero would.
					struct tagged { std::uint32_t order; compact_event event; };
					std::vector<tagged> entries;
					entries.reserve(checkpoint.held.size() + checkpoint.state.size());
					for (const auto& note : checkpoint.held)
						entries.push_back({note.order, {note.start_tick,
							pack(static_cast<std::uint8_t>(0x90 | (note.channel & 0x0F)), note.key, note.velocity),
							static_cast<std::uint8_t>(generated_event::kind::note_on),
							note.key, note.velocity, note.channel}});
					for (const auto& entry : checkpoint.state)
						entries.push_back({entry.order, {entry.tick, entry.message,
							static_cast<std::uint8_t>(generated_event::kind::control),
							static_cast<std::uint8_t>((entry.message >> 8) & 0x7F),
							static_cast<std::uint8_t>((entry.message >> 16) & 0x7F),
							static_cast<std::uint8_t>(entry.message & 0x0F)}});
					std::sort(entries.begin(), entries.end(),
						[](const tagged& a, const tagged& b) { return a.order < b.order; });
					track.virtual_events.reserve(entries.size());
					for (const auto& entry : entries)
						track.virtual_events.push_back(entry.event);
				}
			}

			if (!track.cursor.exhausted)
			{
				track.job_pending = true;
				jobs.push_back(i);
			}
		}
		work_available.notify_all();

		// The heap needs each track's first tick, which means its first page.
		for (std::size_t i = 0; i < real_count; ++i)
		{
			auto& track = tracks[i];
			if (!track.virtual_events.empty())
				heap.push({track.virtual_events.front().tick, real_count + i});
			if (track.cursor.exhausted && track.ready.empty())
				continue;
			if (take_page(i, lock))
				heap.push({track.page.front().tick, i});
		}
		started = true;
	}

	bool next(generated_event& out)
	{
		if (!started)
			restart(0, false);

		const std::size_t real_count = tracks.size();
		while (!heap.empty())
		{
			const merge_ref top = heap.top();
			if (top.index >= real_count)
			{
				auto& track = tracks[top.index - real_count];
				const auto& event = track.virtual_events[track.virtual_index++];
				emit(event, top.index - real_count, out);
				if (track.virtual_index < track.virtual_events.size())
				{
					const std::uint64_t next_tick = track.virtual_events[track.virtual_index].tick;
					if (next_tick != top.tick)
						heap.replace_top({next_tick, top.index});
				}
				else
					heap.pop();
				return true;
			}

			auto& track = tracks[top.index];
			const auto& event = track.page[track.index++];
			emit(event, top.index, out);

			if (track.index < track.page.size())
			{
				const std::uint64_t next_tick = track.page[track.index].tick;
				if (next_tick != top.tick)
					heap.replace_top({next_tick, top.index});
				return true;
			}

			std::unique_lock lock(mutex);
			if (take_page(top.index, lock))
			{
				const std::uint64_t next_tick = track.page.front().tick;
				if (next_tick != top.tick)
					heap.replace_top({next_tick, top.index});
			}
			else
				heap.pop();
			return true;
		}
		return false;
	}

	void emit(const compact_event& event, std::size_t track_index, generated_event& out)
	{
		out.time_us = clock.to_us(event.tick);
		out.short_msg = event.short_msg;
		out.k = static_cast<generated_event::kind>(event.kind);
		out.key = event.key;
		out.velocity = event.velocity;
		out.channel = event.channel;
		out.track_index = static_cast<std::uint16_t>(track_index);
		out.tick = event.tick;
	}
};

std::shared_ptr<prepared_midi_event_source> prepared_midi_event_source::open(
	const std::wstring& filename, progress_callback progress,
	const std::atomic<bool>* cancel_requested, std::string& error, options settings)
{
	auto data = std::make_shared<shared>();
	if (!data->mapping.open(filename) || !data->mapping.is_open())
	{
		error = "Unable to open the MIDI file";
		return nullptr;
	}

	const auto* begin = reinterpret_cast<const std::uint8_t*>(data->mapping.data());
	const auto size = data->mapping.size();
	const auto* end = begin + size;
	if (size < 18 || std::memcmp(begin, "MThd", 4) != 0)
	{
		error = "Not a MIDI file";
		return nullptr;
	}
	data->ppq = static_cast<std::uint16_t>((begin[12] << 8) | begin[13]);
	if (data->ppq == 0)
	{
		error = "Unsupported MIDI time division";
		return nullptr;
	}

	if (progress)
		progress("Scanning MIDI tracks...");

	safc::midi_scan_options scan_options;
	scan_options.threads = settings.scan_threads;
	scan_options.cancel = cancel_requested;
	if (settings.checkpoint_bytes)
	{
		scan_options.min_checkpoint_bytes = settings.checkpoint_bytes;
		scan_options.max_checkpoint_bytes = settings.checkpoint_bytes;
	}
	auto scan = safc::scan_midi_tracks(begin + 14, end, scan_options);
	if (scan.cancelled)
	{
		error = "Cancelled";
		return nullptr;
	}
	if (scan.resume_at != nullptr)
	{
		error = "Track layout could not be verified; the serial parser is needed for this file";
		return nullptr;
	}
	if (scan.tracks.empty())
	{
		error = "No tracks found";
		return nullptr;
	}

	data->tracks = std::move(scan.tracks);
	for (auto& track : data->tracks)
	{
		for (const auto& [tick, tempo] : track.tempos)
			data->tempo[tick] = tempo;
		data->ticks_length = std::max(data->ticks_length, track.length_ticks);
		data->event_count += track.event_count;
		data->note_count += track.note_on_count;
		data->checkpoint_count += track.checkpoints.size();
		track.tempos.clear();
		track.tempos.shrink_to_fit();
	}

	// Same cumulative time map the player builds in open().
	dixelu::long_uint<0> numerator{0};
	std::uint64_t previous_tick = 0;
	std::uint32_t previous_tempo = default_tempo;
	const dixelu::long_uint<0> denominator{static_cast<std::uint64_t>(data->ppq)};
	for (const auto& [tick, tempo] : data->tempo)
	{
		numerator += dixelu::long_uint<0>{tick - previous_tick} * dixelu::long_uint<0>{previous_tempo};
		data->time_map.emplace_back(tick, static_cast<std::uint64_t>(numerator / denominator));
		previous_tick = tick;
		previous_tempo = tempo;
	}
	{
		const std::uint64_t base_us = data->time_map.empty() ? 0 : data->time_map.back().second;
		data->total_duration_us = base_us +
			((data->ticks_length - previous_tick) * previous_tempo) / data->ppq;
	}

	if (progress)
		progress("Prepared " + std::to_string(data->tracks.size()) + " tracks, " +
			std::to_string(data->event_count) + " events");

	return std::shared_ptr<prepared_midi_event_source>(
		new prepared_midi_event_source(std::move(data), settings));
}

prepared_midi_event_source::prepared_midi_event_source(std::shared_ptr<const shared> data, options settings)
	: impl_(std::make_unique<impl>(std::move(data), settings))
{
}

prepared_midi_event_source::~prepared_midi_event_source() = default;

std::uint64_t prepared_midi_event_source::total_duration_us() const
{
	return impl_->data->total_duration_us;
}

void prepared_midi_event_source::rewind()
{
	impl_->restart(0, false);
}

void prepared_midi_event_source::seek(std::uint64_t target_us)
{
	impl_->restart(impl_->clock.to_tick(target_us), true);
}

bool prepared_midi_event_source::next(generated_event& out)
{
	return impl_->next(out);
}

std::uint16_t prepared_midi_event_source::track_count() const
{
	return static_cast<std::uint16_t>(std::min<std::size_t>(impl_->data->tracks.size(), 0xFFFF));
}

std::uint64_t prepared_midi_event_source::event_count() const
{
	return impl_->data->event_count;
}

std::uint64_t prepared_midi_event_source::note_count() const
{
	return impl_->data->note_count;
}

std::size_t prepared_midi_event_source::checkpoint_count() const
{
	return impl_->data->checkpoint_count;
}

std::shared_ptr<prepared_midi_event_source> prepared_midi_event_source::fork_reader() const
{
	return std::shared_ptr<prepared_midi_event_source>(
		new prepared_midi_event_source(impl_->data, impl_->settings));
}
