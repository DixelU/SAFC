#pragma once
#ifndef SAFC_VIDEO_PARAMETER_SETS
#define SAFC_VIDEO_PARAMETER_SETS

// Reads the colour signalling (VUI video_signal_type) from H.264 and HEVC
// sequence parameter sets, so the exporter can check what an encoder will
// tell players about range and matrix before it renders.

#include <cstddef>
#include <cstdint>
#include <vector>

struct video_signal_description
{
	// False when the parameter set uses syntax this reader skips.
	bool parsed = false;
	bool signal_type_present = false;
	bool full_range = false;
	bool colour_description_present = false;
	// ITU-T H.273 codes; 2 is "unspecified".
	std::uint8_t primaries = 2;
	std::uint8_t transfer = 2;
	std::uint8_t matrix = 2;
};

class video_bit_reader
{
public:
	// `nal` excludes the NAL header; emulation prevention bytes are removed.
	video_bit_reader(const std::uint8_t* nal, std::size_t size)
	{
		std::size_t zeros = 0;
		for (std::size_t index = 0; index < size; ++index)
		{
			if (zeros >= 2 && nal[index] == 3)
			{
				zeros = 0;
				continue;
			}
			bytes_.push_back(nal[index]);
			zeros = nal[index] == 0 ? zeros + 1 : 0;
		}
	}

	bool good() const noexcept { return good_; }

	std::uint32_t bits(int count) noexcept
	{
		std::uint32_t value = 0;
		for (int bit = 0; bit < count; ++bit)
		{
			if (position_ >= bytes_.size() * 8)
			{
				good_ = false;
				return 0;
			}
			value = (value << 1) | ((bytes_[position_ >> 3] >> (7 - (position_ & 7))) & 1U);
			++position_;
		}
		return value;
	}

	bool flag() noexcept { return bits(1) != 0; }

	std::uint32_t exp_golomb() noexcept
	{
		int zeros = 0;
		while (good_ && !flag())
			if (++zeros > 31)
			{
				good_ = false;
				return 0;
			}
		return ((1U << zeros) - 1U) + bits(zeros);
	}

private:
	std::vector<std::uint8_t> bytes_;
	std::size_t position_ = 0;
	bool good_ = true;
};

// The VUI fields up to and including colour_description; both codecs share them.
inline video_signal_description read_vui_signal(video_bit_reader& reader)
{
	video_signal_description result;
	if (!reader.flag())
	{
		result.parsed = reader.good();
		return result;
	}
	if (reader.flag() && reader.bits(8) == 255)
		reader.bits(32);
	if (reader.flag())
		reader.flag();
	result.signal_type_present = reader.flag();
	if (result.signal_type_present)
	{
		reader.bits(3);
		result.full_range = reader.flag();
		result.colour_description_present = reader.flag();
		if (result.colour_description_present)
		{
			result.primaries = static_cast<std::uint8_t>(reader.bits(8));
			result.transfer = static_cast<std::uint8_t>(reader.bits(8));
			result.matrix = static_cast<std::uint8_t>(reader.bits(8));
		}
	}
	result.parsed = reader.good();
	return result;
}

// `nal` starts at the one-byte H.264 NAL header.
inline video_signal_description read_h264_sps_signal(const std::uint8_t* nal, std::size_t size)
{
	if (size < 4 || (nal[0] & 0x1F) != 7)
		return {};
	video_bit_reader reader(nal + 1, size - 1);
	const auto profile = reader.bits(8);
	reader.bits(16);
	reader.exp_golomb();
	if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
		profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
		profile == 139 || profile == 134 || profile == 135)
	{
		if (reader.exp_golomb() == 3)
			reader.flag();
		reader.exp_golomb();
		reader.exp_golomb();
		reader.flag();
		if (reader.flag())
			return {};
	}
	reader.exp_golomb();
	const auto order_type = reader.exp_golomb();
	if (order_type == 0)
		reader.exp_golomb();
	else if (order_type == 1)
		return {};
	reader.exp_golomb();
	reader.flag();
	reader.exp_golomb();
	reader.exp_golomb();
	if (!reader.flag())
		reader.flag();
	reader.flag();
	if (reader.flag())
		for (int edge = 0; edge < 4; ++edge)
			reader.exp_golomb();
	return reader.good() ? read_vui_signal(reader) : video_signal_description{};
}

// `nal` starts at the two-byte HEVC NAL header.
inline video_signal_description read_hevc_sps_signal(const std::uint8_t* nal, std::size_t size)
{
	if (size < 4 || ((nal[0] >> 1) & 0x3F) != 33)
		return {};
	video_bit_reader reader(nal + 2, size - 2);
	reader.bits(4);
	if (reader.bits(3) != 0)
		return {};
	reader.flag();
	// General profile_tier_level: 88 bits, then the 8-bit level.
	reader.bits(24);
	reader.bits(32);
	reader.bits(32);
	reader.bits(8);
	reader.exp_golomb();
	if (reader.exp_golomb() == 3)
		reader.flag();
	reader.exp_golomb();
	reader.exp_golomb();
	if (reader.flag())
		for (int edge = 0; edge < 4; ++edge)
			reader.exp_golomb();
	reader.exp_golomb();
	reader.exp_golomb();
	reader.exp_golomb();
	reader.flag();
	for (int field = 0; field < 3; ++field)
		reader.exp_golomb();
	for (int field = 0; field < 6; ++field)
		reader.exp_golomb();
	if (reader.flag() && reader.flag())
		return {};
	reader.flag();
	reader.flag();
	if (reader.flag())
	{
		reader.bits(8);
		reader.exp_golomb();
		reader.exp_golomb();
		reader.flag();
	}
	const auto sets = reader.exp_golomb();
	for (std::uint32_t set = 0; set < sets && reader.good(); ++set)
	{
		if (set != 0 && reader.flag())
			return {};
		const auto pictures = reader.exp_golomb() + reader.exp_golomb();
		for (std::uint32_t picture = 0; picture < pictures && reader.good(); ++picture)
		{
			reader.exp_golomb();
			reader.flag();
		}
	}
	if (reader.flag() && reader.exp_golomb() != 0)
		return {};
	reader.flag();
	reader.flag();
	return reader.good() ? read_vui_signal(reader) : video_signal_description{};
}

// Finds the SPS in Annex B data (start-code separated NAL units, as in
// MF_MT_MPEG_SEQUENCE_HEADER) and reads its colour signalling.
inline video_signal_description read_annex_b_signal(const std::uint8_t* data, std::size_t size,
	bool hevc)
{
	const auto start_code = [&](std::size_t index)
	{
		return index + 3 <= size && data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1;
	};
	for (std::size_t index = 0; index + 3 <= size; ++index)
	{
		if (!start_code(index))
			continue;
		const std::size_t begin = index + 3;
		std::size_t end = begin;
		// A four-byte start code leaves a zero on this unit; the VUI comes first.
		while (end < size && !start_code(end))
			++end;
		const auto result = hevc ? read_hevc_sps_signal(data + begin, end - begin)
			: read_h264_sps_signal(data + begin, end - begin);
		if (result.parsed)
			return result;
		index = end - 1;
	}
	return {};
}

#endif
