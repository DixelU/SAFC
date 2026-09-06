#include "compressed_midi_event_source.h"

#include <Windows.h>

#include <archive.h>
#include <archive_entry.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t archive_input_buffer_size = 1u << 16;
constexpr std::uint32_t maximum_archive_depth = 16;
constexpr std::uint64_t decoded_page_memory_budget = 128ull << 20;

class sequential_stream
{
public:
	virtual ~sequential_stream() = default;
	virtual std::size_t read(std::uint8_t* destination, std::size_t size) = 0;
	virtual bool can_seek() const { return false; }
	virtual std::int64_t seek(std::int64_t, int)
	{
		throw std::runtime_error("The archive input is not seekable");
	}
};

class file_stream final : public sequential_stream
{
public:
	explicit file_stream(const std::wstring& filename)
		: input_(std::filesystem::path(filename), std::ios::binary)
	{
		if (!input_)
			throw std::runtime_error("Unable to open the compressed MIDI file");
	}

	std::size_t read(std::uint8_t* destination, std::size_t size) override
	{
		input_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
		const auto count = input_.gcount();
		if (count < 0 || (!input_ && !input_.eof()))
			throw std::runtime_error("Failed while reading the compressed MIDI file");
		return static_cast<std::size_t>(count);
	}

	bool can_seek() const override { return true; }

	std::int64_t seek(std::int64_t offset, int whence) override
	{
		std::ios_base::seekdir direction;
		switch (whence)
		{
			case SEEK_SET: direction = std::ios::beg; break;
			case SEEK_CUR: direction = std::ios::cur; break;
			case SEEK_END: direction = std::ios::end; break;
			default: throw std::runtime_error("Invalid archive seek origin");
		}

		input_.clear();
		input_.seekg(static_cast<std::streamoff>(offset), direction);
		if (!input_)
			throw std::runtime_error("Unable to seek in the archive input");
		const auto position = input_.tellg();
		if (position < 0)
			throw std::runtime_error("Unable to determine the archive input position");
		return static_cast<std::int64_t>(position);
	}

private:
	std::ifstream input_;
};

class prefix_stream final : public sequential_stream
{
public:
	prefix_stream(std::unique_ptr<sequential_stream> input, std::vector<std::uint8_t> prefix)
		: input_(std::move(input)), prefix_(std::move(prefix))
	{
	}

	std::size_t read(std::uint8_t* destination, std::size_t size) override
	{
		std::size_t written = 0;
		if (prefix_position_ < prefix_.size())
		{
			const auto available = prefix_.size() - prefix_position_;
			const auto copied = (std::min)(available, size);
			std::memcpy(destination, prefix_.data() + prefix_position_, copied);
			prefix_position_ += copied;
			written += copied;
		}

		if (written < size)
			written += input_->read(destination + written, size - written);
		return written;
	}

private:
	std::unique_ptr<sequential_stream> input_;
	std::vector<std::uint8_t> prefix_;
	std::size_t prefix_position_ = 0;
};

class archive_entry_stream final : public sequential_stream
{
public:
	archive_entry_stream(std::unique_ptr<sequential_stream> input,
		const compressed_midi_event_source::progress_callback& progress,
		std::uint32_t depth)
		: input_(std::move(input)), progress_(progress), depth_(depth)
	{
		archive_ = archive_read_new();
		if (!archive_)
			throw std::bad_alloc();

		archive_read_support_filter_all(archive_);
		archive_read_support_format_all(archive_);
		// XZ/GZip/BZip2 files are filters around one otherwise "raw" entry.
		archive_read_support_format_raw(archive_);
		if (input_->can_seek())
			archive_read_set_seek_callback(archive_, &seek_callback);

		const int opened = archive_read_open2(
			archive_, this, nullptr, &read_callback, &skip_callback, nullptr);
		if (opened != ARCHIVE_OK)
			fail("Unable to open archive layer");

		archive_entry* entry = nullptr;
		for (;;)
		{
			const int result = archive_read_next_header(archive_, &entry);
			if (result == ARCHIVE_EOF)
				fail("Archive contains no playable file entry");
			if (result < ARCHIVE_WARN)
				fail("Unable to read archive entry");
			if (archive_entry_filetype(entry) == AE_IFREG)
			{
				const bool raw_stream =
					(archive_format(archive_) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_RAW;
				const char* path = archive_entry_pathname(entry);
				if (raw_stream || is_supported_entry_name(path))
					break;
			}
			archive_read_data_skip(archive_);
		}

		if (progress_)
		{
			std::string message = "Opened archive layer " +
				std::to_string(depth_) + "-" +
				std::to_string(depth_ + decoded_layer_count() - 1);
			if (const char* path = archive_entry_pathname(entry); path && *path)
				message += ": " + std::string(path);
			progress_(message);
		}
	}

	~archive_entry_stream() override
	{
		if (archive_)
		{
			archive_read_close(archive_);
			archive_read_free(archive_);
		}
	}

	std::size_t read(std::uint8_t* destination, std::size_t size) override
	{
		const auto result = archive_read_data(archive_, destination, size);
		if (result < 0)
			fail("Unable to decompress archive entry");
		return static_cast<std::size_t>(result);
	}

	std::uint32_t decoded_layer_count() const
	{
		const auto filters = (std::max)(archive_filter_count(archive_) - 1, 0);
		const bool archive_container =
			(archive_format(archive_) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_RAW;
		return (std::max)(1u,
			static_cast<std::uint32_t>(filters) + (archive_container ? 1u : 0u));
	}

private:
	static bool is_supported_entry_name(const char* path)
	{
		if (!path || !*path)
			return true;
		std::string lower(path);
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		for (const char* suffix :
			{".mid", ".midi", ".xz", ".zip", ".7z", ".gz", ".bz2", ".lzma"})
		{
			const std::string_view ending(suffix);
			if (lower.size() >= ending.size() &&
				lower.compare(
					lower.size() - ending.size(), ending.size(),
					ending.data(), ending.size()) == 0)
				return true;
		}
		return false;
	}

	[[noreturn]] void fail(const char* prefix)
	{
		std::string message(prefix);
		if (!callback_error_.empty())
			message += ": " + callback_error_;
		else if (archive_ && archive_error_string(archive_))
			message += ": " + std::string(archive_error_string(archive_));
		throw std::runtime_error(message);
	}

	static la_ssize_t read_callback(archive*, void* client, const void** buffer)
	{
		auto* self = static_cast<archive_entry_stream*>(client);
		try
		{
			const auto count = self->input_->read(
				self->input_buffer_.data(), self->input_buffer_.size());
			*buffer = self->input_buffer_.data();
			return static_cast<la_ssize_t>(count);
		}
		catch (const std::exception& e)
		{
			self->callback_error_ = e.what();
			return ARCHIVE_FATAL;
		}
	}

	static la_int64_t skip_callback(archive*, void* client, la_int64_t request)
	{
		auto* self = static_cast<archive_entry_stream*>(client);
		try
		{
			la_int64_t skipped = 0;
			while (skipped < request)
			{
				const auto amount = static_cast<std::size_t>((std::min)(
					request - skipped,
					static_cast<la_int64_t>(self->input_buffer_.size())));
				const auto count = self->input_->read(self->input_buffer_.data(), amount);
				if (count == 0)
					break;
				skipped += static_cast<la_int64_t>(count);
			}
			return skipped;
		}
		catch (const std::exception& e)
		{
			self->callback_error_ = e.what();
			return ARCHIVE_FATAL;
		}
	}

	static la_int64_t seek_callback(
		archive*, void* client, la_int64_t offset, int whence)
	{
		auto* self = static_cast<archive_entry_stream*>(client);
		try
		{
			return static_cast<la_int64_t>(self->input_->seek(offset, whence));
		}
		catch (const std::exception& e)
		{
			self->callback_error_ = e.what();
			return ARCHIVE_FATAL;
		}
	}

	std::unique_ptr<sequential_stream> input_;
	compressed_midi_event_source::progress_callback progress_;
	std::uint32_t depth_ = 0;
	archive* archive_ = nullptr;
	std::array<std::uint8_t, archive_input_buffer_size> input_buffer_{};
	std::string callback_error_;
};

enum class stream_kind
{
	midi,
	archive,
	unknown
};

stream_kind detect_stream_kind(const std::vector<std::uint8_t>& prefix)
{
	if (prefix.size() >= 4 && std::memcmp(prefix.data(), "MThd", 4) == 0)
		return stream_kind::midi;

	static constexpr std::array<std::uint8_t, 6> xz_magic
	{0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00};
	static constexpr std::array<std::uint8_t, 6> seven_zip_magic
	{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
	static constexpr std::array<std::uint8_t, 3> gzip_magic
	{0x1F, 0x8B, 0x08};
	static constexpr std::array<std::uint8_t, 3> bzip_magic
	{'B', 'Z', 'h'};

	if (prefix.size() >= xz_magic.size() &&
		std::equal(xz_magic.begin(), xz_magic.end(), prefix.begin()))
		return stream_kind::archive;
	if (prefix.size() >= seven_zip_magic.size() &&
		std::equal(seven_zip_magic.begin(), seven_zip_magic.end(), prefix.begin()))
		return stream_kind::archive;
	if (prefix.size() >= gzip_magic.size() &&
		std::equal(gzip_magic.begin(), gzip_magic.end(), prefix.begin()))
		return stream_kind::archive;
	if (prefix.size() >= bzip_magic.size() &&
		std::equal(bzip_magic.begin(), bzip_magic.end(), prefix.begin()))
		return stream_kind::archive;
	if (prefix.size() >= 4 && prefix[0] == 'P' && prefix[1] == 'K' &&
		((prefix[2] == 3 && prefix[3] == 4) ||
			(prefix[2] == 5 && prefix[3] == 6) ||
			(prefix[2] == 7 && prefix[3] == 8)))
		return stream_kind::archive;

	return stream_kind::unknown;
}

bool is_seven_zip_stream(const std::vector<std::uint8_t>& prefix)
{
	static constexpr std::array<std::uint8_t, 6> seven_zip_magic
	{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
	return prefix.size() >= seven_zip_magic.size() &&
		std::equal(seven_zip_magic.begin(), seven_zip_magic.end(), prefix.begin());
}

std::vector<std::uint8_t> read_prefix(sequential_stream& input, std::size_t maximum)
{
	std::vector<std::uint8_t> prefix(maximum);
	std::size_t count = 0;
	while (count < maximum)
	{
		const auto received = input.read(prefix.data() + count, maximum - count);
		if (received == 0)
			break;
		count += received;
	}
	prefix.resize(count);
	return prefix;
}

class midi_stream_reader
{
public:
	midi_stream_reader(
		std::unique_ptr<sequential_stream> input,
		const std::atomic<bool>* cancel_requested)
		: input_(std::move(input)), cancel_requested_(cancel_requested)
	{
	}

	void read_exact(void* destination, std::size_t size)
	{
		auto* bytes = static_cast<std::uint8_t*>(destination);
		std::size_t count = 0;
		while (count < size)
		{
			const auto received = input_->read(bytes + count, size - count);
			if (received == 0)
				throw std::runtime_error("Unexpected end of decompressed MIDI stream");
			count += received;
			consumed_ += received;
		}
	}

	std::uint8_t byte()
	{
		std::uint8_t value = 0;
		read_exact(&value, 1);
		return value;
	}

	void skip(std::uint64_t count)
	{
		std::array<std::uint8_t, 1u << 15> scratch{};
		while (count > 0)
		{
			if (cancel_requested_ &&
				cancel_requested_->load(std::memory_order_acquire))
				throw std::runtime_error("Compressed MIDI preparation was cancelled");
			const auto amount = static_cast<std::size_t>((std::min<std::uint64_t>)(
				count, scratch.size()));
			read_exact(scratch.data(), amount);
			count -= amount;
		}
	}

	std::uint64_t consumed() const { return consumed_; }

private:
	std::unique_ptr<sequential_stream> input_;
	const std::atomic<bool>* cancel_requested_ = nullptr;
	std::uint64_t consumed_ = 0;
};

std::uint16_t read_be16(const std::uint8_t* data)
{
	return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
}

std::uint32_t read_be32(const std::uint8_t* data)
{
	return (static_cast<std::uint32_t>(data[0]) << 24) |
		(static_cast<std::uint32_t>(data[1]) << 16) |
		(static_cast<std::uint32_t>(data[2]) << 8) |
		static_cast<std::uint32_t>(data[3]);
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right)
{
	if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
		return (std::numeric_limits<std::uint64_t>::max)();
	return left + right;
}

std::uint64_t saturating_mul_div(
	std::uint64_t value, std::uint64_t multiplier, std::uint64_t divisor)
{
	if (divisor == 0)
		return (std::numeric_limits<std::uint64_t>::max)();
	const auto quotient = value / divisor;
	const auto remainder = value % divisor;
	if (multiplier != 0 && quotient > (std::numeric_limits<std::uint64_t>::max)() / multiplier)
		return (std::numeric_limits<std::uint64_t>::max)();
	const auto whole = quotient * multiplier;
	const auto fraction = (remainder * multiplier) / divisor;
	return saturating_add(whole, fraction);
}
}

struct compressed_midi_event_source::impl
{
	struct temporary_files
	{
		std::wstring cache_path;
		std::vector<std::wstring> archive_paths;

		~temporary_files()
		{
			if (!cache_path.empty())
				DeleteFileW(cache_path.c_str());
			for (const auto& path : archive_paths)
				DeleteFileW(path.c_str());
		}
	};

	struct cached_event
	{
		std::uint64_t tick = 0;
		std::uint32_t short_msg = 0;
		std::uint8_t kind = 0;
		std::uint8_t key = 0;
		std::uint8_t velocity = 0;
		std::uint8_t channel = 0;
	};
	static_assert(sizeof(cached_event) == 16);

	struct page_info
	{
		std::uint64_t file_offset = 0;
		std::uint32_t compressed_size = 0;
		std::uint32_t event_count = 0;
		cached_event first_event;
	};

	struct track_info
	{
		std::vector<page_info> pages;
	};

	struct cursor
	{
		std::size_t page_index = 0;
		std::size_t event_index = 0;
		cached_event next_event;
		std::vector<cached_event> decoded_page;
		bool active = false;
	};

	struct heap_entry
	{
		std::uint64_t tick = 0;
		std::uint16_t track = 0;

		bool operator>(const heap_entry& other) const
		{
			if (tick != other.tick)
				return tick > other.tick;
			return track > other.track;
		}
	};

	struct tempo_segment
	{
		std::uint64_t tick = 0;
		std::uint64_t time_us = 0;
		std::uint32_t tempo = 500000;
	};

	explicit impl(progress_callback callback, const std::atomic<bool>* cancellation)
		: progress(std::move(callback)), cancel_requested(cancellation)
	{
		files = std::make_shared<temporary_files>();
		wchar_t temporary_directory[MAX_PATH + 1]{};
		wchar_t temporary_filename[MAX_PATH + 1]{};
		if (!GetTempPathW(MAX_PATH, temporary_directory) ||
			!GetTempFileNameW(temporary_directory, L"SFC", 0, temporary_filename))
			throw std::runtime_error("Unable to create compressed MIDI page cache");

		cache_path = temporary_filename;
		files->cache_path = cache_path;
		cache_writer.open(cache_path, std::ios::binary | std::ios::trunc);
		if (!cache_writer)
			throw std::runtime_error("Unable to open compressed MIDI page cache");
	}

	explicit impl(const impl& source)
		: files(source.files), cache_path(source.cache_path), tracks(source.tracks),
		tempo_segments(source.tempo_segments), maximum_tick(source.maximum_tick),
		total_duration(source.total_duration), events_total(source.events_total),
		events_per_page(source.events_per_page),
		track_count_value(source.track_count_value), ppq(source.ppq), depth(source.depth)
	{
		cache_reader.open(cache_path, std::ios::binary);
		if (!cache_reader)
			throw std::runtime_error("Unable to open prepared MIDI page cache reader");
		rewind();
	}

	~impl()
	{
		cache_writer.close();
		cache_reader.close();
	}

	void check_cancelled() const
	{
		if (cancel_requested && cancel_requested->load(std::memory_order_acquire))
			throw std::runtime_error("Compressed MIDI preparation was cancelled");
	}

	void report(const std::string& message) const
	{
		if (progress)
			progress(message);
	}

	std::unique_ptr<sequential_stream> materialize_seekable_archive(
		std::unique_ptr<sequential_stream> input)
	{
		wchar_t temporary_directory[MAX_PATH + 1]{};
		wchar_t temporary_filename[MAX_PATH + 1]{};
		if (!GetTempPathW(MAX_PATH, temporary_directory) ||
			!GetTempFileNameW(temporary_directory, L"SFA", 0, temporary_filename))
			throw std::runtime_error("Unable to create a seekable nested-archive cache");

		const std::wstring path = temporary_filename;
		files->archive_paths.push_back(path);
		std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("Unable to open the seekable nested-archive cache");

		report("Caching compressed inner 7z layer for random access");
		std::array<std::uint8_t, archive_input_buffer_size> buffer{};
		for (;;)
		{
			check_cancelled();
			const auto count = input->read(buffer.data(), buffer.size());
			if (count == 0)
				break;
			output.write(
				reinterpret_cast<const char*>(buffer.data()),
				static_cast<std::streamsize>(count));
			if (!output)
				throw std::runtime_error("Unable to write the seekable nested-archive cache");
		}
		output.close();
		if (!output)
			throw std::runtime_error("Unable to finish the seekable nested-archive cache");
		return std::make_unique<file_stream>(path);
	}

	std::unique_ptr<sequential_stream> unwrap(const std::wstring& filename)
	{
		std::unique_ptr<sequential_stream> input = std::make_unique<file_stream>(filename);
		for (;;)
		{
			check_cancelled();
			auto prefix = read_prefix(*input, 8);
			const auto kind = detect_stream_kind(prefix);
			const bool seven_zip = is_seven_zip_stream(prefix);
			if (input->can_seek())
				input->seek(0, SEEK_SET);
			else
				input = std::make_unique<prefix_stream>(std::move(input), std::move(prefix));

			if (kind == stream_kind::midi)
				return input;
			if (kind != stream_kind::archive)
				throw std::runtime_error("The selected file does not contain a recognised MIDI/archive stream");
			if (depth >= maximum_archive_depth)
				throw std::runtime_error("Archive nesting exceeds the safety limit of 16 layers");
			if (seven_zip && !input->can_seek())
				input = materialize_seekable_archive(std::move(input));

			auto archive_stream = std::make_unique<archive_entry_stream>(
				std::move(input), progress, depth + 1);
			depth += archive_stream->decoded_layer_count();
			if (depth > maximum_archive_depth)
				throw std::runtime_error("Archive nesting exceeds the safety limit of 16 layers");
			input = std::move(archive_stream);
		}
	}

	class limited_track_reader
	{
	public:
		limited_track_reader(midi_stream_reader& reader, std::uint64_t remaining)
			: reader_(reader), remaining_(remaining)
		{
		}

		std::uint8_t byte()
		{
			if (remaining_ == 0)
				throw std::runtime_error("MIDI event exceeds its declared track size");
			--remaining_;
			return reader_.byte();
		}

		std::uint64_t vlv()
		{
			std::uint64_t value = 0;
			for (int count = 0; count < 8; ++count)
			{
				const auto current = byte();
				if (value > ((std::numeric_limits<std::uint64_t>::max)() >> 7))
					throw std::runtime_error("MIDI variable-length value overflows 64 bits");
				value = (value << 7) | (current & 0x7F);
				if ((current & 0x80) == 0)
					return value;
			}
			throw std::runtime_error("MIDI variable-length value is unreasonably long");
		}

		void skip(std::uint64_t count)
		{
			if (count > remaining_)
				throw std::runtime_error("MIDI event exceeds its declared track size");
			reader_.skip(count);
			remaining_ -= count;
		}

		std::uint64_t remaining() const { return remaining_; }

	private:
		midi_stream_reader& reader_;
		std::uint64_t remaining_ = 0;
	};

	void flush_page(std::uint16_t track, std::vector<cached_event>& events)
	{
		if (events.empty())
			return;

		const auto raw_size = events.size() * sizeof(cached_event);
		std::vector<std::uint8_t> compressed(ZSTD_compressBound(raw_size));
		const auto compressed_size = ZSTD_compress(
			compressed.data(), compressed.size(), events.data(), raw_size, 1);
		if (ZSTD_isError(compressed_size))
			throw std::runtime_error(
				"Unable to compress MIDI page: " + std::string(ZSTD_getErrorName(compressed_size)));
		if (compressed_size > (std::numeric_limits<std::uint32_t>::max)())
			throw std::runtime_error("Compressed MIDI page exceeds the cache format limit");

		const auto position = cache_writer.tellp();
		if (position < 0)
			throw std::runtime_error("Unable to address compressed MIDI page cache");
		cache_writer.write(
			reinterpret_cast<const char*>(compressed.data()),
			static_cast<std::streamsize>(compressed_size));
		if (!cache_writer)
			throw std::runtime_error("Unable to write compressed MIDI page cache");

		page_info page;
		page.file_offset = static_cast<std::uint64_t>(position);
		page.compressed_size = static_cast<std::uint32_t>(compressed_size);
		page.event_count = static_cast<std::uint32_t>(events.size());
		page.first_event = events.front();
		tracks[track].pages.push_back(page);
		events.clear();
	}

	void append_event(std::uint16_t track, std::vector<cached_event>& page, cached_event event)
	{
		page.push_back(event);
		++events_total;
		if (page.size() >= events_per_page)
			flush_page(track, page);
		if ((events_total & ((1u << 20) - 1)) == 0)
		{
			check_cancelled();
			report("Indexed " + std::to_string(events_total) + " MIDI events");
		}
	}

	void parse_track(midi_stream_reader& reader, std::uint16_t track, std::uint32_t byte_length)
	{
		limited_track_reader input(reader, byte_length);
		std::vector<cached_event> page;
		page.reserve(events_per_page);
		std::uint64_t tick = 0;
		std::uint8_t running_status = 0;
		std::uint32_t cancellation_sample = 0;

		while (input.remaining() > 0)
		{
			if ((++cancellation_sample & 0xFFFFu) == 0)
				check_cancelled();
			tick = saturating_add(tick, input.vlv());
			maximum_tick = (std::max)(maximum_tick, tick);

			std::uint8_t command = input.byte();
			std::uint8_t data1 = 0;
			if (command < 0x80)
			{
				if (running_status == 0)
					throw std::runtime_error("MIDI running status is missing at a track event");
				data1 = command;
				command = running_status;
			}
			else if (command < 0xF0)
			{
				running_status = command;
				data1 = input.byte();
			}
			else if (command == 0xFF)
			{
				running_status = 0;
				const auto type = input.byte();
				const auto length = input.vlv();
				if (type == 0x51 && length == 3)
				{
					std::uint32_t tempo = static_cast<std::uint32_t>(input.byte()) << 16;
					tempo |= static_cast<std::uint32_t>(input.byte()) << 8;
					tempo |= input.byte();
					tempos[tick] = tempo;
				}
				else
					input.skip(length);

				if (type == 0x2F)
				{
					input.skip(input.remaining());
					break;
				}
				continue;
			}
			else if (command == 0xF0 || command == 0xF7)
			{
				running_status = 0;
				input.skip(input.vlv());
				continue;
			}
			else
				throw std::runtime_error("Unsupported system event in MIDI track");

			const auto family = command & 0xF0;
			std::uint8_t data2 = 0;
			if (family != 0xC0 && family != 0xD0)
				data2 = input.byte();

			cached_event event;
			event.tick = tick;
			event.short_msg = static_cast<std::uint32_t>(command) |
				(static_cast<std::uint32_t>(data1) << 8) |
				(static_cast<std::uint32_t>(data2) << 16);
			event.key = data1;
			event.velocity = data2;
			event.channel = command & 0x0F;
			if (family == 0x80 || (family == 0x90 && data2 == 0))
				event.kind = static_cast<std::uint8_t>(generated_event::kind::note_off);
			else if (family == 0x90)
				event.kind = static_cast<std::uint8_t>(generated_event::kind::note_on);
			else
				event.kind = static_cast<std::uint8_t>(generated_event::kind::control);

			append_event(track, page, event);
		}

		flush_page(track, page);
	}

	void parse_midi(std::unique_ptr<sequential_stream> input)
	{
		midi_stream_reader reader(std::move(input), cancel_requested);
		std::array<std::uint8_t, 14> header{};
		reader.read_exact(header.data(), header.size());
		if (std::memcmp(header.data(), "MThd", 4) != 0)
			throw std::runtime_error("Decompressed stream is not a Standard MIDI File");

		const auto header_size = read_be32(header.data() + 4);
		if (header_size < 6)
			throw std::runtime_error("MIDI header is shorter than the required six bytes");
		track_count_value = read_be16(header.data() + 10);
		const auto division = read_be16(header.data() + 12);
		if (track_count_value == 0)
			throw std::runtime_error("MIDI declares zero tracks");
		if ((division & 0x8000) != 0 || division == 0)
			throw std::runtime_error("SMPTE-time MIDI is not supported by the compressed player yet");
		ppq = division;
		if (header_size > 6)
			reader.skip(header_size - 6);

		tracks.resize(track_count_value);
		const auto target = decoded_page_memory_budget /
			(static_cast<std::uint64_t>(track_count_value) * sizeof(cached_event));
		events_per_page = static_cast<std::size_t>((std::clamp<std::uint64_t>)(target, 64, 65536));

		report("MIDI reached through " + std::to_string(depth) +
			" archive layer(s); preparing " + std::to_string(track_count_value) + " tracks");

		std::uint16_t parsed_tracks = 0;
		while (parsed_tracks < track_count_value)
		{
			std::array<std::uint8_t, 8> chunk{};
			reader.read_exact(chunk.data(), chunk.size());
			const auto chunk_size = read_be32(chunk.data() + 4);
			if (std::memcmp(chunk.data(), "MTrk", 4) != 0)
			{
				reader.skip(chunk_size);
				continue;
			}

			report("Preparing track " + std::to_string(parsed_tracks + 1) +
				" / " + std::to_string(track_count_value));
			parse_track(reader, parsed_tracks, chunk_size);
			++parsed_tracks;
		}
	}

	void build_tempo_map()
	{
		tempo_segments.clear();
		tempo_segments.push_back({0, 0, 500000});
		std::uint64_t previous_tick = 0;
		std::uint64_t previous_time = 0;
		std::uint32_t previous_tempo = 500000;

		for (const auto& [tick, tempo] : tempos)
		{
			previous_time = saturating_add(previous_time,
				saturating_mul_div(tick - previous_tick, previous_tempo, ppq));
			if (tick == 0)
				tempo_segments.front() = {0, 0, tempo};
			else
				tempo_segments.push_back({tick, previous_time, tempo});
			previous_tick = tick;
			previous_tempo = tempo;
		}

		total_duration = tick_to_us(maximum_tick);
	}

	std::uint64_t tick_to_us(std::uint64_t tick) const
	{
		auto found = std::upper_bound(
			tempo_segments.begin(), tempo_segments.end(), tick,
			[](std::uint64_t value, const tempo_segment& segment)
		{
			return value < segment.tick;
		});
		if (found != tempo_segments.begin())
			--found;
		return saturating_add(found->time_us,
			saturating_mul_div(tick - found->tick, found->tempo, ppq));
	}

	void finish_cache()
	{
		cache_writer.flush();
		cache_writer.close();
		cache_reader.open(cache_path, std::ios::binary);
		if (!cache_reader)
			throw std::runtime_error("Unable to reopen compressed MIDI page cache");
	}

	void load_page(std::uint16_t track, cursor& state)
	{
		const auto& page = tracks[track].pages[state.page_index];
		std::vector<std::uint8_t> compressed(page.compressed_size);
		cache_reader.clear();
		cache_reader.seekg(static_cast<std::streamoff>(page.file_offset));
		cache_reader.read(
			reinterpret_cast<char*>(compressed.data()),
			static_cast<std::streamsize>(compressed.size()));
		if (!cache_reader)
			throw std::runtime_error("Unable to read compressed MIDI page cache");

		state.decoded_page.resize(page.event_count);
		const auto raw_size = state.decoded_page.size() * sizeof(cached_event);
		const auto result = ZSTD_decompress(
			state.decoded_page.data(), raw_size, compressed.data(), compressed.size());
		if (ZSTD_isError(result) || result != raw_size)
			throw std::runtime_error("Compressed MIDI page cache is corrupt");
	}

	void advance(std::uint16_t track, cursor& state)
	{
		const auto& pages = tracks[track].pages;
		const auto& page = pages[state.page_index];
		if (state.event_index + 1 < page.event_count)
		{
			if (state.decoded_page.empty())
				load_page(track, state);
			++state.event_index;
			state.next_event = state.decoded_page[state.event_index];
			return;
		}

		state.decoded_page.clear();
		++state.page_index;
		state.event_index = 0;
		if (state.page_index >= pages.size())
		{
			state.active = false;
			return;
		}
		state.next_event = pages[state.page_index].first_event;
	}

	void rewind()
	{
		heap = {};
		cursors.clear();
		cursors.resize(tracks.size());
		for (std::uint16_t track = 0; track < track_count_value; ++track)
		{
			if (tracks[track].pages.empty())
				continue;
			auto& state = cursors[track];
			state.active = true;
			state.next_event = tracks[track].pages.front().first_event;
			heap.push({state.next_event.tick, track});
		}
	}

	bool next(generated_event& out)
	{
		if (heap.empty())
			return false;

		const auto reference = heap.top();
		heap.pop();
		auto& state = cursors[reference.track];
		const auto event = state.next_event;

		out = generated_event{};
		out.time_us = tick_to_us(event.tick);
		out.short_msg = event.short_msg;
		out.k = static_cast<generated_event::kind>(event.kind);
		out.key = event.key;
		out.velocity = event.velocity;
		out.channel = event.channel;
		out.track_index = reference.track;

		advance(reference.track, state);
		if (state.active)
			heap.push({state.next_event.tick, reference.track});
		return true;
	}

	progress_callback progress;
	const std::atomic<bool>* cancel_requested = nullptr;
	std::shared_ptr<temporary_files> files;
	std::wstring cache_path;
	std::ofstream cache_writer;
	std::ifstream cache_reader;
	std::vector<track_info> tracks;
	std::vector<cursor> cursors;
	std::priority_queue<heap_entry, std::vector<heap_entry>, std::greater<heap_entry>> heap;
	std::map<std::uint64_t, std::uint32_t> tempos;
	std::vector<tempo_segment> tempo_segments;
	std::uint64_t maximum_tick = 0;
	std::uint64_t total_duration = 0;
	std::uint64_t events_total = 0;
	std::size_t events_per_page = 65536;
	std::uint16_t track_count_value = 0;
	std::uint16_t ppq = 0;
	std::uint32_t depth = 0;
};

compressed_midi_event_source::compressed_midi_event_source(std::unique_ptr<impl> implementation)
	: impl_(std::move(implementation))
{
}

compressed_midi_event_source::~compressed_midi_event_source() = default;

std::shared_ptr<compressed_midi_event_source> compressed_midi_event_source::open(
	const std::wstring& filename,
	progress_callback progress,
	const std::atomic<bool>* cancel_requested,
	std::string& error)
{
	try
	{
		auto implementation = std::make_unique<impl>(std::move(progress), cancel_requested);
		auto stream = implementation->unwrap(filename);
		implementation->parse_midi(std::move(stream));
		implementation->build_tempo_map();
		implementation->finish_cache();
		implementation->report(
			"Ready: " + std::to_string(implementation->events_total) + " events in " +
			std::to_string(implementation->track_count_value) + " tracks");
		implementation->rewind();
		return std::shared_ptr<compressed_midi_event_source>(
			new compressed_midi_event_source(std::move(implementation)));
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return {};
	}
}

std::uint64_t compressed_midi_event_source::total_duration_us() const
{
	return impl_->total_duration;
}

void compressed_midi_event_source::rewind()
{
	impl_->rewind();
}

bool compressed_midi_event_source::next(generated_event& out)
{
	return impl_->next(out);
}

std::uint16_t compressed_midi_event_source::track_count() const
{
	return impl_->track_count_value;
}

std::uint64_t compressed_midi_event_source::event_count() const
{
	return impl_->events_total;
}

std::uint32_t compressed_midi_event_source::archive_depth() const
{
	return impl_->depth;
}

std::shared_ptr<compressed_midi_event_source>
compressed_midi_event_source::fork_reader() const
{
	return std::shared_ptr<compressed_midi_event_source>(
		new compressed_midi_event_source(std::make_unique<impl>(*impl_)));
}
