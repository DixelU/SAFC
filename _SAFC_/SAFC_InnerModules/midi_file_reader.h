#pragma once

#include <buffered_file_reader.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <ostream>
#include <span>
#include <stdexcept>

struct midi_processing_cancelled : std::runtime_error
{
    midi_processing_cancelled() : std::runtime_error("MIDI processing cancelled") {}
};

// Cancellation belongs to each reader/job. Keep the ordinary parser fast path
// and bound checks for cancelled processing to 4096 byte reads or 64 KiB copies.
class midi_file_reader : public dixelu::buffered_file_reader
{
public:
    using dixelu::buffered_file_reader::buffered_file_reader;

    void set_cancellation(const std::atomic_bool* value) noexcept
    {
        cancellation_ = value;
        unchecked_bytes_ = 0;
    }

    void check_cancelled()
    {
        if (!cancellation_) return;
        if (unchecked_bytes_) { --unchecked_bytes_; return; }
        if (cancellation_->load(std::memory_order_relaxed)) throw midi_processing_cancelled{};
        unchecked_bytes_ = 4095;
    }

    size_type copy_to(std::ostream& output)
    {
        if (!cancellation_) return dixelu::buffered_file_reader::copy_to(output);
        std::array<std::byte, 65536> chunk;
        size_type copied = 0;
        while (output)
        {
            if (cancellation_->load(std::memory_order_relaxed)) throw midi_processing_cancelled{};
            const auto count = read(std::span(chunk));
            if (!count) break;
            output.write(reinterpret_cast<const char*>(chunk.data()), count);
            copied += count;
        }
        return copied;
    }

private:
    const std::atomic_bool* cancellation_ = nullptr;
    std::uint32_t unchecked_bytes_ = 0;
};

// MIDI parsing historically substituted zero after a failed byte read. Keep
// that parser behavior at this domain boundary while the consolidated reader
// represents EOF explicitly and never aliases a real 0x00 byte with EOF.
[[nodiscard]] inline std::uint8_t read_midi_byte(midi_file_reader& reader)
{
	reader.check_cancelled();
	return std::to_integer<std::uint8_t>(reader.get_or(std::byte{0}));
}
