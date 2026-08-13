#pragma once

#include <buffered_file_reader.h>

#include <cstddef>
#include <cstdint>

using midi_file_reader = dixelu::buffered_file_reader;

// MIDI parsing historically substituted zero after a failed byte read. Keep
// that parser behavior at this domain boundary while the consolidated reader
// represents EOF explicitly and never aliases a real 0x00 byte with EOF.
[[nodiscard]] inline std::uint8_t read_midi_byte(midi_file_reader& reader)
{
	const auto byte = reader.get();
	return byte ? std::to_integer<std::uint8_t>(*byte) : std::uint8_t{0};
}
