#define NOMINMAX

#ifdef _WIN32
#include <Windows.h>
#endif
#include <GL/gl.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#include "simple_player_video_export.h"
#include "simple_player.h"
#include "video_frame_conversion.h"
#include "video_parameter_sets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#ifdef SAFC_WITH_SYNCORE
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

#include <core.h>
#include <mastering.h>
#include <smf.h>
#include <smf_renderer.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "oleaut32.lib")

namespace
{
using Microsoft::WRL::ComPtr;

constexpr LONGLONG mf_second = 10'000'000LL;
constexpr std::uint64_t export_lookahead_guard_us = 500'000ULL;
constexpr std::uint32_t preview_max_width = 320;
constexpr std::uint32_t preview_max_height = 180;

void require_hr(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		std::ostringstream text;
		text << operation << " failed (0x" << std::hex <<
			static_cast<std::uint32_t>(result) << ')';
		throw std::runtime_error(text.str());
	}
}

std::string first_smf_error(const std::vector<safsyn::SmfDiagnostic>& diagnostics,
	const char* fallback)
{
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == safsyn::SmfDiagnosticSeverity::Error)
			return diagnostic.message;
	return fallback;
}

std::vector<std::uint8_t> read_file_bytes(const std::wstring& path)
{
	std::ifstream source(std::filesystem::path(path), std::ios::binary | std::ios::ate);
	if (!source)
		throw std::runtime_error("could not open MIDI file");
	const auto length = source.tellg();
	if (length < 0 || static_cast<std::uint64_t>(length) > SIZE_MAX)
		throw std::runtime_error("invalid MIDI file size");
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	source.seekg(0);
	if (!bytes.empty() && !source.read(reinterpret_cast<char*>(bytes.data()), length))
		throw std::runtime_error("could not read MIDI file");
	return bytes;
}

std::shared_ptr<safsyn::Soundfont> load_bank(const std::wstring& path)
{
	auto bank = std::make_shared<safsyn::Soundfont>();
	if (path.empty())
	{
		constexpr std::uint32_t length = 2048;
		bank->sfz_pcm.emplace_back(length);
		for (std::size_t i = 0; i < length; ++i)
			bank->sfz_pcm[0][i] = static_cast<std::int16_t>(
				std::sin(i * 6.283185307179586 / length) * 12000);
		safsyn::SampleRegion region;
		region.pcm = bank->sfz_pcm[0].data();
		region.pcm_len = length;
		region.root_key = 69;
		region.sample_rate = length * 440;
		region.loop_mode = safsyn::LoopMode::Forward;
		region.loop_end = length;
		region.attack = 0.005f;
		region.release = 0.1f;
		bank->regions.push_back(region);
		return bank;
	}

	BOOL substituted = FALSE;
	const int count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
		path.c_str(), -1, nullptr, 0, nullptr, &substituted);
	std::string native(static_cast<std::size_t>(std::max(count, 0)), '\0');
	if (!count || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
		path.c_str(), -1, native.data(), count, nullptr, &substituted) ||
		substituted)
	{
		throw std::runtime_error(
			"bank path is not supported by the loader; use a path in the Windows system code page");
	}

	const auto extension = std::filesystem::path(path).extension().wstring();
	const bool ok = _wcsicmp(extension.c_str(), L".sfz") == 0
		? safsyn::load_sfz(native.c_str(), *bank)
		: _wcsicmp(extension.c_str(), L".sf2") == 0 &&
			safsyn::load_sf2(native.c_str(), *bank);
	if (!ok || bank->regions.empty())
		throw std::runtime_error("could not load SF2/SFZ sound bank");
	return bank;
}

safsyn::PhaseSettings phase_settings_from_preferences(
	const syncore_preferences& preferences)
{
	safsyn::PhaseSettings phase;
	switch (preferences.phase_mode)
	{
	case syncore_phase_mode::coherent:
		phase.mode = safsyn::PhaseMode::Coherent;
		break;
	case syncore_phase_mode::analytic:
		phase.mode = safsyn::PhaseMode::Analytic;
		break;
	}
	return phase;
}

std::size_t render_threads_from_preferences(const syncore_preferences& preferences)
{
	if (preferences.render_threads != 0)
		return preferences.render_threads;
	const auto cpus = std::thread::hardware_concurrency();
	return (std::min)(std::size_t{16}, std::size_t{cpus > 2 ? cpus - 2 : 1});
}

safsyn::MasteringSettings mastering_from_preferences(
	const syncore_preferences& preferences)
{
	safsyn::MasteringSettings mastering;
	mastering.output_gain_db = preferences.output_gain_db;
	mastering.limiter_enabled = preferences.limiter_enabled;
	mastering.limiter_ceiling_db = -1.0;
	mastering.limiter_lookahead_ms = 5.0;
	mastering.limiter_release_ms = 100.0;
	return mastering;
}

void validate_settings(const simple_player_video_settings& settings,
	const syncore_preferences& preferences)
{
	auto problem = simple_player_video_settings_problem(settings, preferences);
	if (!problem.empty())
		throw std::runtime_error(problem);
}

bool asf_container(simple_player_video_codec codec) noexcept
{
	return codec == simple_player_video_codec::wmv9 || codec == simple_player_video_codec::vc1;
}

const char* container_name(simple_player_video_codec codec) noexcept
{
	return asf_container(codec) ? "WMV" : "MP4";
}

std::wstring normalized_path(const std::wstring& value)
{
	std::error_code error;
	auto path = std::filesystem::absolute(std::filesystem::path(value), error);
	if (error)
		path = std::filesystem::path(value);
	auto normalized = path.lexically_normal().wstring();
	if (!normalized.empty())
		CharLowerBuffW(normalized.data(), static_cast<DWORD>(normalized.size()));
	return normalized;
}

bool same_path(const std::wstring& left, const std::wstring& right)
{
	if (left.empty() || right.empty())
		return false;
	std::error_code error;
	if (std::filesystem::exists(left, error) && !error &&
		std::filesystem::exists(right, error) && !error &&
		std::filesystem::equivalent(left, right, error) && !error)
		return true;
	return normalized_path(left) == normalized_path(right);
}

void validate_paths(const std::wstring& source_path,
	const std::wstring& output_path, const std::wstring& bank_path,
	simple_player_video_codec codec)
{
	if (source_path.empty() || output_path.empty())
		throw std::runtime_error("missing MIDI source or output path");
	if (same_path(source_path, output_path))
		throw std::runtime_error("output path must not overwrite the MIDI source");
	if (!bank_path.empty() && same_path(bank_path, output_path))
		throw std::runtime_error("output path must not overwrite the sound bank");
	if (_wcsicmp(std::filesystem::path(output_path).extension().c_str(),
		simple_player_video_extension(codec)) != 0)
		throw std::runtime_error(std::string(simple_player_video_codec_name(codec)) +
			" video output path must use the " + (asf_container(codec) ? ".wmv" : ".mp4") +
			" extension");
	const auto parent = std::filesystem::path(output_path).parent_path();
	std::error_code error;
	if (!parent.empty() && !std::filesystem::is_directory(parent, error))
		throw std::runtime_error("video output directory does not exist");
}

// Media Foundation picks the container from the extension, so the partial
// file keeps the destination's.
class temporary_video_output
{
public:
	explicit temporary_video_output(std::wstring destination)
		: destination_(std::move(destination))
	{
		const auto final_path = std::filesystem::path(destination_);
		const auto directory = final_path.parent_path();
		const auto stem = final_path.stem().wstring();
		const auto extension = final_path.extension().wstring();
		for (std::uint32_t attempt = 0; attempt < 1024; ++attempt)
		{
			const auto filename = stem + L".safc-part-" +
				std::to_wstring(GetCurrentProcessId()) + L"-" +
				std::to_wstring(attempt) + extension;
			temporary_ = (directory / filename).wstring();
			HANDLE file = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
			if (file != INVALID_HANDLE_VALUE)
			{
				CloseHandle(file);
				return;
			}
			if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
				break;
		}
		throw std::runtime_error("could not reserve a temporary video output file");
	}

	~temporary_video_output()
	{
		if (!committed_ && !temporary_.empty())
			DeleteFileW(temporary_.c_str());
	}

	const std::wstring& path() const noexcept { return temporary_; }

	void commit()
	{
		if (!MoveFileExW(temporary_.c_str(), destination_.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("could not replace the destination with the completed video");
		committed_ = true;
	}

private:
	std::wstring destination_;
	std::wstring temporary_;
	bool committed_ = false;
};

LONGLONG frame_to_time(std::uint64_t frame, std::uint32_t sample_rate)
{
	const auto seconds = frame / sample_rate;
	const auto remainder = frame % sample_rate;
	if (seconds > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()) /
		static_cast<std::uint64_t>(mf_second))
		throw std::runtime_error("audio timestamp exceeds the MP4 time range");
	const auto result = seconds * static_cast<std::uint64_t>(mf_second) +
		(remainder * static_cast<std::uint64_t>(mf_second)) / sample_rate;
	if (result > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()))
		throw std::runtime_error("audio timestamp exceeds the MP4 time range");
	return static_cast<LONGLONG>(result);
}

LONGLONG video_sample_time(std::uint64_t frame, std::uint32_t fps)
{
	return frame_to_time(frame, fps);
}

std::uint64_t seconds_to_frames(double seconds, std::uint32_t sample_rate)
{
	return static_cast<std::uint64_t>(std::llround(seconds * sample_rate));
}

std::uint64_t seconds_to_us(double seconds)
{
	return static_cast<std::uint64_t>(std::llround(seconds * 1'000'000.0));
}

void configure_draw_data(simple_player::draw_data& data,
	const simple_player_video_settings& settings)
{
	constexpr float virtual_width = 400.0f;
	const float virtual_height = virtual_width *
		(static_cast<float>(settings.height) / static_cast<float>(settings.width));
	const float keyboard_height = std::min(40.0f, virtual_height * 0.42f);
	const float notes_height = std::max(virtual_height - keyboard_height, 20.0f);
	constexpr float black_relative_height = 22.5f / 40.0f;

	data.enable_simulated_lag = false;
	data.remove_overlaps = settings.remove_overlaps;
	data.scroll_window_us = seconds_to_us(settings.visible_seconds);
	data.reinit(virtual_width, notes_height, keyboard_height,
		keyboard_height * black_relative_height, 0.0f);
	data.move(-0.5f * virtual_width, 0.5f * keyboard_height);
}

struct com_mta_scope
{
	bool coinitialized = false;

	com_mta_scope()
	{
		const HRESULT coinit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(coinit))
			require_hr(coinit, "COM initialization");
		coinitialized = true;
	}

	~com_mta_scope()
	{
		if (coinitialized)
			CoUninitialize();
	}
};

struct media_runtime
{
	com_mta_scope com;
	bool mf_started = false;

	media_runtime()
	{
		require_hr(MFStartup(MF_VERSION), "Media Foundation startup");
		mf_started = true;
	}

	~media_runtime()
	{
		if (mf_started)
			MFShutdown();
	}
};

std::string narrow_text(const wchar_t* text)
{
	const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (count <= 1)
		return {};
	std::string result(static_cast<std::size_t>(count), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
	result.pop_back();
	return result;
}

std::string hr_text(HRESULT result)
{
	std::ostringstream text;
	text << " (0x" << std::hex << static_cast<std::uint32_t>(result) << ')';
	return text.str();
}

GUID video_subtype(simple_player_video_codec codec) noexcept
{
	switch (codec)
	{
	case simple_player_video_codec::hevc: return MFVideoFormat_HEVC;
	case simple_player_video_codec::av1: return MFVideoFormat_AV1;
	case simple_player_video_codec::wmv9: return MFVideoFormat_WMV3;
	case simple_player_video_codec::vc1: return MFVideoFormat_WVC1;
	default: return MFVideoFormat_H264;
	}
}

GUID yuv_subtype(video_yuv_layout layout) noexcept
{
	switch (layout)
	{
	case video_yuv_layout::p010: return MFVideoFormat_P010;
	case video_yuv_layout::yuy2: return MFVideoFormat_YUY2;
	case video_yuv_layout::y210: return MFVideoFormat_Y210;
	case video_yuv_layout::ayuv: return MFVideoFormat_AYUV;
	case video_yuv_layout::y410: return MFVideoFormat_Y410;
	default: return MFVideoFormat_NV12;
	}
}

std::string format_name(const simple_player_video_settings& settings)
{
	const char* chroma = settings.chroma == simple_player_video_chroma::yuv444 ? "4:4:4" :
		settings.chroma == simple_player_video_chroma::yuv422 ? "4:2:2" : "4:2:0";
	return std::string(chroma) + " " + std::to_string(settings.bit_depth) + "-bit";
}

// MF_MT_MPEG2_PROFILE for the output type, or 0 to leave the encoder's
// default. HEVC needs it: without a 4:4:4 profile the encoder quietly
// subsamples AYUV input to 4:2:0.
UINT32 video_profile(const simple_player_video_settings& settings) noexcept
{
	const bool deep = settings.bit_depth > 8;
	switch (settings.codec)
	{
	case simple_player_video_codec::h264:
		switch (settings.h264_profile)
		{
		case simple_player_video_h264_profile::baseline: return eAVEncH264VProfile_Base;
		case simple_player_video_h264_profile::main: return eAVEncH264VProfile_Main;
		case simple_player_video_h264_profile::high: return eAVEncH264VProfile_High;
		default: return 0;
		}
	case simple_player_video_codec::hevc:
		switch (settings.chroma)
		{
		case simple_player_video_chroma::yuv422: return eAVEncH265VProfile_Main_422_10;
		case simple_player_video_chroma::yuv444:
			return deep ? eAVEncH265VProfile_Main_444_10 : eAVEncH265VProfile_Main_444_8;
		default: return deep ? eAVEncH265VProfile_Main_420_10 : eAVEncH265VProfile_Main_420_8;
		}
	case simple_player_video_codec::av1:
		return deep ? eAVEncAV1VProfile_Main_420_10 : eAVEncAV1VProfile_Main_420_8;
	default:
		return 0;
	}
}

void set_color_attributes(IMFMediaType* type, const simple_player_video_settings& settings)
{
	const UINT32 matrix = settings.matrix == simple_player_video_matrix::bt601
		? MFVideoTransferMatrix_BT601 : settings.matrix == simple_player_video_matrix::bt2020
			? MFVideoTransferMatrix_BT2020_10 : MFVideoTransferMatrix_BT709;
	require_hr(type->SetUINT32(MF_MT_YUV_MATRIX, matrix), "setting the YUV matrix");
	require_hr(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, settings.full_range
		? MFNominalRange_0_255 : MFNominalRange_16_235), "setting the video range");
	require_hr(type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709),
		"setting the video primaries");
	require_hr(type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709),
		"setting the transfer function");
}

void set_frame_attributes(IMFMediaType* type, const simple_player_video_settings& settings)
{
	require_hr(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
		"setting progressive video");
	require_hr(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, settings.width, settings.height),
		"setting the video frame size");
	require_hr(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, settings.fps, 1),
		"setting the video frame rate");
	require_hr(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
		"setting the video pixel aspect");
}

// One ICodecAPI property, passed to the encoder through the sink writer's
// encoding parameters and read back once the encoder exists.
struct encoder_property
{
	GUID key;
	std::uint64_t value = 0;
	bool wide = false;
	const char* label = "";
	// Encoders round some values; differences up to this are not reported.
	std::uint64_t tolerance = 0;
};

std::vector<encoder_property> encoder_properties(const simple_player_video_settings& settings)
{
	using rate_control = simple_player_video_rate_control;
	std::vector<encoder_property> properties;
	const auto add = [&](const GUID& key, std::uint64_t value, const char* label,
		bool wide = false, std::uint64_t tolerance = 0)
	{
		properties.push_back({key, value, wide, label, tolerance});
	};
	const bool constant_qp = settings.rate_control == rate_control::constant_qp;
	add(CODECAPI_AVEncCommonRateControlMode, constant_qp
		? static_cast<std::uint64_t>(eAVEncCommonRateControlMode_Quality)
		: static_cast<std::uint64_t>(settings.rate_control), "rate control mode");
	if (constant_qp)
	{
		if (settings.qp_p_offset == 0 && settings.qp_b_offset == 0)
			add(CODECAPI_AVEncVideoEncodeQP, settings.qp, "QP", true);
		else
		{
			// I, P and B QPs in the low, second and third 16-bit fields.
			const auto qp = static_cast<std::int64_t>(settings.qp);
			add(CODECAPI_AVEncVideoEncodeFrameTypeQP, static_cast<std::uint64_t>(qp) |
				(static_cast<std::uint64_t>(qp + settings.qp_p_offset) << 16) |
				(static_cast<std::uint64_t>(qp + settings.qp_b_offset) << 32),
				"per-frame-type QP", true);
		}
	}
	else if (settings.rate_control == rate_control::quality)
		add(CODECAPI_AVEncCommonQuality, settings.quality, "quality", false, 5);
	else
	{
		add(CODECAPI_AVEncCommonMeanBitRate, settings.video_bitrate_kbps * 1000ULL,
			"mean bitrate");
		if (settings.rate_control == rate_control::peak_constrained_vbr &&
			settings.peak_bitrate_kbps != 0)
			add(CODECAPI_AVEncCommonMaxBitRate, settings.peak_bitrate_kbps * 1000ULL,
				"peak bitrate");
	}
	if (settings.buffer_kbits != 0)
		add(CODECAPI_AVEncCommonBufferSize, settings.buffer_kbits * 1000ULL, "VBV buffer size");
	const auto optional = [&](const GUID& key, std::int32_t value, const char* label,
		std::uint64_t tolerance = 0)
	{
		if (value != simple_player_video_encoder_default)
			add(key, static_cast<std::uint64_t>(value), label, false, tolerance);
	};
	optional(CODECAPI_AVEncVideoMinQP, settings.min_qp, "minimum QP");
	optional(CODECAPI_AVEncVideoMaxQP, settings.max_qp, "maximum QP");
	optional(CODECAPI_AVEncCommonQualityVsSpeed, settings.quality_vs_speed,
		"speed/quality preset", 5);
	optional(CODECAPI_AVEncMPVGOPSize, settings.gop_size, "keyframe interval");
	optional(CODECAPI_AVEncMPVDefaultBPictureCount, settings.b_frames, "B-frame count");
	optional(CODECAPI_AVEncVideoMaxNumRefFrame, settings.max_reference_frames,
		"reference frame count");
	optional(CODECAPI_AVEncNumWorkerThreads, settings.worker_threads, "worker thread count");
	if (settings.codec == simple_player_video_codec::h264)
		optional(CODECAPI_AVEncH264CABACEnable, settings.cabac, "CABAC");
	optional(CODECAPI_AVLowLatencyMode, settings.low_latency, "low-latency mode");
	return properties;
}

ComPtr<IMFAttributes> encoding_parameters(const std::vector<encoder_property>& properties,
	const std::vector<std::size_t>& chosen)
{
	ComPtr<IMFAttributes> parameters;
	require_hr(MFCreateAttributes(parameters.GetAddressOf(), static_cast<UINT32>(chosen.size() + 1)),
		"creating encoder parameters");
	for (const auto index : chosen)
	{
		const auto& property = properties[index];
		require_hr(property.wide
			? parameters->SetUINT64(property.key, property.value)
			: parameters->SetUINT32(property.key, static_cast<UINT32>(property.value)),
			"setting an encoder parameter");
	}
	return parameters;
}

ComPtr<IMFAttributes> sink_writer_attributes(const simple_player_video_settings& settings)
{
	ComPtr<IMFAttributes> attributes;
	require_hr(MFCreateAttributes(attributes.GetAddressOf(), 2),
		"creating sink writer attributes");
	require_hr(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
		settings.hardware_encoder), "configuring hardware media transforms");
	if (settings.fast_start && !asf_container(settings.codec))
		require_hr(attributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, TRUE),
			"requesting the MP4 index first");
	return attributes;
}

ComPtr<IMFMediaType> video_output_type(const simple_player_video_settings& settings)
{
	ComPtr<IMFMediaType> type;
	require_hr(MFCreateMediaType(type.GetAddressOf()), "creating video output type");
	require_hr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "setting video major type");
	require_hr(type->SetGUID(MF_MT_SUBTYPE, video_subtype(settings.codec)),
		"setting the video codec");
	require_hr(type->SetUINT32(MF_MT_AVG_BITRATE, settings.video_bitrate_kbps * 1000U),
		"setting video bitrate");
	set_frame_attributes(type.Get(), settings);
	if (const auto profile = video_profile(settings))
		require_hr(type->SetUINT32(MF_MT_MPEG2_PROFILE, profile), "setting the video profile");
	set_color_attributes(type.Get(), settings);
	return type;
}

ComPtr<IMFMediaType> video_input_type(const simple_player_video_settings& settings)
{
	const auto layout = video_yuv_layout_for(settings.chroma, settings.bit_depth);
	ComPtr<IMFMediaType> type;
	require_hr(MFCreateMediaType(type.GetAddressOf()), "creating video input type");
	require_hr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
		"setting video input major type");
	require_hr(type->SetGUID(MF_MT_SUBTYPE, yuv_subtype(layout)), "setting the YUV input subtype");
	set_frame_attributes(type.Get(), settings);
	require_hr(type->SetUINT32(MF_MT_DEFAULT_STRIDE, video_yuv_stride(layout, settings.width)),
		"setting input video stride");
	require_hr(type->SetUINT32(MF_MT_SAMPLE_SIZE, static_cast<UINT32>(
		video_yuv_frame_bytes(layout, settings.width, settings.height))),
		"setting input video sample size");
	require_hr(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE),
		"marking input frames independent");
	set_color_attributes(type.Get(), settings);
	if (settings.chroma != simple_player_video_chroma::yuv444)
	{
		const UINT32 siting = settings.chroma_filter == simple_player_video_chroma_filter::cosited
			? MFVideoChromaSubsampling_MPEG2
			: settings.chroma_filter == simple_player_video_chroma_filter::nearest
				? MFVideoChromaSubsampling_Cosited : MFVideoChromaSubsampling_MPEG1;
		require_hr(type->SetUINT32(MF_MT_VIDEO_CHROMA_SITING, siting), "setting chroma siting");
	}
	return type;
}

// Configures a throwaway writer on memory with some of the encoder
// properties. An encoder keeps its first configuration and then accepts what
// it refused, so each diagnostic attempt needs a writer of its own.
HRESULT try_video_configuration(const std::wstring& output_path,
	const simple_player_video_settings& settings,
	const std::vector<encoder_property>& properties,
	const std::vector<std::size_t>& chosen) noexcept
{
	try
	{
		ComPtr<IStream> memory;
		ComPtr<IMFByteStream> stream;
		ComPtr<IMFSinkWriter> writer;
		DWORD index = 0;
		HRESULT result = CreateStreamOnHGlobal(nullptr, TRUE, memory.GetAddressOf());
		if (SUCCEEDED(result))
			result = MFCreateMFByteStreamOnStream(memory.Get(), stream.GetAddressOf());
		if (SUCCEEDED(result))
			result = MFCreateSinkWriterFromURL(output_path.c_str(), stream.Get(),
				sink_writer_attributes(settings).Get(), writer.GetAddressOf());
		if (SUCCEEDED(result))
			result = writer->AddStream(video_output_type(settings).Get(), &index);
		if (SUCCEEDED(result))
			result = writer->SetInputMediaType(index, video_input_type(settings).Get(),
				encoding_parameters(properties, chosen).Get());
		return result;
	}
	catch (...)
	{
		return E_FAIL;
	}
}

// The installed WMA encoder's stereo type closest to the requested bitrate.
ComPtr<IMFMediaType> wma_output_type(const GUID& subtype, const char* name,
	std::uint32_t sample_rate, std::uint32_t bits, std::uint32_t bitrate_kbps)
{
	ComPtr<IMFCollection> types;
	require_hr(MFTranscodeGetAudioOutputAvailableTypes(subtype, MFT_ENUM_FLAG_ALL, nullptr,
		types.GetAddressOf()), "listing WMA encoder output types");
	DWORD count = 0;
	require_hr(types->GetElementCount(&count), "counting WMA encoder output types");
	ComPtr<IMFMediaType> best;
	std::uint64_t best_distance = (std::numeric_limits<std::uint64_t>::max)();
	const std::int64_t wanted = static_cast<std::int64_t>(bitrate_kbps) * 125;
	for (DWORD index = 0; index < count; ++index)
	{
		ComPtr<IUnknown> element;
		ComPtr<IMFMediaType> type;
		if (FAILED(types->GetElement(index, element.GetAddressOf())) ||
			FAILED(element.As(&type)))
			continue;
		UINT32 channels = 0, rate = 0, sample_bits = 0, bytes_per_second = 0;
		type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
		type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
		type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &sample_bits);
		type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes_per_second);
		if (channels != 2 || rate != sample_rate || sample_bits != bits)
			continue;
		const auto distance = static_cast<std::uint64_t>(
			std::llabs(static_cast<std::int64_t>(bytes_per_second) - wanted));
		if (distance < best_distance)
		{
			best_distance = distance;
			best = type;
		}
	}
	if (!best)
		throw std::runtime_error(std::string("the ") + name + " encoder has no stereo " +
			std::to_string(sample_rate) + " Hz " + std::to_string(bits) + "-bit type");
	return best;
}

class media_foundation_writer
{
public:
	~media_foundation_writer()
	{
		abort();
		join_write_thread();
	}

	// `output_rate` is the encoded audio rate; SYNCore's PCM arrives at
	// `pcm_sample_rate`. Without audio only a video stream is written.
	void open(const std::wstring& output_path, const simple_player_video_settings& settings,
		std::uint32_t pcm_sample_rate, std::uint32_t output_rate, bool include_audio = true)
	{
		width_ = settings.width;
		height_ = settings.height;
		fps_ = settings.fps;
		layout_ = video_yuv_layout_for(settings.chroma, settings.bit_depth);
		converter_.emplace(layout_, settings.matrix, settings.full_range, settings.chroma_filter);
		frame_bytes_ = video_yuv_frame_bytes(layout_, width_, height_);
		if (frame_bytes_ > (std::numeric_limits<DWORD>::max)())
			throw std::runtime_error("video frame is too large");

		// The MP4 writer reads its own output back to move the index first.
		require_hr(MFCreateFile(MF_ACCESSMODE_READWRITE, MF_OPENMODE_DELETE_IF_EXIST,
			MF_FILEFLAGS_NONE, output_path.c_str(), byte_stream_.GetAddressOf()),
			"opening the video output file");
		require_hr(MFCreateSinkWriterFromURL(output_path.c_str(), byte_stream_.Get(),
			sink_writer_attributes(settings).Get(), writer_.GetAddressOf()),
			(std::string("creating the ") + container_name(settings.codec) + " sink writer").c_str());

		open_video(settings, output_path);
		if (include_audio)
			open_audio(settings, pcm_sample_rate, output_rate);
		else
			audio_finished_ = true;

		require_hr(writer_->BeginWriting(), "beginning the video write");
		write_thread_ = std::thread([this]() { write_queued_samples(); });
	}

	const std::string& encoder_name() const noexcept { return encoder_name_; }
	const std::vector<std::string>& warnings() const noexcept { return warnings_; }

	void write_video_frame(const std::uint8_t* bgra_top_down, std::uint64_t frame)
	{
		const auto bytes = static_cast<DWORD>(frame_bytes_);
		ComPtr<IMFMediaBuffer> buffer;
		require_hr(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()),
			"creating video sample buffer");
		BYTE* destination = nullptr;
		DWORD maximum = 0;
		DWORD current = 0;
		require_hr(buffer->Lock(&destination, &maximum, &current),
			"locking video sample buffer");
		try
		{
			convert_frame(bgra_top_down, destination);
		}
		catch (...)
		{
			buffer->Unlock();
			throw;
		}
		require_hr(buffer->Unlock(), "unlocking video sample buffer");
		require_hr(buffer->SetCurrentLength(bytes), "setting video buffer length");

		ComPtr<IMFSample> sample;
		require_hr(MFCreateSample(sample.GetAddressOf()), "creating video sample");
		require_hr(sample->AddBuffer(buffer.Get()), "adding video buffer");
		const auto sample_time = video_sample_time(frame, fps_);
		if (frame == (std::numeric_limits<std::uint64_t>::max)())
			throw std::runtime_error("video timestamp overflow");
		const auto next_sample_time = video_sample_time(frame + 1, fps_);
		require_hr(sample->SetSampleTime(sample_time),
			"setting video sample time");
		require_hr(sample->SetSampleDuration(next_sample_time - sample_time),
			"setting video sample duration");
		enqueue_sample(stream_kind::video, sample_time, std::move(sample));
	}

	// Interleaved stereo float; null writes silence. Converted to the
	// sample format negotiated in open_audio().
	void write_audio_frames(const float* interleaved, std::uint32_t frames,
		std::uint64_t first_frame)
	{
		if (frames == 0)
			return;
		const auto bytes = static_cast<DWORD>(frames * 2U * (audio_input_bits_ / 8U));
		ComPtr<IMFMediaBuffer> buffer;
		require_hr(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()),
			"creating audio sample buffer");
		BYTE* destination = nullptr;
		DWORD maximum = 0;
		DWORD current = 0;
		require_hr(buffer->Lock(&destination, &maximum, &current),
			"locking audio sample buffer");
		convert_audio(interleaved, static_cast<std::size_t>(frames) * 2, destination);
		require_hr(buffer->Unlock(), "unlocking audio sample buffer");
		require_hr(buffer->SetCurrentLength(bytes), "setting audio buffer length");

		ComPtr<IMFSample> sample;
		require_hr(MFCreateSample(sample.GetAddressOf()), "creating audio sample");
		require_hr(sample->AddBuffer(buffer.Get()), "adding audio buffer");
		if (first_frame > (std::numeric_limits<std::uint64_t>::max)() - frames)
			throw std::runtime_error("audio timestamp overflow");
		const auto sample_time = frame_to_time(first_frame, sample_rate_);
		const auto next_sample_time = frame_to_time(first_frame + frames, sample_rate_);
		require_hr(sample->SetSampleTime(sample_time),
			"setting audio sample time");
		require_hr(sample->SetSampleDuration(next_sample_time - sample_time),
			"setting audio sample duration");
		enqueue_sample(stream_kind::audio, sample_time, std::move(sample));
	}

	void finish_audio_stream() noexcept
	{
		finish_stream(stream_kind::audio);
	}

	void finish_video_stream() noexcept
	{
		finish_stream(stream_kind::video);
	}

	void abort() noexcept
	{
		{
			std::lock_guard queue_lock(queue_mutex_);
			aborted_ = true;
		}
		queue_changed_.notify_all();
	}

	void finalize()
	{
		finish_audio_stream();
		finish_video_stream();
		join_write_thread();
		if (write_error_)
			std::rethrow_exception(write_error_);
		if (writer_)
		{
			const HRESULT result = writer_->Finalize();
			writer_.Reset();
			if (byte_stream_)
				byte_stream_->Close();
			byte_stream_.Reset();
			require_hr(result, "finalizing the video file");
		}
	}

private:
	void open_video(const simple_player_video_settings& settings, const std::wstring& output_path)
	{
		const HRESULT added = writer_->AddStream(video_output_type(settings).Get(), &video_stream_);
		if (FAILED(added))
			throw std::runtime_error(std::string("the ") + container_name(settings.codec) +
				" writer cannot store " + simple_player_video_codec_name(settings.codec) +
				" video" + hr_text(added));
		const auto properties = encoder_properties(settings);
		set_video_input(properties, settings, output_path);
		encoder_name_ = identify_encoder(settings);
		check_properties(properties);
		check_signalling(settings);
	}

	// Reads the colour description the encoder will write. A full-range
	// stream marked limited (or the reverse) shows wrong black and white
	// levels in every player, so that fails before anything renders.
	void check_signalling(const simple_player_video_settings& settings)
	{
		const bool hevc = settings.codec == simple_player_video_codec::hevc;
		if (!hevc && settings.codec != simple_player_video_codec::h264)
			return;
		ComPtr<IMFTransform> transform;
		ComPtr<IMFMediaType> output;
		UINT32 size = 0;
		if (FAILED(writer_->GetServiceForStream(video_stream_, GUID_NULL,
			IID_PPV_ARGS(transform.GetAddressOf()))) || !transform ||
			FAILED(transform->GetOutputCurrentType(0, output.GetAddressOf())) ||
			FAILED(output->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) || size == 0)
			return;
		std::vector<UINT8> header(size);
		if (FAILED(output->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), size, nullptr)))
			return;
		const auto signal = read_annex_b_signal(header.data(), header.size(), hevc);
		if (!signal.parsed)
			return;
		const bool full_range = signal.signal_type_present && signal.full_range;
		const auto range_name = [](bool full) { return full ? "full" : "limited"; };
		if (full_range != settings.full_range)
			throw std::runtime_error(encoder_name_ + " marks the video as " +
				range_name(full_range) + " range, so players would show wrong black and white "
				"levels; choose " + range_name(full_range) + " range for this encoder");
		using matrix = simple_player_video_matrix;
		const std::uint8_t expected = settings.matrix == matrix::bt601 ? 6 :
			settings.matrix == matrix::bt2020 ? 9 : 1;
		if (!signal.colour_description_present)
		{
			const auto assumed = settings.height >= 720 ? matrix::bt709 : matrix::bt601;
			if (settings.matrix != assumed)
				warnings_.push_back(encoder_name_ + " writes no colour matrix; players assume " +
					(assumed == matrix::bt709 ? "BT.709" : "BT.601") + " at this frame size");
		}
		// BT.601 has two codes: 5 (BT.470 B/G) and 6 (SMPTE 170M).
		else if (signal.matrix != expected && !(expected == 6 && signal.matrix == 5))
			warnings_.push_back(encoder_name_ + " marks the colour matrix as H.273 code " +
				std::to_string(signal.matrix));
	}

	void set_video_input(const std::vector<encoder_property>& properties,
		const simple_player_video_settings& settings, const std::wstring& output_path)
	{
		std::vector<std::size_t> all(properties.size());
		for (std::size_t index = 0; index < all.size(); ++index)
			all[index] = index;
		const HRESULT result = writer_->SetInputMediaType(video_stream_,
			video_input_type(settings).Get(), encoding_parameters(properties, all).Get());
		if (SUCCEEDED(result))
			return;
		const auto attempt = [&](const std::vector<std::size_t>& chosen)
		{
			return try_video_configuration(output_path, settings, properties, chosen);
		};
		const std::string codec = simple_player_video_codec_name(settings.codec);
		const HRESULT plain = attempt({});
		if (FAILED(plain))
			throw std::runtime_error("no " + std::string(settings.hardware_encoder ? "" : "software ") +
				codec + " encoder accepts " + format_name(settings) + " " +
				std::to_string(width_) + "x" + std::to_string(height_) + " input" + hr_text(plain) +
				(settings.hardware_encoder ? "" : "; hardware encoders are disabled"));
		// Name what the encoder refused: add the settings back one at a time,
		// then look for the earlier setting the failing one conflicts with.
		const auto rejected = [&](const std::string& what, HRESULT error)
		{
			return std::runtime_error("the " + codec + " encoder rejected the " + what + hr_text(error));
		};
		for (std::size_t failing = 0; failing < properties.size(); ++failing)
		{
			const std::vector<std::size_t> prefix(all.begin(), all.begin() +
				static_cast<std::ptrdiff_t>(failing + 1));
			const HRESULT partial = attempt(prefix);
			if (SUCCEEDED(partial))
				continue;
			const std::string label = properties[failing].label;
			if (FAILED(attempt({failing})))
				throw rejected(label + " setting", partial);
			for (std::size_t earlier = 0; earlier < failing; ++earlier)
				if (FAILED(attempt({earlier, failing})))
					throw rejected(std::string(properties[earlier].label) + " and " + label +
						" settings together", partial);
			throw rejected(label + " setting with the settings before it", partial);
		}
		throw rejected("combined settings", result);
	}

	std::string identify_encoder(const simple_player_video_settings& settings)
	{
		ComPtr<IMFTransform> transform;
		if (SUCCEEDED(writer_->GetServiceForStream(video_stream_, GUID_NULL,
			IID_PPV_ARGS(transform.GetAddressOf()))) && transform)
		{
			ComPtr<IMFAttributes> attributes;
			wchar_t* name = nullptr;
			UINT32 length = 0;
			if (SUCCEEDED(transform->GetAttributes(attributes.GetAddressOf())) && attributes &&
				SUCCEEDED(attributes->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length)))
			{
				auto result = narrow_text(name);
				CoTaskMemFree(name);
				if (!result.empty())
					return result;
			}
		}
		// Software encoders created by class ID carry no friendly name.
		return asf_container(settings.codec) ? std::string("Windows Media Video encoder (software)")
			: "Microsoft " + std::string(simple_player_video_codec_name(settings.codec)) +
				" encoder (software)";
	}

	// Reports settings the encoder does not support or replaced; encoders
	// accept many unknown properties silently.
	void check_properties(const std::vector<encoder_property>& properties)
	{
		ComPtr<ICodecAPI> api;
		if (FAILED(writer_->GetServiceForStream(video_stream_, GUID_NULL,
			IID_PPV_ARGS(api.GetAddressOf()))) || !api)
			return;
		for (const auto& property : properties)
		{
			VARIANT value;
			VariantInit(&value);
			if (api->IsSupported(&property.key) != S_OK ||
				FAILED(api->GetValue(&property.key, &value)))
			{
				warnings_.push_back(std::string(property.label) + " is not supported by " +
					encoder_name_);
				continue;
			}
			std::uint64_t actual = 0;
			bool known = true;
			switch (value.vt)
			{
			case VT_UI8: actual = value.ullVal; break;
			case VT_I8: actual = static_cast<std::uint64_t>(value.llVal); break;
			case VT_UI4: actual = value.ulVal; break;
			case VT_I4: actual = static_cast<std::uint32_t>(value.lVal); break;
			case VT_BOOL: actual = value.boolVal != VARIANT_FALSE; break;
			default: known = false; break;
			}
			VariantClear(&value);
			const auto difference = actual > property.value
				? actual - property.value : property.value - actual;
			if (known && difference > property.tolerance)
				warnings_.push_back(std::string(property.label) + ": " + encoder_name_ +
					" uses " + std::to_string(actual) + " instead of " +
					std::to_string(property.value));
		}
	}

	void open_audio(const simple_player_video_settings& settings,
		std::uint32_t pcm_sample_rate, std::uint32_t output_rate)
	{
		using audio_codec = simple_player_video_audio_codec;
		sample_rate_ = pcm_sample_rate;
		const std::string name = simple_player_video_audio_codec_name(settings.audio_codec);
		const bool sixteen_bit = settings.audio_codec == audio_codec::aac ||
			settings.audio_codec == audio_codec::wma;
		const std::uint32_t output_bits = sixteen_bit ? 16U : settings.audio_bits;
		// SAFC writes the encoder's own sample format. Media Foundation's
		// resampler, which runs when the rates differ, needs float input.
		audio_input_bits_ = output_rate == pcm_sample_rate ? output_bits : 32U;
		audio_dither_ = settings.audio_dither && audio_input_bits_ == 16;

		ComPtr<IMFMediaType> audio_out;
		if (settings.audio_codec == audio_codec::wma)
			audio_out = wma_output_type(MFAudioFormat_WMAudioV8, "WMA", output_rate, 16,
				settings.audio_bitrate_kbps);
		else if (settings.audio_codec == audio_codec::wma_pro)
			audio_out = wma_output_type(MFAudioFormat_WMAudioV9, "WMA Pro", output_rate,
				output_bits, settings.audio_bitrate_kbps);
		else
		{
			const bool float_pcm = settings.audio_codec == audio_codec::pcm && output_bits == 32;
			const GUID subtype = settings.audio_codec == audio_codec::aac ? MFAudioFormat_AAC :
				settings.audio_codec == audio_codec::alac ? MFAudioFormat_ALAC :
				settings.audio_codec == audio_codec::flac ? MFAudioFormat_FLAC :
				float_pcm ? MFAudioFormat_Float : MFAudioFormat_PCM;
			require_hr(MFCreateMediaType(audio_out.GetAddressOf()), "creating audio output type");
			require_hr(audio_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
				"setting audio major type");
			require_hr(audio_out->SetGUID(MF_MT_SUBTYPE, subtype), "setting the audio codec");
			require_hr(audio_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2),
				"setting audio channels");
			require_hr(audio_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, output_rate),
				"setting audio sample rate");
			require_hr(audio_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, output_bits),
				"setting audio bits per sample");
			if (settings.audio_codec == audio_codec::aac)
				require_hr(audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
					settings.audio_bitrate_kbps * 1000U / 8U), "setting AAC bitrate");
			else if (settings.audio_codec == audio_codec::pcm)
			{
				const UINT32 block = 2U * output_bits / 8U;
				require_hr(audio_out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block),
					"setting PCM block alignment");
				require_hr(audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
					output_rate * block), "setting PCM byte rate");
			}
		}
		const HRESULT added = writer_->AddStream(audio_out.Get(), &audio_stream_);
		if (FAILED(added))
			throw std::runtime_error(std::string("the ") + container_name(settings.codec) +
				" writer cannot store " + name + " audio" + hr_text(added));

		const UINT32 block = 2U * audio_input_bits_ / 8U;
		ComPtr<IMFMediaType> audio_in;
		require_hr(MFCreateMediaType(audio_in.GetAddressOf()), "creating audio input type");
		require_hr(audio_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
			"setting audio input major type");
		require_hr(audio_in->SetGUID(MF_MT_SUBTYPE,
			audio_input_bits_ == 32 ? MFAudioFormat_Float : MFAudioFormat_PCM),
			"setting PCM input subtype");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2),
			"setting PCM channels");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, pcm_sample_rate),
			"setting PCM sample rate");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audio_input_bits_),
			"setting PCM bits per sample");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block),
			"setting PCM block alignment");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
			pcm_sample_rate * block), "setting PCM byte rate");
		const HRESULT input = writer_->SetInputMediaType(audio_stream_, audio_in.Get(), nullptr);
		if (FAILED(input))
			throw std::runtime_error("the " + name + " encoder cannot take " +
				std::to_string(output_rate) + " Hz " + std::to_string(output_bits) +
				"-bit stereo" + (output_rate != pcm_sample_rate
					? " resampled from " + std::to_string(pcm_sample_rate) + " Hz" : std::string()) +
				hr_text(input));
	}

	// Large frames convert in parallel row bands; bands start on even rows,
	// as 4:2:0 chroma rows pair up luma rows.
	void convert_frame(const std::uint8_t* bgra, BYTE* destination) const
	{
		const auto pixels = static_cast<std::uint64_t>(width_) * height_;
		const auto cpus = std::thread::hardware_concurrency();
		const std::uint32_t bands = pixels < 2'000'000ULL
			? 1U : (std::min)(8U, (std::max)(1U, cpus / 2));
		const std::uint32_t band_rows = bands <= 1
			? height_ : ((height_ + bands - 1) / bands + 1) & ~1U;
		std::vector<std::jthread> workers;
		for (std::uint32_t first = band_rows; first < height_; first += band_rows)
			workers.emplace_back([this, bgra, destination, first, band_rows]()
			{
				converter_->convert(bgra, width_, height_, destination, first, band_rows);
			});
		converter_->convert(bgra, width_, height_, destination, 0, band_rows);
	}

	void convert_audio(const float* source, std::size_t samples, BYTE* destination) noexcept
	{
		const std::size_t bytes = audio_input_bits_ / 8U;
		if (!source)
		{
			std::memset(destination, 0, samples * bytes);
			return;
		}
		if (audio_input_bits_ == 32)
		{
			std::memcpy(destination, source, samples * sizeof(float));
			return;
		}
		for (std::size_t index = 0; index < samples; ++index)
		{
			const float value = std::clamp(source[index], -1.0f, 1.0f);
			if (audio_input_bits_ == 24)
			{
				const auto sample = static_cast<std::int32_t>(
					std::lrint(value * (value < 0.0f ? 8388608.0f : 8388607.0f)));
				destination[index * 3 + 0] = static_cast<BYTE>(sample);
				destination[index * 3 + 1] = static_cast<BYTE>(sample >> 8);
				destination[index * 3 + 2] = static_cast<BYTE>(sample >> 16);
				continue;
			}
			float scaled = value * (value < 0.0f ? 32768.0f : 32767.0f);
			// Triangular dither of one LSB peak, from two uniform values.
			if (audio_dither_)
				scaled += next_uniform() - next_uniform();
			const auto sample = static_cast<std::int16_t>(
				std::clamp(std::lrint(scaled), -32768L, 32767L));
			std::memcpy(destination + index * 2, &sample, 2);
		}
	}

	float next_uniform() noexcept
	{
		dither_state_ ^= dither_state_ << 13;
		dither_state_ ^= dither_state_ >> 17;
		dither_state_ ^= dither_state_ << 5;
		return static_cast<float>(dither_state_ >> 8) * (1.0f / 16777216.0f);
	}

	enum class stream_kind { audio, video };

	struct queued_sample
	{
		LONGLONG timestamp = 0;
		ComPtr<IMFSample> sample;
	};

	static constexpr std::size_t maximum_audio_queue_samples = 16;
	static constexpr std::size_t maximum_video_queue_samples = 4;

	void enqueue_sample(stream_kind stream, LONGLONG timestamp,
		ComPtr<IMFSample> sample)
	{
		std::unique_lock queue_lock(queue_mutex_);
		auto& queue = stream == stream_kind::audio ? audio_queue_ : video_queue_;
		const auto limit = stream == stream_kind::audio
			? maximum_audio_queue_samples : maximum_video_queue_samples;
		queue_changed_.wait(queue_lock, [&]()
		{
			return aborted_ || queue.size() < limit;
		});
		if (aborted_)
		{
			const auto error = write_error_;
			queue_lock.unlock();
			if (error)
				std::rethrow_exception(error);
			throw std::runtime_error("cancelled");
		}
		queue.push_back({timestamp, std::move(sample)});
		queue_lock.unlock();
		queue_changed_.notify_all();
	}

	void finish_stream(stream_kind stream) noexcept
	{
		{
			std::lock_guard queue_lock(queue_mutex_);
			if (stream == stream_kind::audio)
				audio_finished_ = true;
			else
				video_finished_ = true;
		}
		queue_changed_.notify_all();
	}

	std::optional<std::pair<DWORD, ComPtr<IMFSample>>> next_sample()
	{
		std::unique_lock queue_lock(queue_mutex_);
		queue_changed_.wait(queue_lock, [&]()
		{
			return aborted_ ||
				(!audio_queue_.empty() && (!video_queue_.empty() || video_finished_)) ||
				(!video_queue_.empty() && audio_finished_) ||
				(audio_finished_ && video_finished_);
		});
		if (aborted_)
			return std::nullopt;
		if (audio_queue_.empty() && video_queue_.empty())
			return std::nullopt;

		const bool take_audio = !audio_queue_.empty() &&
			(video_queue_.empty() ||
				audio_queue_.front().timestamp <= video_queue_.front().timestamp);
		auto& queue = take_audio ? audio_queue_ : video_queue_;
		auto queued = std::move(queue.front());
		queue.pop_front();
		const DWORD stream = take_audio ? audio_stream_ : video_stream_;
		queue_lock.unlock();
		queue_changed_.notify_all();
		return std::pair<DWORD, ComPtr<IMFSample>>{stream, std::move(queued.sample)};
	}

	void write_queued_samples() noexcept
	{
		try
		{
			com_mta_scope thread_com;
			while (auto queued = next_sample())
				require_hr(writer_->WriteSample(queued->first, queued->second.Get()),
					queued->first == audio_stream_
						? "writing audio sample" : "writing video sample");
		}
		catch (...)
		{
			{
				std::lock_guard queue_lock(queue_mutex_);
				if (!write_error_)
					write_error_ = std::current_exception();
				aborted_ = true;
			}
			queue_changed_.notify_all();
		}
	}

	void join_write_thread() noexcept
	{
		if (write_thread_.joinable())
			write_thread_.join();
	}

	ComPtr<IMFByteStream> byte_stream_;
	ComPtr<IMFSinkWriter> writer_;
	DWORD video_stream_ = 0;
	DWORD audio_stream_ = 0;
	std::uint32_t width_ = 0;
	std::uint32_t height_ = 0;
	std::uint32_t fps_ = 0;
	std::uint32_t sample_rate_ = 0;
	video_yuv_layout layout_ = video_yuv_layout::nv12;
	std::optional<video_yuv_converter> converter_;
	std::size_t frame_bytes_ = 0;
	std::uint32_t audio_input_bits_ = 16;
	bool audio_dither_ = false;
	std::uint32_t dither_state_ = 0x2545F491U;
	std::string encoder_name_;
	std::vector<std::string> warnings_;
	std::mutex queue_mutex_;
	std::condition_variable queue_changed_;
	std::deque<queued_sample> audio_queue_;
	std::deque<queued_sample> video_queue_;
	bool audio_finished_ = false;
	bool video_finished_ = false;
	bool aborted_ = false;
	std::exception_ptr write_error_;
	std::thread write_thread_;
};

class offscreen_gl_surface
{
public:
	~offscreen_gl_surface()
	{
		destroy();
	}

	void create(std::uint32_t width, std::uint32_t height, std::uint32_t background_rgb)
	{
		width_ = width;
		height_ = height;
		background_[0] = static_cast<float>((background_rgb >> 16) & 0xFF) / 255.0f;
		background_[1] = static_cast<float>((background_rgb >> 8) & 0xFF) / 255.0f;
		background_[2] = static_cast<float>(background_rgb & 0xFF) / 255.0f;
		if (!create_window_surface())
		{
			destroy();
			width_ = width;
			height_ = height;
			if (!create_bitmap_surface())
				throw std::runtime_error("could not create an offscreen OpenGL surface");
		}

		readback_.resize(static_cast<std::size_t>(width_) * height_ * 4);
		rgb32_.resize(static_cast<std::size_t>(width_) * height_ * 4);

		const HDC previous_dc = wglGetCurrentDC();
		const HGLRC previous_context = wglGetCurrentContext();
		require_gl(wglMakeCurrent(hdc_, context_),
			"could not inspect the offscreen OpenGL context");
		const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
		const bool core_bgra = version &&
			(version[0] > '1' || (version[0] == '1' && version[1] == '.' &&
				version[2] >= '2'));
		bgra_readback_ = core_bgra ||
			(extensions && std::strstr(extensions, "GL_EXT_bgra"));
		framebuffer_backed_ = create_framebuffer();
		wglMakeCurrent(previous_dc, previous_context);
	}

	bool hardware_accelerated() const noexcept { return hardware_accelerated_; }
	bool framebuffer_backed() const noexcept { return framebuffer_backed_; }

	void render(simple_player& player, simple_player::draw_data& data,
		std::int64_t position_us)
	{
		const HDC previous_dc = wglGetCurrentDC();
		const HGLRC previous_context = wglGetCurrentContext();
		require_gl(wglMakeCurrent(hdc_, context_), "activating offscreen OpenGL context");
		struct restore_context
		{
			HDC dc;
			HGLRC context;
			~restore_context() { wglMakeCurrent(dc, context); }
		} restore{previous_dc, previous_context};
		while (glGetError() != GL_NO_ERROR) {}
		if (framebuffer_backed_)
		{
			bind_framebuffer_(GL_FRAMEBUFFER, framebuffer_);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
		}
		else
			glDrawBuffer(double_buffered_ ? GL_BACK : GL_FRONT);

		const float virtual_width = 400.0f;
		const float half_width = virtual_width * 0.5f;
		const float half_height = half_width *
			(static_cast<float>(height_) / static_cast<float>(width_));

		glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(-half_width, half_width, -half_height, half_height, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glClearColor(background_[0], background_[1], background_[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		player.draw_at(data, position_us);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadBuffer(framebuffer_backed_ ? GL_COLOR_ATTACHMENT0 :
			(double_buffered_ ? GL_BACK : GL_FRONT));
		glReadPixels(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_),
			bgra_readback_ ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, readback_.data());
		if (glGetError() != GL_NO_ERROR)
			throw std::runtime_error("offscreen OpenGL frame readback failed");

		const std::size_t stride = static_cast<std::size_t>(width_) * 4;
		for (std::uint32_t y = 0; y < height_; ++y)
		{
			const auto* source = readback_.data() +
				static_cast<std::size_t>(height_ - 1 - y) * stride;
			auto* destination = rgb32_.data() + static_cast<std::size_t>(y) * stride;
			if (bgra_readback_)
				std::memcpy(destination, source, stride);
			else for (std::uint32_t x = 0; x < width_; ++x)
			{
				destination[x * 4 + 0] = source[x * 4 + 2];
				destination[x * 4 + 1] = source[x * 4 + 1];
				destination[x * 4 + 2] = source[x * 4 + 0];
				destination[x * 4 + 3] = 0xFF;
			}
		}
	}

	const std::uint8_t* pixels() const noexcept
	{
		return rgb32_.data();
	}

private:
	using gen_framebuffers_proc = void (APIENTRY *)(GLsizei, GLuint*);
	using bind_framebuffer_proc = void (APIENTRY *)(GLenum, GLuint);
	using framebuffer_texture_2d_proc = void (APIENTRY *)(
		GLenum, GLenum, GLenum, GLuint, GLint);
	using check_framebuffer_status_proc = GLenum (APIENTRY *)(GLenum);
	using delete_framebuffers_proc = void (APIENTRY *)(GLsizei, const GLuint*);

	static PROC load_gl_proc(const char* core_name, const char* extension_name)
	{
		auto result = wglGetProcAddress(core_name);
		auto invalid = [](PROC value)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(value);
			return !value || address <= 3 || address ==
				(std::numeric_limits<std::uintptr_t>::max)();
		};
		if (invalid(result) && extension_name)
			result = wglGetProcAddress(extension_name);
		return invalid(result) ? nullptr : result;
	}

	bool create_framebuffer()
	{
		gen_framebuffers_ = reinterpret_cast<gen_framebuffers_proc>(
			load_gl_proc("glGenFramebuffers", "glGenFramebuffersEXT"));
		bind_framebuffer_ = reinterpret_cast<bind_framebuffer_proc>(
			load_gl_proc("glBindFramebuffer", "glBindFramebufferEXT"));
		framebuffer_texture_2d_ = reinterpret_cast<framebuffer_texture_2d_proc>(
			load_gl_proc("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
		check_framebuffer_status_ = reinterpret_cast<check_framebuffer_status_proc>(
			load_gl_proc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
		delete_framebuffers_ = reinterpret_cast<delete_framebuffers_proc>(
			load_gl_proc("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
		if (!gen_framebuffers_ || !bind_framebuffer_ || !framebuffer_texture_2d_ ||
			!check_framebuffer_status_ || !delete_framebuffers_)
			return false;

		while (glGetError() != GL_NO_ERROR) {}
		glGenTextures(1, &color_texture_);
		glBindTexture(GL_TEXTURE_2D, color_texture_);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width_),
			static_cast<GLsizei>(height_), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		gen_framebuffers_(1, &framebuffer_);
		bind_framebuffer_(GL_FRAMEBUFFER, framebuffer_);
		framebuffer_texture_2d_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, color_texture_, 0);
		const bool complete = check_framebuffer_status_(GL_FRAMEBUFFER) ==
			GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
		bind_framebuffer_(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		if (complete)
			return true;

		if (framebuffer_)
			delete_framebuffers_(1, &framebuffer_);
		if (color_texture_)
			glDeleteTextures(1, &color_texture_);
		framebuffer_ = 0;
		color_texture_ = 0;
		return false;
	}

	static bool register_surface_window_class()
	{
		static const bool registered = []()
		{
			WNDCLASSEXW window_class{};
			window_class.cbSize = sizeof(window_class);
			window_class.style = CS_OWNDC;
			window_class.lpfnWndProc = DefWindowProcW;
			window_class.hInstance = GetModuleHandleW(nullptr);
			window_class.lpszClassName = L"SAFC_OffscreenVideoRenderer";
			return RegisterClassExW(&window_class) != 0 ||
				GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
		}();
		return registered;
	}

	bool configure_context(DWORD surface_flags)
	{
		PIXELFORMATDESCRIPTOR requested{};
		requested.nSize = sizeof(requested);
		requested.nVersion = 1;
		requested.dwFlags = surface_flags | PFD_SUPPORT_OPENGL;
		requested.iPixelType = PFD_TYPE_RGBA;
		requested.cColorBits = 32;
		requested.cAlphaBits = 8;
		requested.iLayerType = PFD_MAIN_PLANE;
		const int format = ChoosePixelFormat(hdc_, &requested);
		if (format == 0)
			return false;

		PIXELFORMATDESCRIPTOR selected{};
		if (!DescribePixelFormat(hdc_, format, sizeof(selected), &selected) ||
			!(selected.dwFlags & PFD_SUPPORT_OPENGL) ||
			!SetPixelFormat(hdc_, format, &selected))
			return false;
		context_ = wglCreateContext(hdc_);
		if (!context_)
			return false;
		double_buffered_ = (selected.dwFlags & PFD_DOUBLEBUFFER) != 0;
		hardware_accelerated_ = (selected.dwFlags & PFD_GENERIC_FORMAT) == 0 ||
			(selected.dwFlags & PFD_GENERIC_ACCELERATED) != 0;
		return true;
	}

	bool create_window_surface()
	{
		if (!register_surface_window_class())
			return false;
		window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			L"SAFC_OffscreenVideoRenderer", L"", WS_POPUP | WS_CLIPCHILDREN |
			WS_CLIPSIBLINGS, 0, 0, static_cast<int>(width_),
			static_cast<int>(height_), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!window_)
			return false;
		hdc_ = GetDC(window_);
		return hdc_ && configure_context(PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER);
	}

	bool create_bitmap_surface()
	{
		hdc_ = CreateCompatibleDC(nullptr);
		if (!hdc_)
			return false;
		BITMAPINFO bitmap_info{};
		bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width_);
		bitmap_info.bmiHeader.biHeight = static_cast<LONG>(height_);
		bitmap_info.bmiHeader.biPlanes = 1;
		bitmap_info.bmiHeader.biBitCount = 32;
		bitmap_info.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		bitmap_ = CreateDIBSection(hdc_, &bitmap_info, DIB_RGB_COLORS, &bits,
			nullptr, 0);
		if (!bitmap_)
			return false;
		old_bitmap_ = static_cast<HBITMAP>(SelectObject(hdc_, bitmap_));
		if (!old_bitmap_ || old_bitmap_ == reinterpret_cast<HBITMAP>(HGDI_ERROR))
		{
			old_bitmap_ = nullptr;
			return false;
		}
		return configure_context(PFD_DRAW_TO_BITMAP);
	}

	void require_gl(BOOL ok, const char* operation)
	{
		if (!ok)
			throw std::runtime_error(operation);
	}

	void destroy()
	{
		if (context_)
		{
			const HDC previous_dc = wglGetCurrentDC();
			const HGLRC previous_context = wglGetCurrentContext();
			if ((framebuffer_ || color_texture_) && wglMakeCurrent(hdc_, context_))
			{
				if (framebuffer_ && delete_framebuffers_)
					delete_framebuffers_(1, &framebuffer_);
				if (color_texture_)
					glDeleteTextures(1, &color_texture_);
				framebuffer_ = 0;
				color_texture_ = 0;
				wglMakeCurrent(previous_dc, previous_context);
			}
			if (wglGetCurrentContext() == context_)
				wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(context_);
			context_ = nullptr;
		}
		if (hdc_ && old_bitmap_)
		{
			SelectObject(hdc_, old_bitmap_);
			old_bitmap_ = nullptr;
		}
		if (bitmap_)
		{
			DeleteObject(bitmap_);
			bitmap_ = nullptr;
		}
		if (window_ && hdc_)
			ReleaseDC(window_, hdc_);
		else if (hdc_)
			DeleteDC(hdc_);
		hdc_ = nullptr;
		if (window_)
		{
			DestroyWindow(window_);
			window_ = nullptr;
		}
	}

	HWND window_ = nullptr;
	HDC hdc_ = nullptr;
	HBITMAP bitmap_ = nullptr;
	HBITMAP old_bitmap_ = nullptr;
	HGLRC context_ = nullptr;
	GLuint framebuffer_ = 0;
	GLuint color_texture_ = 0;
	std::uint32_t width_ = 0;
	std::uint32_t height_ = 0;
	float background_[3]{};
	bool double_buffered_ = false;
	bool hardware_accelerated_ = false;
	bool bgra_readback_ = false;
	bool framebuffer_backed_ = false;
	gen_framebuffers_proc gen_framebuffers_ = nullptr;
	bind_framebuffer_proc bind_framebuffer_ = nullptr;
	framebuffer_texture_2d_proc framebuffer_texture_2d_ = nullptr;
	check_framebuffer_status_proc check_framebuffer_status_ = nullptr;
	delete_framebuffers_proc delete_framebuffers_ = nullptr;
	std::vector<std::uint8_t> readback_;
	std::vector<std::uint8_t> rgb32_;
};

void build_preview_frame(const std::uint8_t* source_bgra, std::uint32_t source_width,
	std::uint32_t source_height, std::vector<std::uint8_t>& preview,
	std::uint32_t& preview_width, std::uint32_t& preview_height)
{
	const double scale = (std::min)(
		static_cast<double>(preview_max_width) / source_width,
		static_cast<double>(preview_max_height) / source_height);
	preview_width = (std::max)(
		1U, static_cast<std::uint32_t>(std::floor(source_width * scale)));
	preview_height = (std::max)(
		1U, static_cast<std::uint32_t>(std::floor(source_height * scale)));
	preview.resize(static_cast<std::size_t>(preview_width) * preview_height * 4);

	for (std::uint32_t y = 0; y < preview_height; ++y)
	{
		const std::uint32_t source_y = (std::min)(
			source_height - 1,
			static_cast<std::uint32_t>(
				(static_cast<std::uint64_t>(y) * source_height) / preview_height));
		for (std::uint32_t x = 0; x < preview_width; ++x)
		{
			const std::uint32_t source_x = (std::min)(
				source_width - 1,
				static_cast<std::uint32_t>(
					(static_cast<std::uint64_t>(x) * source_width) / preview_width));
			const auto* src = source_bgra +
				(static_cast<std::size_t>(source_y) * source_width + source_x) * 4;
			auto* dst = preview.data() +
				(static_cast<std::size_t>(y) * preview_width + x) * 4;
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = 0xFF;
		}
	}
}

struct export_progress_state
{
	std::atomic<std::uint64_t> completed_frames{0};
	std::atomic<std::uint64_t> total_frames{0};
	std::atomic<std::uint64_t> completed_audio_frames{0};
	std::atomic<std::uint64_t> total_audio_frames{0};
	std::atomic<std::uint64_t> preparation_completed{0};
	std::atomic<std::uint64_t> preparation_total{0};
	std::atomic_bool audio_preparing{false};
	std::atomic<std::uint64_t> completed_events{0};
	std::atomic<std::uint64_t> total_events{0};
	std::atomic<std::uint64_t> active_voices{0};
	std::atomic<std::uint64_t> active_cohorts{0};
	std::atomic<std::uint64_t> peak_active_voices{0};
	std::atomic<std::uint64_t> peak_active_cohorts{0};
	std::atomic<std::uint64_t> cohort_capacity_steals{0};
	std::atomic<std::uint64_t> parallel_render_calls{0};
	std::atomic<std::uint64_t> parallel_rendered_frames{0};
	std::atomic<std::uint32_t> requested_audio_render_threads{0};
	std::atomic<std::uint32_t> audio_render_threads{0};
	std::atomic_bool software_video_renderer{false};
	std::atomic_bool framebuffer_video_renderer{false};
	std::mutex callback_mutex;
	std::chrono::steady_clock::time_point next_callback{};
};

struct render_cancellation
{
	std::atomic_bool internal{false};
	const std::atomic_bool* external = nullptr;

	bool requested() const noexcept
	{
		return internal.load(std::memory_order_acquire) ||
			(external && external->load(std::memory_order_acquire));
	}

	void request_internal() noexcept
	{
		internal.store(true, std::memory_order_release);
	}
};

bool render_cancelled(void* user_data) noexcept
{
	return static_cast<render_cancellation*>(user_data)->requested();
}

void atomic_max(std::atomic<std::uint64_t>& destination, std::uint64_t value)
{
	auto current = destination.load(std::memory_order_relaxed);
	while (current < value && !destination.compare_exchange_weak(
		current, value, std::memory_order_relaxed))
	{
	}
}

void publish_audio_progress(export_progress_state& state,
	const safsyn::SmfRenderProgress& progress, std::uint64_t lead_in_frames)
{
	state.preparation_total.store(progress.preparation.total, std::memory_order_relaxed);
	state.preparation_completed.store(progress.preparation.completed, std::memory_order_relaxed);
	state.audio_preparing.store(progress.stage == safsyn::SmfRenderProgressStage::Preparing,
		std::memory_order_release);
	state.completed_audio_frames.store(lead_in_frames + progress.frames_rendered,
		std::memory_order_relaxed);
	state.completed_events.store(progress.scheduled_events, std::memory_order_relaxed);
	state.active_voices.store(progress.active_voices, std::memory_order_relaxed);
	state.active_cohorts.store(progress.active_cohorts, std::memory_order_relaxed);
	atomic_max(state.peak_active_voices, progress.active_voices);
	atomic_max(state.peak_active_cohorts, progress.active_cohorts);
}

void publish_audio_result(export_progress_state& state,
	const safsyn::SmfRenderResult& result, std::uint64_t lead_in_frames)
{
	state.audio_preparing.store(false, std::memory_order_release);
	state.completed_audio_frames.store(lead_in_frames + result.frames_written,
		std::memory_order_relaxed);
	state.completed_events.store(result.scheduled_events, std::memory_order_relaxed);
	state.active_voices.store(result.active_voices_at_end, std::memory_order_relaxed);
	state.active_cohorts.store(result.active_cohorts_at_end, std::memory_order_relaxed);
	atomic_max(state.peak_active_voices, result.engine.peak_active_logical_voices);
	atomic_max(state.peak_active_cohorts, result.engine.peak_active_cohorts);
	state.cohort_capacity_steals.store(result.engine.cohort_capacity_steals,
		std::memory_order_relaxed);
	state.parallel_render_calls.store(result.engine.parallel_render_calls,
		std::memory_order_relaxed);
	state.parallel_rendered_frames.store(result.engine.parallel_rendered_frames,
		std::memory_order_relaxed);
	state.audio_render_threads.store(static_cast<std::uint32_t>(result.render_threads),
		std::memory_order_relaxed);
}

simple_player_video_progress make_progress_snapshot(export_progress_state& state,
	std::string stage, const std::uint8_t* preview_bgra = nullptr,
	std::uint32_t preview_width = 0, std::uint32_t preview_height = 0,
	std::uint32_t preview_stride = 0)
{
	simple_player_video_progress value;
	value.completed_frames = state.completed_frames.load(std::memory_order_relaxed);
	value.total_frames = state.total_frames.load(std::memory_order_relaxed);
	value.completed_audio_frames =
		state.completed_audio_frames.load(std::memory_order_relaxed);
	value.total_audio_frames = state.total_audio_frames.load(std::memory_order_relaxed);
	value.audio_preparing = state.audio_preparing.load(std::memory_order_acquire);
	value.preparation_completed = state.preparation_completed.load(std::memory_order_relaxed);
	value.preparation_total = state.preparation_total.load(std::memory_order_relaxed);
	value.completed_events = state.completed_events.load(std::memory_order_relaxed);
	value.total_events = state.total_events.load(std::memory_order_relaxed);
	value.active_voices = state.active_voices.load(std::memory_order_relaxed);
	value.active_cohorts = state.active_cohorts.load(std::memory_order_relaxed);
	value.peak_active_voices = state.peak_active_voices.load(std::memory_order_relaxed);
	value.peak_active_cohorts = state.peak_active_cohorts.load(std::memory_order_relaxed);
	value.cohort_capacity_steals =
		state.cohort_capacity_steals.load(std::memory_order_relaxed);
	value.parallel_render_calls =
		state.parallel_render_calls.load(std::memory_order_relaxed);
	value.parallel_rendered_frames =
		state.parallel_rendered_frames.load(std::memory_order_relaxed);
	value.requested_audio_render_threads =
		state.requested_audio_render_threads.load(std::memory_order_relaxed);
	value.audio_render_threads =
		state.audio_render_threads.load(std::memory_order_relaxed);
	value.preview_width = preview_width;
	value.preview_height = preview_height;
	value.preview_stride = preview_stride;
	value.preview_bgra = preview_bgra;
	value.stage = std::move(stage);
	// Video callbacks run independently. Keep the reason for silent audio
	// visible even when a newer callback describes video work.
	if (value.audio_preparing && value.preparation_total != 0)
		value.stage += " | Prerendering sample variants " +
			std::to_string(value.preparation_completed) + "/" +
			std::to_string(value.preparation_total) + "...";
	return value;
}

bool report_progress(simple_player_video_progress_callback progress,
	void* user_data, export_progress_state& state, std::string stage,
	render_cancellation* cancel, const std::uint8_t* preview_bgra = nullptr,
	std::uint32_t preview_width = 0, std::uint32_t preview_height = 0,
	std::uint32_t preview_stride = 0, bool force = false)
{
	if (cancel && cancel->requested())
		return false;
	if (!progress)
		return true;

	std::lock_guard callback_lock(state.callback_mutex);
	if (cancel && cancel->requested())
		return false;
	const auto now = std::chrono::steady_clock::now();
	if (!force && !preview_bgra && now < state.next_callback)
		return true;
	state.next_callback = now + std::chrono::milliseconds(100);
	return progress(make_progress_snapshot(state, std::move(stage),
		preview_bgra, preview_width, preview_height, preview_stride), user_data);
}

std::uint64_t scale_time(std::uint64_t value, std::uint64_t multiplier,
	std::uint64_t divisor)
{
	if (divisor == 0 || multiplier == 0 || (value / divisor) >
		(std::numeric_limits<std::uint64_t>::max)() / multiplier)
		throw std::runtime_error("render time is too large");
	return (value / divisor) * multiplier +
		((value % divisor) * multiplier) / divisor;
}

class scheduled_smf_visual_source final : public playback_event_source
{
public:
	scheduled_smf_visual_source(const safsyn::SmfFile& file,
		const safsyn::SmfAnalysis& analysis, std::uint32_t sample_rate)
		: file_(file), sample_rate_(sample_rate),
		duration_us_(scale_time(analysis.duration_frames, 1'000'000ULL, sample_rate))
	{
	}

	std::uint64_t total_duration_us() const override { return duration_us_; }

	void rewind() override
	{
		stream_.emplace(file_, sample_rate_);
		good_ = true;
	}

	bool next(generated_event& output) override
	{
		if (!stream_)
			rewind();
		safsyn::ScheduledSmfEvent scheduled;
		while (stream_->next(scheduled))
		{
			if (scheduled.event.kind != safsyn::SmfEventKind::Channel)
				continue;
			output = {};
			output.time_us = scale_time(scheduled.sample, 1'000'000ULL, sample_rate_);
			output.tick = scheduled.event.tick;
			output.short_msg = scheduled.event.status |
				(static_cast<std::uint32_t>(scheduled.event.data1) << 8) |
				(static_cast<std::uint32_t>(scheduled.event.data2) << 16);
			output.key = scheduled.event.data1;
			output.velocity = scheduled.event.data2;
			output.channel = scheduled.event.status & 0x0f;
			output.track_index = static_cast<std::uint16_t>((std::min)(
				scheduled.event.track, static_cast<std::uint32_t>(UINT16_MAX)));
			const auto command = scheduled.event.status & 0xf0;
			if (command == 0x90 && scheduled.event.data2 != 0)
				output.k = generated_event::kind::note_on;
			else if (command == 0x80 || command == 0x90)
				output.k = generated_event::kind::note_off;
			else
				output.k = generated_event::kind::control;
			return true;
		}
		good_ = stream_->good();
		if (!good_)
			throw std::runtime_error(first_smf_error(stream_->diagnostics(),
				"SMF visual scheduler failed"));
		return false;
	}

	bool good() const noexcept { return good_; }
	const std::vector<safsyn::SmfDiagnostic>& diagnostics() const noexcept
	{
		static const std::vector<safsyn::SmfDiagnostic> empty;
		return stream_ ? stream_->diagnostics() : empty;
	}

private:
	const safsyn::SmfFile& file_;
	std::uint32_t sample_rate_;
	std::uint64_t duration_us_;
	std::optional<safsyn::ScheduledSmfStream> stream_;
	bool good_ = true;
};

struct playback_audio_source
{
	playback_event_source* source = nullptr;
	std::uint32_t sample_rate = 0;
	render_cancellation* cancel = nullptr;
	std::exception_ptr error;
};

bool next_playback_audio_event(safsyn::TimedMidiEvent& output,
	void* user_data) noexcept
{
	auto& adapter = *static_cast<playback_audio_source*>(user_data);
	try
	{
		generated_event event;
		while (adapter.source->next(event))
		{
			if (event.short_msg == 0)
				continue;
			output = {};
			output.frame = scale_time(event.time_us, adapter.sample_rate, 1'000'000ULL);
			output.tick = event.tick.value_or(event.time_us);
			output.kind = safsyn::TimedMidiEventKind::ShortMessage;
			output.short_message = event.short_msg;
			return true;
		}
		return false;
	}
	catch (...)
	{
		adapter.error = std::current_exception();
		if (adapter.cancel)
			adapter.cancel->request_internal();
		return false;
	}
}

struct audio_progress_bridge
{
	export_progress_state* state = nullptr;
	std::uint64_t lead_in_frames = 0;
	render_cancellation* cancel = nullptr;
	simple_player_video_progress_callback progress = nullptr;
	void* progress_user_data = nullptr;
};

bool on_audio_progress(const safsyn::SmfRenderProgress& value,
	void* user_data) noexcept
{
	auto& bridge = *static_cast<audio_progress_bridge*>(user_data);
	try
	{
		const bool preparation_boundary = value.stage == safsyn::SmfRenderProgressStage::Preparing &&
			value.preparation.total != 0 &&
			(bridge.state->preparation_total.load(std::memory_order_relaxed) == 0 ||
				value.preparation.completed == value.preparation.total);
		const bool preparation_finished = value.stage != safsyn::SmfRenderProgressStage::Preparing &&
			bridge.state->audio_preparing.load(std::memory_order_acquire);
		publish_audio_progress(*bridge.state, value, bridge.lead_in_frames);
		const char* stage = value.stage == safsyn::SmfRenderProgressStage::Preparing
			? "Preparing audio" : "Rendering audio";
		return report_progress(bridge.progress, bridge.progress_user_data,
			*bridge.state, stage, bridge.cancel, nullptr, 0, 0, 0,
			preparation_boundary || preparation_finished);
	}
	catch (...)
	{
		return false;
	}
}

struct media_foundation_pcm_sink
{
	media_foundation_writer* writer = nullptr;
	std::uint64_t output_offset = 0;
	std::exception_ptr error;
};

bool write_media_foundation_pcm(const float* audio, std::uint32_t frames,
	std::uint64_t first_frame, void* user_data) noexcept
{
	auto& sink = *static_cast<media_foundation_pcm_sink*>(user_data);
	try
	{
		sink.writer->write_audio_frames(audio, frames, sink.output_offset + first_frame);
		return true;
	}
	catch (...)
	{
		sink.error = std::current_exception();
		return false;
	}
}

safsyn::SmfRenderOptions make_audio_options(
	const syncore_preferences& preferences,
	std::uint64_t tail_frames, audio_progress_bridge& progress)
{
	safsyn::SmfRenderOptions options;
	options.sample_rate = preferences.sample_rate;
	options.voice_capacity = 256;
	options.voice_model = safsyn::VoiceModel::Cohorts;
	options.maximum_cohorts = preferences.maximum_cohorts;
	options.render_threads = render_threads_from_preferences(preferences);
	options.tail_frames = tail_frames;
	options.block_frames = preferences.buffer_frames;
	options.phase = phase_settings_from_preferences(preferences);
	options.mastering = mastering_from_preferences(preferences);
	options.progress_callback = on_audio_progress;
	options.progress_user_data = &progress;
	return options;
}

void write_audio_lead_in(media_foundation_writer& writer,
	std::uint64_t lead_in_frames, std::uint32_t block_frames,
	export_progress_state& progress_state, render_cancellation* cancel,
	simple_player_video_progress_callback progress, void* progress_user_data)
{
	std::uint64_t cursor = 0;
	while (cursor < lead_in_frames)
	{
		if (cancel && cancel->requested())
			throw std::runtime_error("cancelled");
		const auto frames = static_cast<std::uint32_t>((std::min)(
			lead_in_frames - cursor, static_cast<std::uint64_t>(block_frames)));
		writer.write_audio_frames(nullptr, frames, cursor);
		cursor += frames;
		progress_state.completed_audio_frames.store(cursor, std::memory_order_relaxed);
		if (!report_progress(progress, progress_user_data, progress_state,
			"Rendering audio lead-in", cancel))
			throw std::runtime_error("cancelled");
	}
}

template<class Render>
std::uint64_t render_audio_stream(media_foundation_writer& writer,
	const syncore_preferences& preferences,
	std::uint64_t lead_in_frames, std::uint64_t tail_frames,
	export_progress_state& progress_state, render_cancellation* cancel,
	simple_player_video_progress_callback progress, void* progress_user_data,
	Render&& render)
{
	const auto block_frames = preferences.buffer_frames;
	write_audio_lead_in(writer, lead_in_frames, block_frames, progress_state,
		cancel, progress, progress_user_data);

	audio_progress_bridge bridge{&progress_state, lead_in_frames, cancel,
		progress, progress_user_data};
	auto options = make_audio_options(preferences, tail_frames, bridge);
	media_foundation_pcm_sink sink{&writer, lead_in_frames};
	safsyn::SmfRenderResult render_result;
	const bool rendered = render(options, render_result,
		write_media_foundation_pcm, &sink);
	if (sink.error)
		std::rethrow_exception(sink.error);
	publish_audio_result(progress_state, render_result, lead_in_frames);
	if (!rendered)
	{
		if (render_result.cancelled ||
			(cancel && cancel->requested()))
			throw std::runtime_error("cancelled");
		throw std::runtime_error(first_smf_error(render_result.diagnostics,
			"embedded audio rendering failed"));
	}
	if (!report_progress(progress, progress_user_data, progress_state,
		"Audio complete", cancel, nullptr, 0, 0, 0, true))
		throw std::runtime_error("cancelled");
	return lead_in_frames + render_result.frames_written;
}
std::uint64_t render_video_stream(media_foundation_writer& writer,
	playback_event_source& visual_events,
	const simple_player_video_settings& settings, std::uint64_t clip_us,
	std::uint64_t total_video_frames, export_progress_state& progress_state,
	render_cancellation* cancel, simple_player_video_progress_callback progress,
	void* progress_user_data)
{
	offscreen_gl_surface surface;
	surface.create(settings.width, settings.height, settings.background_rgb);
	progress_state.software_video_renderer.store(
		!surface.hardware_accelerated(), std::memory_order_relaxed);
	progress_state.framebuffer_video_renderer.store(
		surface.framebuffer_backed(), std::memory_order_relaxed);
	const char* video_stage = surface.hardware_accelerated()
		? (surface.framebuffer_backed()
			? "Rendering video (accelerated OpenGL FBO)"
			: "Rendering video (accelerated OpenGL backbuffer fallback)")
		: (surface.framebuffer_backed()
			? "Rendering video (software OpenGL FBO fallback)"
			: "Rendering video (software OpenGL backbuffer fallback)");

	simple_player visual_player;
	if (!visual_player.begin_offline_visual_render(visual_events))
		throw std::runtime_error("could not prepare the MIDI visual render");

	struct visual_guard
	{
		simple_player& player;
		~visual_guard() { player.end_offline_visual_render(); }
	} guard{visual_player};

	simple_player::draw_data draw_data;
	configure_draw_data(draw_data, settings);
	const std::uint64_t visual_lookahead_us = draw_data.scroll_window_us +
		export_lookahead_guard_us;
	const std::uint64_t frame_progress_step = (std::max)(
		std::uint64_t{1}, static_cast<std::uint64_t>(settings.fps) / 4ULL);
	std::uint64_t next_frame_progress = 0;
	std::vector<std::uint8_t> preview;
	std::uint32_t preview_width = 0;
	std::uint32_t preview_height = 0;

	for (std::uint64_t frame = 0; frame < total_video_frames; ++frame)
	{
		if (cancel && cancel->requested())
			throw std::runtime_error("cancelled");
		const std::uint64_t clip_position_us =
			scale_time(frame, 1'000'000ULL, settings.fps);
		const std::int64_t visual_position_us =
			static_cast<std::int64_t>((std::min)(clip_position_us, clip_us)) -
			static_cast<std::int64_t>(simple_player::default_start_lead_in_us);
		if (!visual_player.advance_offline_visual_render_to(
			visual_position_us, visual_lookahead_us, render_cancelled, cancel))
			throw std::runtime_error("visual render was cancelled");
		surface.render(visual_player, draw_data, visual_position_us);
		writer.write_video_frame(surface.pixels(), frame);
		progress_state.completed_frames.store(frame + 1, std::memory_order_relaxed);

		if (frame == 0 || frame + 1 == total_video_frames ||
			frame + 1 >= next_frame_progress)
		{
			build_preview_frame(surface.pixels(), settings.width, settings.height,
				preview, preview_width, preview_height);
			if (!report_progress(progress, progress_user_data, progress_state,
				video_stage, cancel, preview.data(), preview_width,
				preview_height, preview_width * 4U))
				throw std::runtime_error("cancelled");
			next_frame_progress = frame + 1 >
				(std::numeric_limits<std::uint64_t>::max)() - frame_progress_step
				? (std::numeric_limits<std::uint64_t>::max)()
				: frame + 1 + frame_progress_step;
		}
	}
	return total_video_frames;
}

bool cancellation_exception(const std::exception_ptr& error) noexcept
{
	if (!error)
		return false;
	try
	{
		std::rethrow_exception(error);
	}
	catch (const std::runtime_error& value)
	{
		return std::string(value.what()) == "cancelled" ||
			std::string(value.what()) == "visual render was cancelled";
	}
	catch (...)
	{
		return false;
	}
}

std::uint64_t video_frame_count(std::uint64_t clip_us, std::uint32_t fps)
{
	const auto whole = scale_time(clip_us, fps, 1'000'000ULL);
	const auto remainder = clip_us % 1'000'000ULL;
	const bool round_up = (remainder * fps) % 1'000'000ULL != 0;
	if (round_up && whole == (std::numeric_limits<std::uint64_t>::max)())
		throw std::runtime_error("video frame count overflow");
	return whole + (round_up ? 1 : 0);
}

// The encoded audio rate: the setting, or SYNCore's rate where the codec
// can take it.
std::uint32_t resolved_audio_rate(const simple_player_video_settings& settings,
	const syncore_preferences& preferences) noexcept
{
	if (settings.audio_sample_rate != 0)
		return settings.audio_sample_rate;
	const bool lossy_44_or_48 = settings.audio_codec == simple_player_video_audio_codec::aac ||
		settings.audio_codec == simple_player_video_audio_codec::wma;
	if (lossy_44_or_48 && preferences.sample_rate != 44100 && preferences.sample_rate != 48000)
		return 48000;
	return preferences.sample_rate;
}

// Some Windows versions encode AV1 but their MP4 writer cannot store it, and
// says so only when it finalizes. A few frames find that out before a render.
void check_container_accepts(const std::wstring& output_path,
	const simple_player_video_settings& settings)
{
	auto check_path = std::filesystem::path(output_path);
	check_path.replace_filename(check_path.stem().wstring() + L".safc-check" +
		check_path.extension().wstring());
	temporary_video_output check(check_path.wstring());
	media_foundation_writer writer;
	writer.open(check.path(), settings, 48000, 48000, false);
	const std::vector<std::uint8_t> black(
		static_cast<std::size_t>(settings.width) * settings.height * 4, 0);
	for (std::uint64_t frame = 0; frame < 8; ++frame)
		writer.write_video_frame(black.data(), frame);
	writer.finish_video_stream();
	try
	{
		writer.finalize();
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(std::string("this Windows version cannot store ") +
			simple_player_video_codec_name(settings.codec) + " video in " +
			container_name(settings.codec) + ": " + error.what());
	}
}

void enumerate_video_encoders(std::vector<simple_player_video_encoder_info>& encoders)
{
	constexpr simple_player_video_codec codecs[] = {
		simple_player_video_codec::h264, simple_player_video_codec::hevc,
		simple_player_video_codec::av1, simple_player_video_codec::wmv9,
		simple_player_video_codec::vc1};
	for (const auto codec : codecs)
	{
		const MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, video_subtype(codec)};
		IMFActivate** activates = nullptr;
		UINT32 count = 0;
		if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr,
			&output, &activates, &count)))
			continue;
		for (UINT32 index = 0; index < count; ++index)
		{
			ComPtr<IMFActivate> activate;
			activate.Attach(activates[index]);
			simple_player_video_encoder_info info;
			info.codec = codec;
			wchar_t* name = nullptr;
			UINT32 length = 0;
			if (SUCCEEDED(activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length)))
			{
				info.name = narrow_text(name);
				CoTaskMemFree(name);
			}
			UINT32 flags = 0;
			activate->GetUINT32(MF_TRANSFORM_FLAGS_Attribute, &flags);
			info.hardware = (flags & MFT_ENUM_FLAG_HARDWARE) != 0;
			UINT32 size = 0;
			if (SUCCEEDED(activate->GetBlobSize(MFT_INPUT_TYPES_Attributes, &size)) &&
				size >= sizeof(MFT_REGISTER_TYPE_INFO))
			{
				std::vector<MFT_REGISTER_TYPE_INFO> inputs(size / sizeof(MFT_REGISTER_TYPE_INFO));
				if (SUCCEEDED(activate->GetBlob(MFT_INPUT_TYPES_Attributes,
					reinterpret_cast<UINT8*>(inputs.data()),
					static_cast<UINT32>(inputs.size() * sizeof(MFT_REGISTER_TYPE_INFO)), nullptr)))
				{
					for (const auto& input : inputs)
					{
						const auto& subtype = input.guidSubtype;
						if (subtype == MFVideoFormat_NV12) info.inputs[0][0] = true;
						else if (subtype == MFVideoFormat_P010) info.inputs[0][1] = true;
						else if (subtype == MFVideoFormat_YUY2) info.inputs[1][0] = true;
						else if (subtype == MFVideoFormat_Y210) info.inputs[1][1] = true;
						else if (subtype == MFVideoFormat_AYUV) info.inputs[2][0] = true;
						else if (subtype == MFVideoFormat_Y410) info.inputs[2][1] = true;
					}
				}
			}
			// Drivers can register one encoder per adapter under one name.
			const auto existing = std::find_if(encoders.begin(), encoders.end(),
				[&](const simple_player_video_encoder_info& value)
				{
					return value.codec == codec && value.name == info.name;
				});
			if (existing == encoders.end())
				encoders.push_back(std::move(info));
			else
				for (int chroma = 0; chroma < 3; ++chroma)
					for (int depth = 0; depth < 2; ++depth)
						existing->inputs[chroma][depth] |= info.inputs[chroma][depth];
		}
		CoTaskMemFree(activates);
	}

	const bool av1 = std::any_of(encoders.begin(), encoders.end(),
		[](const simple_player_video_encoder_info& value)
		{
			return value.codec == simple_player_video_codec::av1;
		});
	if (!av1)
		return;
	bool writable = true;
	try
	{
		wchar_t directory[MAX_PATH + 1]{};
		if (!GetTempPathW(MAX_PATH + 1, directory))
			throw std::runtime_error("no temporary directory");
		simple_player_video_settings check;
		check.codec = simple_player_video_codec::av1;
		check_container_accepts(
			(std::filesystem::path(directory) / L"safc-encoder-check.mp4").wstring(), check);
	}
	catch (...)
	{
		writable = false;
	}
	for (auto& encoder : encoders)
		if (encoder.codec == simple_player_video_codec::av1)
			encoder.writable = writable;
}

template<class RenderAudio>
simple_player_video_result render_video_and_audio(
	const std::wstring& source_path, playback_event_source& visual_events,
	std::uint64_t duration_frames, std::uint64_t duration_us,
	std::uint64_t total_events, const std::wstring& output_path,
	const std::wstring& bank_path, const syncore_preferences& synth_preferences,
	const simple_player_video_settings& settings, std::atomic_bool* cancel,
	simple_player_video_progress_callback progress, void* progress_user_data,
	RenderAudio&& render_audio)
{
	validate_settings(settings, synth_preferences);
	validate_paths(source_path, output_path, bank_path, settings.codec);
	render_cancellation effective_cancel;
	effective_cancel.external = cancel;
	if (effective_cancel.requested())
		throw std::runtime_error("cancelled");

	export_progress_state progress_state;
	progress_state.requested_audio_render_threads.store(
		synth_preferences.render_threads, std::memory_order_relaxed);
	progress_state.total_events.store(total_events, std::memory_order_relaxed);
	if (!report_progress(progress, progress_user_data, progress_state,
		"Loading MIDI and sound bank", &effective_cancel, nullptr, 0, 0, 0, true))
		throw std::runtime_error("cancelled");

	auto bank = load_bank(bank_path);
	const auto lead_in_frames = seconds_to_frames(
		simple_player::default_start_lead_in_us / 1'000'000.0,
		synth_preferences.sample_rate);
	const auto tail_frames = seconds_to_frames(
		settings.tail_seconds, synth_preferences.sample_rate);
	const auto tail_us = seconds_to_us(settings.tail_seconds);
	const auto maximum_clip_us = static_cast<std::uint64_t>(
		(std::numeric_limits<LONGLONG>::max)()) / 10ULL;
	if (duration_us > maximum_clip_us -
		simple_player::default_start_lead_in_us - tail_us)
		throw std::runtime_error("video duration is too large");
	const auto clip_us = simple_player::default_start_lead_in_us + duration_us +
		tail_us;
	const auto total_video_frames = video_frame_count(clip_us, settings.fps);
	if (duration_frames > (std::numeric_limits<std::uint64_t>::max)() -
		lead_in_frames - tail_frames)
		throw std::runtime_error("audio duration is too large");
	const auto total_audio_frames = lead_in_frames + duration_frames + tail_frames;
	(void)frame_to_time(total_audio_frames, synth_preferences.sample_rate);
	(void)video_sample_time(total_video_frames, settings.fps);
	progress_state.total_frames.store(total_video_frames, std::memory_order_relaxed);
	progress_state.total_audio_frames.store(total_audio_frames, std::memory_order_relaxed);

	media_runtime runtime;
	if (settings.codec == simple_player_video_codec::av1)
		check_container_accepts(output_path, settings);
	temporary_video_output temporary(output_path);
	media_foundation_writer writer;
	writer.open(temporary.path(), settings, synth_preferences.sample_rate,
		resolved_audio_rate(settings, synth_preferences));

	std::exception_ptr first_error;
	bool first_error_is_cancellation = false;
	std::mutex error_mutex;
	auto capture_error = [&]()
	{
		auto current = std::current_exception();
		const bool current_is_cancellation = cancellation_exception(current);
		{
			std::lock_guard error_lock(error_mutex);
			if (!first_error || (first_error_is_cancellation && !current_is_cancellation))
			{
				first_error = std::move(current);
				first_error_is_cancellation = current_is_cancellation;
			}
		}
		effective_cancel.request_internal();
		writer.abort();
	};

	std::uint64_t audio_frames = 0;
	std::uint64_t video_frames = 0;
	std::thread audio_thread;
	std::thread video_thread;
	try
	{
		audio_thread = std::thread([&]()
		{
			try
			{
				com_mta_scope thread_com;
				auto render_with_bank = [&](const safsyn::SmfRenderOptions& options,
					safsyn::SmfRenderResult& result,
					safsyn::StereoPcmWriteCallback write_pcm, void* write_user_data)
				{
					return render_audio(*bank, effective_cancel, options, result,
						write_pcm, write_user_data);
				};
				audio_frames = render_audio_stream(writer, synth_preferences,
					lead_in_frames, tail_frames, progress_state, &effective_cancel,
					progress, progress_user_data, render_with_bank);
				writer.finish_audio_stream();
			}
			catch (...)
			{
				writer.finish_audio_stream();
				capture_error();
			}
		});
		video_thread = std::thread([&]()
		{
			try
			{
				com_mta_scope thread_com;
				video_frames = render_video_stream(writer, visual_events, settings,
					clip_us, total_video_frames, progress_state, &effective_cancel,
					progress, progress_user_data);
				writer.finish_video_stream();
			}
			catch (...)
			{
				writer.finish_video_stream();
				capture_error();
			}
		});
	}
	catch (...)
	{
		effective_cancel.request_internal();
		writer.abort();
		if (audio_thread.joinable())
			audio_thread.join();
		if (video_thread.joinable())
			video_thread.join();
		throw;
	}
	audio_thread.join();
	video_thread.join();
	if (first_error)
		std::rethrow_exception(first_error);

	if (!report_progress(progress, progress_user_data, progress_state,
		std::string("Finalizing ") + container_name(settings.codec), &effective_cancel,
		nullptr, 0, 0, 0, true))
		throw std::runtime_error("cancelled");
	writer.finalize();
	temporary.commit();

	simple_player_video_result result;
	result.ok = true;
	result.video_encoder = writer.encoder_name();
	result.audio_frames = audio_frames;
	result.video_frames = video_frames;
	result.cohort_capacity_steals = progress_state.cohort_capacity_steals.load(
		std::memory_order_relaxed);
	if (result.cohort_capacity_steals != 0)
		result.warning = "SYNCore reached the export cohort limit and stole " +
			std::to_string(result.cohort_capacity_steals) + " cohort(s)";
	if (progress_state.software_video_renderer.load(std::memory_order_relaxed))
	{
		if (!result.warning.empty())
			result.warning += "; ";
		result.warning += "video frames used the software OpenGL fallback";
	}
	for (const auto& warning : writer.warnings())
	{
		if (!result.warning.empty())
			result.warning += "; ";
		result.warning += warning;
	}
	return result;
}

template<class Render>
simple_player_video_result guard_video_render(Render&& render) noexcept
{
	try
	{
		return render();
	}
	catch (const std::bad_alloc&)
	{
		simple_player_video_result result;
		result.error = "not enough memory while rendering video";
		return result;
	}
	catch (const std::runtime_error& error)
	{
		simple_player_video_result result;
		result.cancelled = std::string(error.what()) == "cancelled" ||
			std::string(error.what()) == "visual render was cancelled";
		result.error = result.cancelled ? "video render cancelled" : error.what();
		return result;
	}
	catch (const std::exception& error)
	{
		simple_player_video_result result;
		result.error = error.what();
		return result;
	}
	catch (...)
	{
		simple_player_video_result result;
		result.error = "unknown video render failure";
		return result;
	}
}
}
#endif

bool simple_player_video_export_available() noexcept
{
#ifdef SAFC_WITH_SYNCORE
	return true;
#else
	return false;
#endif
}

std::vector<simple_player_video_encoder_info> simple_player_video_encoders() noexcept
{
	std::vector<simple_player_video_encoder_info> encoders;
#ifdef SAFC_WITH_SYNCORE
	try
	{
		// A private MTA thread: the caller's thread may already be an STA.
		std::thread([&encoders]()
		{
			try
			{
				media_runtime runtime;
				enumerate_video_encoders(encoders);
			}
			catch (...)
			{
			}
		}).join();
	}
	catch (...)
	{
	}
#endif
	return encoders;
}

const char* simple_player_video_codec_name(simple_player_video_codec codec) noexcept
{
	switch (codec)
	{
	case simple_player_video_codec::h264: return "H.264";
	case simple_player_video_codec::hevc: return "HEVC";
	case simple_player_video_codec::av1: return "AV1";
	case simple_player_video_codec::wmv9: return "WMV 9";
	case simple_player_video_codec::vc1: return "VC-1";
	}
	return "unknown";
}

const char* simple_player_video_audio_codec_name(simple_player_video_audio_codec codec) noexcept
{
	switch (codec)
	{
	case simple_player_video_audio_codec::aac: return "AAC";
	case simple_player_video_audio_codec::alac: return "ALAC";
	case simple_player_video_audio_codec::flac: return "FLAC";
	case simple_player_video_audio_codec::wma: return "WMA";
	case simple_player_video_audio_codec::wma_pro: return "WMA Pro";
	case simple_player_video_audio_codec::pcm: return "PCM";
	}
	return "unknown";
}

const wchar_t* simple_player_video_extension(simple_player_video_codec codec) noexcept
{
	return codec == simple_player_video_codec::wmv9 || codec == simple_player_video_codec::vc1
		? L".wmv" : L".mp4";
}

bool simple_player_video_audio_supported(simple_player_video_codec video,
	simple_player_video_audio_codec audio) noexcept
{
	const bool asf = video == simple_player_video_codec::wmv9 ||
		video == simple_player_video_codec::vc1;
	switch (audio)
	{
	case simple_player_video_audio_codec::aac:
	case simple_player_video_audio_codec::alac:
	case simple_player_video_audio_codec::flac:
		return !asf;
	case simple_player_video_audio_codec::wma:
	case simple_player_video_audio_codec::wma_pro:
	case simple_player_video_audio_codec::pcm:
		return asf;
	}
	return false;
}

std::string simple_player_video_settings_problem(
	const simple_player_video_settings& settings,
	const syncore_preferences& preferences)
{
	using codec = simple_player_video_codec;
	using audio = simple_player_video_audio_codec;
	constexpr auto automatic = simple_player_video_encoder_default;
	const auto one_of = [](std::uint32_t value, std::initializer_list<std::uint32_t> options)
	{
		return std::find(options.begin(), options.end(), value) != options.end();
	};
	const auto optional_in = [](std::int32_t value, std::int32_t minimum, std::int32_t maximum)
	{
		return value == automatic || (value >= minimum && value <= maximum);
	};

	if (settings.width < 16 || settings.width > 8192 || settings.height < 16 ||
		settings.height > 8192 || (settings.width & 1U) || (settings.height & 1U))
		return "Width and height must be even values from 16 to 8192";
	if (settings.fps < 1 || settings.fps > 240)
		return "FPS must be from 1 to 240";
	if (settings.codec > codec::vc1)
		return "Unknown video codec";
	if (settings.rate_control > simple_player_video_rate_control::constant_qp)
		return "Unknown rate control mode";
	if (settings.video_bitrate_kbps < 64 || settings.video_bitrate_kbps > 250000)
		return "Video bitrate must be from 64 to 250000 kbps";
	if (settings.quality > 100)
		return "Quality must be from 0 to 100";
	const std::int64_t qp_limit = settings.codec == codec::av1 ? 255 : 51;
	const auto qp_range = "0 to " + std::to_string(qp_limit);
	const auto qp = static_cast<std::int64_t>(settings.qp);
	if (qp > qp_limit)
		return "QP must be from " + qp_range;
	if (qp + settings.qp_p_offset < 0 || qp + settings.qp_p_offset > qp_limit ||
		qp + settings.qp_b_offset < 0 || qp + settings.qp_b_offset > qp_limit)
		return "QP with its P- and B-frame offsets must stay from " + qp_range;
	if (settings.peak_bitrate_kbps != 0 && (settings.peak_bitrate_kbps < settings.video_bitrate_kbps ||
		settings.peak_bitrate_kbps > 1000000))
		return "Peak bitrate must be 0 (encoder default) or from the video bitrate to 1000000 kbps";
	if (settings.buffer_kbits > 4000000)
		return "VBV buffer must be at most 4000000 kbits";
	if (!optional_in(settings.min_qp, 0, static_cast<std::int32_t>(qp_limit)) ||
		!optional_in(settings.max_qp, 0, static_cast<std::int32_t>(qp_limit)))
		return "Minimum and maximum QP must be from " + qp_range;
	if (settings.min_qp != automatic && settings.max_qp != automatic &&
		settings.min_qp > settings.max_qp)
		return "Minimum QP must not exceed maximum QP";
	if (!optional_in(settings.quality_vs_speed, 0, 100))
		return "Speed/quality preset must be from 0 to 100";
	if (!optional_in(settings.gop_size, 1, 1000000))
		return "Keyframe interval must be from 1 to 1000000 frames";
	if (!optional_in(settings.b_frames, 0, 16))
		return "B-frame count must be from 0 to 16";
	if (!optional_in(settings.max_reference_frames, 1, 16))
		return "Reference frame count must be from 1 to 16";
	if (!optional_in(settings.worker_threads, 1, 64))
		return "Encoder worker threads must be from 1 to 64";
	if (!optional_in(settings.cabac, 0, 1) || !optional_in(settings.low_latency, 0, 1))
		return "CABAC and low-latency mode must be default, on, or off";
	if (settings.h264_profile > simple_player_video_h264_profile::high)
		return "Unknown H.264 profile";
	if (settings.codec == codec::h264 && settings.cabac == 1 &&
		settings.h264_profile == simple_player_video_h264_profile::baseline)
		return "CABAC needs the Main or High H.264 profile";
	if (settings.chroma > simple_player_video_chroma::yuv444 ||
		(settings.bit_depth != 8 && settings.bit_depth != 10))
		return "Chroma must be 4:2:0, 4:2:2, or 4:4:4 at 8 or 10 bits";
	if (settings.codec != codec::hevc && settings.chroma != simple_player_video_chroma::yuv420)
		return "Only HEVC can encode 4:2:2 or 4:4:4";
	if (settings.bit_depth == 10 && settings.codec != codec::hevc && settings.codec != codec::av1)
		return "Only HEVC and AV1 can encode 10-bit video";
	if (settings.matrix > simple_player_video_matrix::bt2020 ||
		settings.chroma_filter > simple_player_video_chroma_filter::nearest)
		return "Unknown colour matrix or chroma filter";
	if (settings.background_rgb > 0xFFFFFF)
		return "Background must be an RGB colour";

	if (settings.audio_codec > audio::pcm)
		return "Unknown audio codec";
	if (!simple_player_video_audio_supported(settings.codec, settings.audio_codec))
		return settings.codec == codec::wmv9 || settings.codec == codec::vc1
			? "WMV video carries WMA, WMA Pro, or PCM audio"
			: "MP4 video carries AAC, ALAC, or FLAC audio";
	const auto rate = settings.audio_sample_rate;
	const auto bitrate = settings.audio_bitrate_kbps;
	const auto bits = settings.audio_bits;
	const std::string audio_name = simple_player_video_audio_codec_name(settings.audio_codec);
	switch (settings.audio_codec)
	{
	case audio::aac:
		if (!one_of(bitrate, {96, 128, 160, 192}))
			return "AAC bitrate must be 96, 128, 160, or 192 kbps";
		if (rate != 0 && !one_of(rate, {44100, 48000}))
			return "AAC sample rate must be 44100 or 48000 Hz";
		break;
	case audio::wma:
		if (bitrate < 32 || bitrate > 320)
			return "WMA bitrate must be from 32 to 320 kbps";
		if (rate != 0 && !one_of(rate, {44100, 48000}))
			return "WMA sample rate must be 44100 or 48000 Hz";
		break;
	case audio::wma_pro:
		if (bitrate < 32 || bitrate > 768)
			return "WMA Pro bitrate must be from 32 to 768 kbps";
		if (rate != 0 && !one_of(rate, {44100, 48000, 88200, 96000}))
			return "WMA Pro sample rate must be 44100, 48000, 88200, or 96000 Hz";
		if (!one_of(bits, {16, 24}))
			return "WMA Pro samples must be 16 or 24 bits";
		break;
	default:
		if (rate != 0 && !one_of(rate, {32000, 44100, 48000, 88200, 96000, 176400, 192000}))
			return audio_name + " sample rate must be 32000, 44100, 48000, 88200, 96000, "
				"176400, or 192000 Hz";
		if (settings.audio_codec == audio::pcm ? !one_of(bits, {16, 24, 32}) : !one_of(bits, {16, 24}))
			return settings.audio_codec == audio::pcm
				? "PCM samples must be 16 or 24 bits, or 32-bit float"
				: audio_name + " samples must be 16 or 24 bits";
		break;
	}

	if (!std::isfinite(settings.tail_seconds) || settings.tail_seconds < 0.0 ||
		settings.tail_seconds > 60.0)
		return "Tail seconds must be from 0 to 60";
	if (!std::isfinite(settings.visible_seconds) || settings.visible_seconds < 0.01 ||
		settings.visible_seconds > 60.0)
		return "Visible seconds must be from 0.01 to 60";
	if (preferences.sample_rate < 8000 || preferences.sample_rate > 192000)
		return "SYNCore sample rate is outside the supported range";
	if (preferences.buffer_frames < 256 || preferences.buffer_frames > 1048576)
		return "SYNCore buffer size is outside the supported range";
	if (preferences.maximum_cohorts == 0 || preferences.maximum_cohorts > 1048576)
		return "SYNCore cohort ceiling is outside the supported range";
	if (preferences.render_threads > 64)
		return "SYNCore thread setting is outside the supported range";
	return {};
}

simple_player_video_result render_simple_player_video(
	const std::wstring& midi_path,
	const std::wstring& output_path,
	const std::wstring& bank_path,
	const syncore_preferences& synth_preferences,
	const simple_player_video_settings& settings,
	std::atomic_bool* cancel,
	simple_player_video_progress_callback progress,
	void* progress_user_data) noexcept
{
#ifndef SAFC_WITH_SYNCORE
	(void)midi_path;
	(void)output_path;
	(void)bank_path;
	(void)synth_preferences;
	(void)settings;
	(void)cancel;
	(void)progress;
	(void)progress_user_data;
	simple_player_video_result result;
	result.error = "this SAFC build does not include SYNCore";
	return result;
#else
	return guard_video_render([&]()
	{
		validate_settings(settings, synth_preferences);
		validate_paths(midi_path, output_path, bank_path, settings.codec);
		if (cancel && cancel->load(std::memory_order_acquire))
			throw std::runtime_error("cancelled");

		safsyn::SmfFile file;
		if (!file.load_bytes(read_file_bytes(midi_path)))
			throw std::runtime_error(first_smf_error(file.diagnostics(),
				"invalid MIDI file"));

		safsyn::SmfAnalysisOptions analysis_options;
		analysis_options.sample_rate = synth_preferences.sample_rate;
		safsyn::SmfAnalysis analysis;
		if (!safsyn::analyze_smf(file, analysis_options, analysis))
			throw std::runtime_error(first_smf_error(analysis.diagnostics,
				"could not analyze MIDI file"));

		scheduled_smf_visual_source visual_events(
			file, analysis, synth_preferences.sample_rate);
		const auto duration_us = scale_time(
			analysis.duration_frames, 1'000'000ULL, synth_preferences.sample_rate);
		auto render_audio = [&](const safsyn::Soundfont& bank,
			render_cancellation&,
			const safsyn::SmfRenderOptions& options,
			safsyn::SmfRenderResult& render_result,
			safsyn::StereoPcmWriteCallback write_pcm, void* write_user_data)
		{
			return safsyn::render_smf_pcm(file, analysis, bank, options,
				render_result, write_pcm, write_user_data);
		};
		return render_video_and_audio(midi_path, visual_events,
			analysis.duration_frames, duration_us, analysis.total_events,
			output_path, bank_path, synth_preferences, settings,
			cancel, progress, progress_user_data, render_audio);
	});
#endif
}

simple_player_video_result render_simple_player_video_events(
	const std::wstring& source_path,
	playback_event_source& audio_events,
	playback_event_source& visual_events,
	std::uint64_t total_events,
	const std::wstring& output_path,
	const std::wstring& bank_path,
	const syncore_preferences& synth_preferences,
	const simple_player_video_settings& settings,
	std::atomic_bool* cancel,
	simple_player_video_progress_callback progress,
	void* progress_user_data) noexcept
{
#ifndef SAFC_WITH_SYNCORE
	(void)source_path;
	(void)audio_events;
	(void)visual_events;
	(void)total_events;
	(void)output_path;
	(void)bank_path;
	(void)synth_preferences;
	(void)settings;
	(void)cancel;
	(void)progress;
	(void)progress_user_data;
	simple_player_video_result result;
	result.error = "this SAFC build does not include SYNCore";
	return result;
#else
	return guard_video_render([&]()
	{
		validate_settings(settings, synth_preferences);
		validate_paths(source_path, output_path, bank_path, settings.codec);
		if (cancel && cancel->load(std::memory_order_acquire))
			throw std::runtime_error("cancelled");

		const auto duration_us = audio_events.total_duration_us();
		if (duration_us != visual_events.total_duration_us())
			throw std::runtime_error(
				"prepared audio and visual event sources have different durations");
		const auto duration_frames = scale_time(
			duration_us, synth_preferences.sample_rate, 1'000'000ULL);
		auto render_audio = [&](const safsyn::Soundfont& bank,
			render_cancellation& cancellation,
			const safsyn::SmfRenderOptions& options,
			safsyn::SmfRenderResult& render_result,
			safsyn::StereoPcmWriteCallback write_pcm, void* write_user_data)
		{
			audio_events.rewind();
			playback_audio_source source{
				&audio_events, synth_preferences.sample_rate, &cancellation};
			const bool rendered = safsyn::render_timed_midi_pcm(
				duration_frames, bank, next_playback_audio_event, &source,
				options, render_result, write_pcm, write_user_data);
			if (source.error)
				std::rethrow_exception(source.error);
			return rendered;
		};
		return render_video_and_audio(source_path, visual_events,
			duration_frames, duration_us, total_events, output_path,
			bank_path, synth_preferences, settings, cancel, progress,
			progress_user_data, render_audio);
	});
#endif
}
