#pragma once
#ifndef SAFC_VIDEO_FRAME_CONVERSION
#define SAFC_VIDEO_FRAME_CONVERSION

// Converts the exporter's top-down BGRA frames to the YUV layouts Media
// Foundation encoders accept. Doing it here, instead of in the pipeline's
// colour converter, fixes the matrix, range and chroma reduction, and makes
// 10-bit and 4:2:2/4:4:4 input possible: the converter offers neither from RGB.

#include "simple_player_video_export.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

enum class video_yuv_layout : std::uint8_t
{
	nv12,	// 8-bit 4:2:0, Y plane then interleaved UV
	p010,	// 10-bit 4:2:0, as NV12 with 16-bit samples, value in the high bits
	yuy2,	// 8-bit 4:2:2 packed Y0 U Y1 V
	y210,	// 10-bit 4:2:2 packed, 16-bit samples, value in the high bits
	ayuv,	// 8-bit 4:4:4 packed bytes V U Y A
	y410,	// 10-bit 4:4:4 packed U | Y << 10 | V << 20 | A << 30
};

inline video_yuv_layout video_yuv_layout_for(simple_player_video_chroma chroma,
	std::uint32_t bit_depth) noexcept
{
	const bool deep = bit_depth > 8;
	switch (chroma)
	{
	case simple_player_video_chroma::yuv422:
		return deep ? video_yuv_layout::y210 : video_yuv_layout::yuy2;
	case simple_player_video_chroma::yuv444:
		return deep ? video_yuv_layout::y410 : video_yuv_layout::ayuv;
	default:
		return deep ? video_yuv_layout::p010 : video_yuv_layout::nv12;
	}
}

// Bytes in one row of the first plane; Media Foundation's default stride.
inline std::uint32_t video_yuv_stride(video_yuv_layout layout, std::uint32_t width) noexcept
{
	switch (layout)
	{
	case video_yuv_layout::nv12: return width;
	case video_yuv_layout::p010: return width * 2;
	case video_yuv_layout::yuy2: return width * 2;
	default: return width * 4;
	}
}

inline std::size_t video_yuv_frame_bytes(video_yuv_layout layout,
	std::uint32_t width, std::uint32_t height) noexcept
{
	const auto plane = static_cast<std::size_t>(video_yuv_stride(layout, width)) * height;
	return layout == video_yuv_layout::nv12 || layout == video_yuv_layout::p010
		? plane + plane / 2 : plane;
}

// Fixed-point (16 fractional bits) coefficients for one matrix, range and depth.
class video_yuv_converter
{
public:
	video_yuv_converter(video_yuv_layout layout, simple_player_video_matrix matrix,
		bool full_range, simple_player_video_chroma_filter filter) noexcept
		: layout_(layout), filter_(filter)
	{
		double kr = 0.2126, kb = 0.0722;
		if (matrix == simple_player_video_matrix::bt601)
			kr = 0.299, kb = 0.114;
		else if (matrix == simple_player_video_matrix::bt2020)
			kr = 0.2627, kb = 0.0593;
		const int bits = layout == video_yuv_layout::nv12 || layout == video_yuv_layout::yuy2 ||
			layout == video_yuv_layout::ayuv ? 8 : 10;
		auto& k = coefficients_;
		k.maximum = (1 << bits) - 1;
		const int shift = bits - 8;
		const double y_scale = full_range ? k.maximum : 219 << shift;
		const double c_scale = full_range ? k.maximum : 224 << shift;
		k.y_offset = full_range ? 0 : 16 << shift;
		k.c_offset = 1 << (bits - 1);

		const auto fixed = [](double value)
		{
			return static_cast<std::int32_t>(value * 65536.0 + (value < 0 ? -0.5 : 0.5));
		};
		k.y_r = fixed(y_scale * kr / 255.0);
		k.y_b = fixed(y_scale * kb / 255.0);
		// The three luma terms sum to the full scale, so white maps exactly.
		k.y_g = fixed(y_scale / 255.0) - k.y_r - k.y_b;
		k.cb_r = fixed(-c_scale * kr / (2.0 * (1.0 - kb)) / 255.0);
		k.cb_b = fixed(c_scale * 0.5 / 255.0);
		// Chroma terms sum to zero, so every grey is neutral.
		k.cb_g = -k.cb_r - k.cb_b;
		k.cr_r = k.cb_b;
		k.cr_b = fixed(-c_scale * kb / (2.0 * (1.0 - kr)) / 255.0);
		k.cr_g = -k.cr_r - k.cr_b;
	}

	// Converts rows [first_row, first_row + rows) of a frame whose width and
	// height are even. For 4:2:0 first_row is even too, so independent row
	// ranges can convert on separate threads.
	void convert(const std::uint8_t* bgra, std::uint32_t width, std::uint32_t height,
		std::uint8_t* destination, std::uint32_t first_row, std::uint32_t rows) const noexcept
	{
		const frame_rows frame{bgra, width, height, destination, first_row,
			(std::min)(height, first_row + rows)};
		using filter = simple_player_video_chroma_filter;
		// The layout and filter are fixed per frame, so each pairing gets its
		// own loop without per-sample branches.
		const auto filtered = [&](auto&& run)
		{
			switch (filter_)
			{
			case filter::cosited: run(std::integral_constant<filter, filter::cosited>{}); break;
			case filter::nearest: run(std::integral_constant<filter, filter::nearest>{}); break;
			default: run(std::integral_constant<filter, filter::box>{}); break;
			}
		};
		switch (layout_)
		{
		case video_yuv_layout::nv12:
			filtered([&](auto chroma) { planar_420<false, decltype(chroma)::value>(frame); });
			break;
		case video_yuv_layout::p010:
			filtered([&](auto chroma) { planar_420<true, decltype(chroma)::value>(frame); });
			break;
		case video_yuv_layout::yuy2:
			filtered([&](auto chroma) { packed_422<false, decltype(chroma)::value>(frame); });
			break;
		case video_yuv_layout::y210:
			filtered([&](auto chroma) { packed_422<true, decltype(chroma)::value>(frame); });
			break;
		case video_yuv_layout::ayuv:
			packed_444<false>(frame);
			break;
		case video_yuv_layout::y410:
			packed_444<true>(frame);
			break;
		}
	}

private:
	using filter_type = simple_player_video_chroma_filter;

	struct frame_rows
	{
		const std::uint8_t* bgra;
		std::uint32_t width;
		std::uint32_t height;
		std::uint8_t* destination;
		std::uint32_t first;
		std::uint32_t last;
	};

	// The loops copy these to a local first: they write through byte pointers,
	// which may alias members, so the compiler would reload members per pixel.
	struct coefficients
	{
		std::int32_t maximum = 255;
		std::int32_t y_offset = 16;
		std::int32_t c_offset = 128;
		std::int32_t y_r = 0, y_g = 0, y_b = 0;
		std::int32_t cb_r = 0, cb_g = 0, cb_b = 0;
		std::int32_t cr_r = 0, cr_g = 0, cr_b = 0;

		std::int32_t clamp(std::int32_t value) const noexcept
		{
			return (std::min)((std::max)(value, 0), maximum);
		}

		std::int32_t luma(const std::uint8_t* pixel) const noexcept
		{
			const std::int32_t sum = y_r * pixel[2] + y_g * pixel[1] + y_b * pixel[0];
			return clamp(y_offset + ((sum + 32768) >> 16));
		}

		// The sums carry 16 fractional bits plus `shift - 16` bits of filter weight.
		void chroma(std::int32_t r, std::int32_t g, std::int32_t b, int shift,
			std::int32_t& cb, std::int32_t& cr) const noexcept
		{
			const std::int32_t half = 1 << (shift - 1);
			cb = clamp(c_offset + ((cb_r * r + cb_g * g + cb_b * b + half) >> shift));
			cr = clamp(c_offset + ((cr_r * r + cr_g * g + cr_b * b + half) >> shift));
		}
	};

	// log2 of a block's total filter weight over one row.
	template<filter_type Filter>
	static constexpr int row_weight_bits() noexcept
	{
		return Filter == filter_type::cosited ? 2 : Filter == filter_type::box ? 1 : 0;
	}

	// Adds one row of the block at even column x: box takes x and x + 1,
	// cosited [1 2 1] around x, nearest x alone.
	template<filter_type Filter>
	static void add_row(const std::uint8_t* line, std::uint32_t x,
		std::int32_t& r, std::int32_t& g, std::int32_t& b) noexcept
	{
		const auto* pixel = line + static_cast<std::size_t>(x) * 4;
		if constexpr (Filter == filter_type::nearest)
		{
			b += pixel[0];
			g += pixel[1];
			r += pixel[2];
		}
		else if constexpr (Filter == filter_type::box)
		{
			b += pixel[0] + pixel[4];
			g += pixel[1] + pixel[5];
			r += pixel[2] + pixel[6];
		}
		else
		{
			const auto* left = x ? pixel - 4 : pixel;
			b += left[0] + 2 * pixel[0] + pixel[4];
			g += left[1] + 2 * pixel[1] + pixel[5];
			r += left[2] + 2 * pixel[2] + pixel[6];
		}
	}

	// Writes sample `index` of a row as a byte, or as a 16-bit word with the
	// 10-bit value in its high bits.
	template<bool Deep>
	static void store(std::uint8_t* row, std::size_t index, std::int32_t value) noexcept
	{
		if constexpr (Deep)
		{
			const auto word = static_cast<std::uint16_t>(value << 6);
			std::memcpy(row + index * 2, &word, 2);
		}
		else
			row[index] = static_cast<std::uint8_t>(value);
	}

	template<bool Deep, filter_type Filter>
	void planar_420(const frame_rows& frame) const noexcept
	{
		const auto k = coefficients_;
		const std::size_t source_stride = static_cast<std::size_t>(frame.width) * 4;
		const std::size_t stride = video_yuv_stride(layout_, frame.width);
		auto* chroma_plane = frame.destination + stride * frame.height;
		// Nearest reads only the top row of each block.
		constexpr int shift = 16 + row_weight_bits<Filter>() +
			(Filter == filter_type::nearest ? 0 : 1);
		for (std::uint32_t y = frame.first; y < frame.last; y += 2)
		{
			const auto* top = frame.bgra + source_stride * y;
			const auto* bottom = top + source_stride;
			auto* luma_top = frame.destination + stride * y;
			auto* luma_bottom = luma_top + stride;
			auto* chroma_row = chroma_plane + stride * (y / 2);
			for (std::uint32_t x = 0; x < frame.width; x += 2)
			{
				const auto* pixel = top + static_cast<std::size_t>(x) * 4;
				const auto* below = bottom + static_cast<std::size_t>(x) * 4;
				store<Deep>(luma_top, x, k.luma(pixel));
				store<Deep>(luma_top, x + 1, k.luma(pixel + 4));
				store<Deep>(luma_bottom, x, k.luma(below));
				store<Deep>(luma_bottom, x + 1, k.luma(below + 4));
				std::int32_t r = 0, g = 0, b = 0;
				add_row<Filter>(top, x, r, g, b);
				if constexpr (Filter != filter_type::nearest)
					add_row<Filter>(bottom, x, r, g, b);
				std::int32_t cb = 0, cr = 0;
				k.chroma(r, g, b, shift, cb, cr);
				store<Deep>(chroma_row, x, cb);
				store<Deep>(chroma_row, x + 1, cr);
			}
		}
	}

	template<bool Deep, filter_type Filter>
	void packed_422(const frame_rows& frame) const noexcept
	{
		const auto k = coefficients_;
		const std::size_t source_stride = static_cast<std::size_t>(frame.width) * 4;
		const std::size_t stride = video_yuv_stride(layout_, frame.width);
		constexpr int shift = 16 + row_weight_bits<Filter>();
		for (std::uint32_t y = frame.first; y < frame.last; ++y)
		{
			const auto* row = frame.bgra + source_stride * y;
			auto* out = frame.destination + stride * y;
			for (std::uint32_t x = 0; x < frame.width; x += 2)
			{
				const auto* pixel = row + static_cast<std::size_t>(x) * 4;
				std::int32_t r = 0, g = 0, b = 0;
				add_row<Filter>(row, x, r, g, b);
				std::int32_t cb = 0, cr = 0;
				k.chroma(r, g, b, shift, cb, cr);
				const std::size_t sample = static_cast<std::size_t>(x) * 2;
				store<Deep>(out, sample, k.luma(pixel));
				store<Deep>(out, sample + 1, cb);
				store<Deep>(out, sample + 2, k.luma(pixel + 4));
				store<Deep>(out, sample + 3, cr);
			}
		}
	}

	template<bool Deep>
	void packed_444(const frame_rows& frame) const noexcept
	{
		const auto k = coefficients_;
		const std::size_t source_stride = static_cast<std::size_t>(frame.width) * 4;
		const std::size_t stride = video_yuv_stride(layout_, frame.width);
		for (std::uint32_t y = frame.first; y < frame.last; ++y)
		{
			const auto* row = frame.bgra + source_stride * y;
			auto* out = frame.destination + stride * y;
			for (std::uint32_t x = 0; x < frame.width; ++x)
			{
				const auto* pixel = row + static_cast<std::size_t>(x) * 4;
				std::int32_t cb = 0, cr = 0;
				k.chroma(pixel[2], pixel[1], pixel[0], 16, cb, cr);
				const auto yv = k.luma(pixel);
				auto* target = out + static_cast<std::size_t>(x) * 4;
				if constexpr (Deep)
				{
					const std::uint32_t packed = static_cast<std::uint32_t>(cb) |
						(static_cast<std::uint32_t>(yv) << 10) |
						(static_cast<std::uint32_t>(cr) << 20) | (3U << 30);
					std::memcpy(target, &packed, 4);
				}
				else
				{
					target[0] = static_cast<std::uint8_t>(cr);
					target[1] = static_cast<std::uint8_t>(cb);
					target[2] = static_cast<std::uint8_t>(yv);
					target[3] = 0xFF;
				}
			}
		}
	}

	video_yuv_layout layout_;
	filter_type filter_;
	coefficients coefficients_;
};

#endif
