#pragma once

#ifndef SAF_MIDI_TRACK_COLLAPSE_HANDLER
#define SAF_MIDI_TRACK_COLLAPSE_HANDLER

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <memory_mapped_file_reader.h>

#include "midi_file_reader.h"

// Post-processing stage behind "Collapse tracks": merges the tracks of an
// already processed MIDI by tick into a single track, or into one track per
// channel. Tracks are read from a mapping of the file, so memory use does not
// grow with it, and the only disk use is the collapsed output.
struct midi_track_collapse_handler
{
	// Payload of one MTrk chunk, without its 8-byte header
	struct track_extent
	{
		std::uint64_t offset;
		std::uint64_t size;
	};

	struct options
	{
		// One output track per channel instead of a single one
		bool channel_split = false;
		// Running-status compression of the output
		bool running_status = false;
		// Deltas wider than a 4-byte VLV are split with dummy events
		bool delta_overflow_correction = true;
		bool remove_empty_tracks = false;
		// Longer output continues in another track. Notes held across the edge
		// are closed and reopened there, as the in-place merge does.
		std::uint64_t max_track_size = 0xFFFFFFFFull;
		const std::atomic_bool* cancel = nullptr;
		// Bytes of input track data read so far, out of the total
		std::function<void(std::uint64_t, std::uint64_t)> progress;
	};

	// Read-ahead of all tracks together, unless the minimum per track exceeds
	// it. Long reads for few tracks keep hard drives from seeking per page.
	static constexpr std::size_t read_ahead_budget = 128ull << 20;
	static constexpr std::size_t min_read_ahead = 64ull << 10;
	static constexpr std::size_t max_read_ahead = 8ull << 20;

	// Extents of the MTrk chunks of a file, as declared by their headers
	static std::vector<track_extent> scan_tracks(const std::filesystem::path& path)
	{
		dixelu::memory_mapped_file_reader input(path);
		if (!input.is_open() || input.size() < 14)
			throw std::runtime_error("Cannot read MIDI for collapsing");

		const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
		std::vector<track_extent> tracks;
		std::uint64_t position = 14;

		while (position + 8 <= input.size())
		{
			const auto* chunk = data + position;
			const std::uint32_t id = read_u32(chunk);
			const std::uint32_t size = read_u32(chunk + 4);

			const auto payload = (std::min)(std::uint64_t{size}, input.size() - (position + 8));
			if (id == mtrk_id)
				tracks.push_back({position + 8, payload});

			position += 8 + payload;
		}

		return tracks;
	}

	// Writes the header of input, with the new track count, followed by the
	// collapsed tracks. Returns the number of tracks written.
	static std::uint64_t collapse(
		const std::filesystem::path& input,
		const std::vector<track_extent>& tracks,
		const std::filesystem::path& output,
		const options& settings)
	{
		collapse_job job(input, tracks, output, settings);
		return job.run();
	}

	// Collapses next to path, then replaces path with the result
	static std::uint64_t collapse_in_place(
		const std::filesystem::path& path,
		const std::vector<track_extent>& tracks,
		const options& settings)
	{
		auto collapsed = path;
		collapsed += L".collapsed.tmp";
		try
		{
			// The mapping of path is closed once collapse() returns
			const auto track_count = collapse(path, tracks, collapsed, settings);
			replace_file(collapsed, path);
			return track_count;
		}
		catch (...)
		{
			std::error_code ignored;
			std::filesystem::remove(collapsed, ignored);
			throw;
		}
	}

private:
	static constexpr std::uint32_t mtrk_id = 0x4D54726B;
	static constexpr std::uint64_t standard_delta_limit = (1ull << 28) - 1;
	static constexpr std::size_t write_buffer_size = 1ull << 20;
	static constexpr std::size_t page_size = 4096;
	static constexpr std::uint8_t end_of_track[] = {0x00, 0xFF, 0x2F, 0x00};
	static constexpr std::uint8_t dummy_event[] = {0xFF, 0x7F, 0x01, 0x00};

	static std::uint32_t read_u32(const std::uint8_t* bytes)
	{
		return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) |
			(std::uint32_t{bytes[2]} << 8) | std::uint32_t{bytes[3]};
	}

	static void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination)
	{
#ifdef _WIN32
		if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
				"Cannot replace the processed MIDI with its collapsed tracks");
#else
		std::filesystem::rename(source, destination);
#endif
	}

	struct track_cursor
	{
		const std::uint8_t* begin = nullptr;
		const std::uint8_t* position = nullptr;
		const std::uint8_t* end = nullptr;

		// Read-ahead reaches this far; pages before released left the working set
		const std::uint8_t* read_ahead = nullptr;
		const std::uint8_t* released = nullptr;
		const std::uint8_t* next_read_ahead = nullptr;

		std::uint64_t tick = 0;
		std::uint8_t running_status = 0;

		// The current event. A meta or SysEx payload stays unread until copied out.
		std::uint8_t status = 0;
		std::uint8_t meta_type = 0;
		std::uint8_t data[2]{};
		std::uint64_t payload = 0;
	};

	// One output track (or several, past max_track_size) written as MTrk chunks
	// with backpatched sizes, either into the output or into a temporary file.
	struct track_sink
	{
		std::ostream* out = nullptr;
		std::unique_ptr<std::ofstream> temporary;
		std::filesystem::path temporary_path;

		std::vector<std::uint8_t> pending;
		std::uint64_t stream_offset = 0; // of the first pending byte
		std::uint64_t chunk_start = 0;
		std::uint64_t chunk_size = 0;
		std::uint64_t chunk_tick = 0;
		bool chunk_open = false;
		std::uint64_t chunks = 0;
		std::uint8_t running_status = 0;

		// Notes on, by channel and key, to close and reopen at a track edge
		std::array<std::uint32_t, 16 * 128> held{};
		std::uint64_t held_total = 0;

		~track_sink()
		{
			if (!temporary)
				return;
			temporary.reset();
			std::error_code ignored;
			std::filesystem::remove(temporary_path, ignored);
		}

		void put(std::uint8_t byte)
		{
			pending.push_back(byte);
			++chunk_size;
		}

		void put(const std::uint8_t* bytes, std::size_t count)
		{
			pending.insert(pending.end(), bytes, bytes + count);
			chunk_size += count;
		}

		void put_vlv(std::uint64_t value)
		{
			std::uint8_t stack[10];
			std::size_t size = 0;
			do
			{
				stack[size++] = static_cast<std::uint8_t>(value & 0x7F);
				value >>= 7;
			}
			while (value);
			while (size > 1)
				put(stack[--size] | 0x80);
			put(stack[0]);
		}

		void flush()
		{
			if (pending.empty())
				return;
			out->write(reinterpret_cast<const char*>(pending.data()), static_cast<std::streamsize>(pending.size()));
			stream_offset += pending.size();
			pending.clear();
		}

		void flush_if_full()
		{
			if (pending.size() >= write_buffer_size)
				flush();
		}
	};

	class collapse_job
	{
	public:
		collapse_job(
			const std::filesystem::path& input_path,
			const std::vector<track_extent>& tracks,
			const std::filesystem::path& output_path,
			const options& settings) :
			settings_(settings),
			output_path_(output_path)
		{
			if (!input_.open(input_path))
				throw std::system_error(input_.last_error(), "Cannot mmap processed MIDI");

			if (input_.size() < header_.size())
				throw std::runtime_error("Processed MIDI is empty");

			const auto* data = reinterpret_cast<const std::uint8_t*>(input_.data());
			std::copy_n(data, header_.size(), header_.begin());

			auto per_track_budget = read_ahead_budget / (std::max<std::size_t>)(tracks.size(), 1);
			read_ahead_ = std::clamp(per_track_budget, min_read_ahead, max_read_ahead);
			cursors_.resize(tracks.size());

			for (std::size_t i = 0; i < tracks.size(); ++i)
			{
				if (tracks[i].offset > input_.size() || tracks[i].size > input_.size() - tracks[i].offset)
					throw std::runtime_error("Track extents outside the processed MIDI");

				auto& cursor = cursors_[i];
				cursor.begin = cursor.position = cursor.read_ahead = cursor.released = cursor.next_read_ahead =
					data + tracks[i].offset;
				cursor.end = cursor.begin + tracks[i].size;
				total_bytes_ += tracks[i].size;
			}
		}

		std::uint64_t run()
		{
			output_.open(output_path_, std::ios::binary | std::ios::out | std::ios::trunc);
			if (!output_)
				throw std::runtime_error("Cannot write collapsed MIDI");

			output_.exceptions(std::ios::failbit | std::ios::badbit);
			output_.write(reinterpret_cast<const char*>(header_.data()), header_.size());

			const std::size_t sink_count = settings_.channel_split ? 16 : 1;
			for (std::size_t i = 0; i < sink_count; ++i)
			{
				sinks_[i] = std::make_unique<track_sink>();
				sinks_[i]->pending.reserve(write_buffer_size + 64);
			}
			if (!settings_.channel_split)
			{
				sinks_[0]->out = &output_;
				sinks_[0]->stream_offset = header_.size();
			}

			merge();

			std::uint64_t track_count = 0;
			for (std::size_t i = 0; i < sink_count; ++i)
			{
				auto& sink = *sinks_[i];
				if (sink.chunk_open)
					close_chunk(sink);
				if (sink.chunks)
				{
					if (sink.temporary)
						append_temporary(sink);
					track_count += sink.chunks;
				}
				else if (!settings_.remove_empty_tracks)
				{
					output_.write("MTrk\0\0\0\4", 8);
					output_.write(reinterpret_cast<const char*>(end_of_track), sizeof(end_of_track));
					++track_count;
				}
			}

			if (track_count > 0xFFFF)
			{
				// SAFC tolerates MIDIs with more than 2^16 tracks;
				track_count = 0xFFFF;
				// logger->warning("Track count overflow while collapsing tracks");
			}

			output_.seekp(10, std::ios::beg);
			output_.put(static_cast<char>(track_count >> 8));
			output_.put(static_cast<char>(track_count & 0xFF));
			output_.flush();
			output_.close();
			report_progress(true);
			return track_count;
		}

	private:
		struct heap_entry
		{
			std::uint64_t tick;
			std::size_t index;

			// Equal ticks keep track order, as a stable sort of the tracks would
			bool operator<(const heap_entry& other) const
			{
				return tick < other.tick || (tick == other.tick && index < other.index);
			}
		};

		const options& settings_;
		std::filesystem::path output_path_;
		dixelu::memory_mapped_file_reader input_;
		std::ofstream output_;
		std::array<std::uint8_t, 14> header_{};
		std::vector<track_cursor> cursors_;
		std::array<std::unique_ptr<track_sink>, 16> sinks_;
		bool had_channel_events_ = false;
		std::size_t read_ahead_ = 0;
		std::uint64_t total_bytes_ = 0;
		std::uint64_t reported_bytes_ = 0;
		std::uint32_t events_until_check_ = 0;

		void check_cancelled()
		{
			if (settings_.cancel && settings_.cancel->load(std::memory_order_relaxed))
				throw midi_processing_cancelled{};
		}

		void report_progress(bool force = false)
		{
			if (!settings_.progress)
				return;
			std::uint64_t consumed = 0;
			for (const auto& cursor : cursors_)
				consumed += cursor.position - cursor.begin;
			if (!force && consumed - reported_bytes_ < (1ull << 20))
				return;
			reported_bytes_ = consumed;
			settings_.progress(consumed, total_bytes_);
		}

		// Runs once per read_ahead_ / 2 bytes of a track
		void advance_read_ahead(track_cursor& cursor)
		{
#ifdef _WIN32
			// Pages already merged leave the working set; they stay cached as
			// standby memory, which the system can take back for free.
			const auto unread_page = reinterpret_cast<std::uintptr_t>(cursor.position) & ~std::uintptr_t{page_size - 1};
			const auto release_end = (std::max)(cursor.begin, reinterpret_cast<const std::uint8_t*>(unread_page));
			if (release_end > cursor.released)
			{
				VirtualUnlock(const_cast<std::uint8_t*>(cursor.released), release_end - cursor.released);
				cursor.released = release_end;
			}

			// Page faults would read the track a few pages at a time
			const auto start = (std::max)(cursor.read_ahead, cursor.position);
			const auto stop = start + (std::min<std::size_t>)(read_ahead_, cursor.end - start);
			if (stop > start)
			{
				WIN32_MEMORY_RANGE_ENTRY range{const_cast<std::uint8_t*>(start), static_cast<SIZE_T>(stop - start)};
				PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
			}
			cursor.read_ahead = stop;
			cursor.next_read_ahead = cursor.read_ahead - (std::min<std::size_t>)(read_ahead_ / 2,
				cursor.read_ahead - cursor.position);
			if (cursor.next_read_ahead <= cursor.position)
				cursor.next_read_ahead = cursor.end;
#else
			cursor.next_read_ahead = cursor.end;
#endif
		}

		static bool get(track_cursor& cursor, std::uint8_t& value)
		{
			if (cursor.position == cursor.end) [[unlikely]]
			{
				value = 0;
				return false;
			}

			value = *cursor.position++;
			return true;
		}

		static bool get_vlv(track_cursor& cursor, std::uint64_t& value)
		{
			std::uint8_t single_byte = 0;
			value = 0;

			bool status = true;

			do
			{
				status = get(cursor, single_byte);
				value = value << 7 | single_byte & 0x7F;
			}
			while (single_byte & 0x80 && status);

			return status;
		}

		// Reads the next event up to its payload. False once the track ends;
		// a truncated last event ends it too.
		bool next_event(track_cursor& cursor)
		{
			if (cursor.position >= cursor.next_read_ahead && cursor.position < cursor.end)
				advance_read_ahead(cursor);

			std::uint64_t delta;
			std::uint8_t byte;
			if (!get_vlv(cursor, delta) || !get(cursor, byte))
				return false;

			cursor.tick += delta;

			if (byte < 0x80)
			{
				if (!cursor.running_status)
					throw std::runtime_error("Data byte without a status while collapsing tracks");
				cursor.status = cursor.running_status;
				cursor.data[0] = byte;
				return (cursor.status & 0xE0) == 0xC0 || get(cursor, cursor.data[1]);
			}

			cursor.status = byte;
			if (byte < 0xF0)
			{
				cursor.running_status = byte;
				return get(cursor, cursor.data[0]) && ((byte & 0xE0) == 0xC0 || get(cursor, cursor.data[1]));
			}

			if (byte != 0xFF && byte != 0xF0 && byte != 0xF7)
				throw std::runtime_error("Unknown event type " + std::to_string(byte) + " while collapsing tracks");

			if (byte == 0xFF && !get(cursor, cursor.meta_type))
				return false;

			if (!get_vlv(cursor, cursor.payload) || cursor.payload > std::uint64_t(cursor.end - cursor.position))
				return false;

			return byte != 0xFF || cursor.meta_type != 0x2F;
		}

		track_sink& sink_for(std::uint8_t status)
		{
			if (!settings_.channel_split || status >= 0xF0)
				return *sinks_[0];

			// As the in-track channel split does: meta events before the first
			// channel event join that channel's track, later ones go to track 0.
			const auto channel = status & 0x0F;
			if (!had_channel_events_)
			{
				had_channel_events_ = true;
				std::swap(sinks_[0], sinks_[channel]);
			}

			return *sinks_[channel];
		}

		void open_temporary(track_sink& sink)
		{
			static std::atomic_uint64_t counter{0};
			sink.temporary_path = output_path_;
			sink.temporary_path += L".collapse-" + std::to_wstring(counter.fetch_add(1)) + L".tmp";
			sink.temporary = std::make_unique<std::ofstream>(sink.temporary_path,
				std::ios::binary | std::ios::out | std::ios::trunc);

			if (!*sink.temporary)
				throw std::runtime_error("Cannot create a temporary file for collapsed tracks");

			sink.temporary->exceptions(std::ios::failbit | std::ios::badbit);
			sink.out = sink.temporary.get();
		}

		void open_chunk(track_sink& sink)
		{
			if (!sink.out)
				open_temporary(sink);

			sink.flush();
			sink.chunk_start = sink.stream_offset;
			static constexpr std::uint8_t header[] = {'M', 'T', 'r', 'k', 0, 0, 0, 0};
			sink.pending.insert(sink.pending.end(), std::begin(header), std::end(header));
			sink.chunk_size = 0;
			sink.chunk_tick = 0;
			sink.running_status = 0;
			sink.chunk_open = true;
		}

		void close_chunk(track_sink& sink)
		{
			sink.put(end_of_track, sizeof(end_of_track));
			if (sink.chunk_size > 0xFFFFFFFFull)
				throw std::runtime_error("A MIDI event is too large for a single track");
			sink.flush();

			const auto end = sink.stream_offset;
			sink.out->seekp(static_cast<std::streamoff>(sink.chunk_start + 4));
			for (int shift = 24; shift >= 0; shift -= 8)
				sink.out->put(static_cast<char>((sink.chunk_size >> shift) & 0xFF));
			sink.out->seekp(static_cast<std::streamoff>(end));

			sink.chunk_open = false;
			++sink.chunks;
		}

		void put_delta(track_sink& sink, std::uint64_t tick)
		{
			auto delta = tick - sink.chunk_tick;
			sink.chunk_tick = tick;

			if (settings_.delta_overflow_correction)
			{
				while (delta > standard_delta_limit)
				{
					sink.put_vlv(standard_delta_limit);
					sink.put(dummy_event, sizeof(dummy_event));
					sink.running_status = 0;
					delta -= standard_delta_limit;
				}
			}

			sink.put_vlv(delta);
		}

		void put_channel_event(track_sink& sink, std::uint64_t tick,
			std::uint8_t status, std::uint8_t first, std::uint8_t second)
		{
			put_delta(sink, tick);
			if (!settings_.running_status || status != sink.running_status)
				sink.put(status);

			sink.running_status = status;
			sink.put(first);

			if ((status & 0xE0) != 0xC0)
				sink.put(second);
		}

		// Ends the current track at the current tick and continues in a new one
		void roll_over(track_sink& sink)
		{
			const auto tick = sink.chunk_tick;

			for (std::size_t index = 0; index < sink.held.size(); ++index)
			{
				for (auto count = sink.held[index]; count; --count)
				{
					put_channel_event(sink, tick, static_cast<std::uint8_t>(0x80 | (index >> 7)),
						static_cast<std::uint8_t>(index & 0x7F), 0);
				}
			}

			close_chunk(sink);

			open_chunk(sink);

			for (std::size_t index = 0; index < sink.held.size(); ++index)
			{
				for (auto count = sink.held[index]; count; --count)
				{
					put_channel_event(sink, tick, static_cast<std::uint8_t>(0x90 | (index >> 7)),
						static_cast<std::uint8_t>(index & 0x7F), 1);
				}
			}
		}

		void write_event(track_cursor& cursor)
		{
			auto& sink = sink_for(cursor.status);
			const bool is_channel_event = cursor.status < 0xF0;

			// Delta, status, meta type and length, payload
			std::uint64_t size = 10 + 3 + (is_channel_event ? 0 : 10 + cursor.payload);
			if (settings_.delta_overflow_correction)
				size += (cursor.tick - sink.chunk_tick) / standard_delta_limit * 8;

			const auto edges = sink.held_total * 4 + sizeof(end_of_track);
			if (sink.chunk_open && sink.chunk_size + size + edges > settings_.max_track_size)
				roll_over(sink);
			if (!sink.chunk_open)
				open_chunk(sink);

			if (is_channel_event)
			{
				put_channel_event(sink, cursor.tick, cursor.status, cursor.data[0], cursor.data[1]);

				const auto kind = cursor.status & 0xF0;
				if (kind == 0x80 || kind == 0x90)
				{
					auto& held = sink.held[((cursor.status & 0x0F) << 7) | (cursor.data[0] & 0x7F)];
					if (kind == 0x90 && cursor.data[1])
						++held, ++sink.held_total;
					else if (held)
						--held, --sink.held_total;
				}
			}
			else
			{
				put_delta(sink, cursor.tick);

				sink.put(cursor.status);
				if (cursor.status == 0xFF)
					sink.put(cursor.meta_type);

				sink.put_vlv(cursor.payload);
				copy_payload(cursor, sink);
				sink.running_status = 0;
			}
			sink.flush_if_full();
		}

		void copy_payload(track_cursor& cursor, track_sink& sink)
		{
			while (cursor.payload)
			{
				const auto count = static_cast<std::size_t>(
					(std::min<std::uint64_t>)(write_buffer_size, cursor.payload));
				sink.put(cursor.position, count);
				cursor.position += count;
				cursor.payload -= count;
				sink.flush_if_full();
			}
		}

		void merge()
		{
			std::vector<heap_entry> heap;
			heap.reserve(cursors_.size());
			for (std::size_t i = 0; i < cursors_.size(); ++i)
			{
				check_cancelled();
				if (next_event(cursors_[i]))
					heap.push_back({cursors_[i].tick, i});
			}

			const auto sift_down = [&heap](std::size_t index)
			{
				const auto count = heap.size();
				const auto value = heap[index];
				for (;;)
				{
					auto child = index * 2 + 1;
					if (child >= count)
						break;
					if (child + 1 < count && heap[child + 1] < heap[child])
						++child;
					if (!(heap[child] < value))
						break;
					heap[index] = heap[child];
					index = child;
				}
				heap[index] = value;
			};

			for (std::size_t index = heap.size() / 2; index-- > 0;)
				sift_down(index);

			while (!heap.empty())
			{
				if (!events_until_check_--)
				{
					check_cancelled();
					report_progress();
					events_until_check_ = 1u << 16;
				}

				auto& top = heap.front();
				auto& cursor = cursors_[top.index];
				write_event(cursor);

				if (next_event(cursor))
					top.tick = cursor.tick;
				else
				{
					top = heap.back();
					heap.pop_back();
				}
				if (heap.size() > 1)
					sift_down(0);
			}

			// Every track counts as read, including what follows its EOT
			for (auto& cursor : cursors_)
				cursor.position = cursor.end;
		}

		void append_temporary(track_sink& sink)
		{
			sink.temporary->close();
			midi_file_reader chunks(sink.temporary_path);
			chunks.set_cancellation(settings_.cancel);

			if (!chunks.is_open())
				throw std::runtime_error("Cannot read a temporary file of collapsed tracks");

			chunks.copy_to(output_);
			if (chunks.failed())
				throw std::runtime_error("Temporary collapsed track read failed");
		}
	};
};

#endif // SAF_MIDI_TRACK_COLLAPSE_HANDLER
