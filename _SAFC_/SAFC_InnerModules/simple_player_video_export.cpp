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
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <core.h>
#include <mastering.h>
#include <smf.h>
#include <smf_renderer.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

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
	case syncore_phase_mode::random_polarity:
		phase.mode = safsyn::PhaseMode::RandomPolarity;
		break;
	case syncore_phase_mode::analytic:
		phase.mode = safsyn::PhaseMode::Analytic;
		break;
	case syncore_phase_mode::smooth_field:
		phase.mode = safsyn::PhaseMode::SmoothField;
		break;
	case syncore_phase_mode::independent_bins:
		phase.mode = safsyn::PhaseMode::IndependentBins;
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
	if (settings.width < 16 || settings.width > 8192 ||
		settings.height < 16 || settings.height > 8192 ||
		(settings.width & 1U) || (settings.height & 1U))
		throw std::runtime_error("video width and height must be even values from 16 to 8192");
	if (settings.fps < 1 || settings.fps > 240)
		throw std::runtime_error("FPS must be from 1 to 240");
	if (settings.video_bitrate_kbps < 64 || settings.video_bitrate_kbps > 250000)
		throw std::runtime_error("bitrate settings are outside the supported range");
	if (settings.audio_bitrate_kbps != 96 && settings.audio_bitrate_kbps != 128 &&
		settings.audio_bitrate_kbps != 160 && settings.audio_bitrate_kbps != 192)
		throw std::runtime_error("AAC bitrate must be 96, 128, 160, or 192 kbps");
	if (settings.audio_sample_rate != 44100 && settings.audio_sample_rate != 48000)
		throw std::runtime_error("AAC sample rate must be 44100 or 48000 Hz");
	if (settings.maximum_cohorts > 1048576)
		throw std::runtime_error("export cohort limit must be zero or at most 1048576");
	if (!std::isfinite(settings.tail_seconds) || settings.tail_seconds < 0.0 ||
		settings.tail_seconds > 60.0 || !std::isfinite(settings.visible_seconds) ||
		settings.visible_seconds < 0.25 || settings.visible_seconds > 60.0)
		throw std::runtime_error("tail and visible seconds are outside the supported range");
	if (preferences.render_threads > 64)
		throw std::runtime_error("SYNCore thread setting is outside the supported range");
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
	const std::wstring& output_path, const std::wstring& bank_path)
{
	if (source_path.empty() || output_path.empty())
		throw std::runtime_error("missing MIDI source or output path");
	if (same_path(source_path, output_path))
		throw std::runtime_error("output path must not overwrite the MIDI source");
	if (!bank_path.empty() && same_path(bank_path, output_path))
		throw std::runtime_error("output path must not overwrite the sound bank");
	if (_wcsicmp(std::filesystem::path(output_path).extension().c_str(), L".mp4") != 0)
		throw std::runtime_error("video output path must use the .mp4 extension");
	const auto parent = std::filesystem::path(output_path).parent_path();
	std::error_code error;
	if (!parent.empty() && !std::filesystem::is_directory(parent, error))
		throw std::runtime_error("video output directory does not exist");
}

class temporary_mp4_output
{
public:
	explicit temporary_mp4_output(std::wstring destination)
		: destination_(std::move(destination))
	{
		const auto final_path = std::filesystem::path(destination_);
		const auto directory = final_path.parent_path();
		const auto stem = final_path.stem().wstring();
		for (std::uint32_t attempt = 0; attempt < 1024; ++attempt)
		{
			const auto filename = stem + L".safc-part-" +
				std::to_wstring(GetCurrentProcessId()) + L"-" +
				std::to_wstring(attempt) + L".mp4";
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
		throw std::runtime_error("could not reserve a temporary MP4 output file");
	}

	~temporary_mp4_output()
	{
		if (!committed_ && !temporary_.empty())
			DeleteFileW(temporary_.c_str());
	}

	const std::wstring& path() const noexcept { return temporary_; }

	void commit()
	{
		if (!MoveFileExW(temporary_.c_str(), destination_.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("could not replace the destination with the completed MP4");
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

class media_foundation_writer
{
public:
	~media_foundation_writer()
	{
		abort();
		join_write_thread();
	}

	void open(const std::wstring& output_path,
		const simple_player_video_settings& settings, std::uint32_t sample_rate)
	{
		width_ = settings.width;
		height_ = settings.height;
		fps_ = settings.fps;
		sample_rate_ = sample_rate;
		ComPtr<IMFAttributes> writer_attributes;
		require_hr(MFCreateAttributes(writer_attributes.GetAddressOf(), 1),
			"creating MP4 sink writer attributes");
		require_hr(writer_attributes->SetUINT32(
			MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
			"enabling hardware media transforms");
		require_hr(MFCreateSinkWriterFromURL(output_path.c_str(), nullptr,
			writer_attributes.Get(),
			writer_.GetAddressOf()), "creating MP4 sink writer");

		ComPtr<IMFMediaType> video_out;
		require_hr(MFCreateMediaType(video_out.GetAddressOf()), "creating video output type");
		require_hr(video_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
			"setting video major type");
		require_hr(video_out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264),
			"setting H.264 output subtype");
		require_hr(video_out->SetUINT32(MF_MT_AVG_BITRATE,
			settings.video_bitrate_kbps * 1000U), "setting video bitrate");
		require_hr(video_out->SetUINT32(MF_MT_INTERLACE_MODE,
			MFVideoInterlace_Progressive), "setting progressive video");
		require_hr(MFSetAttributeSize(video_out.Get(), MF_MT_FRAME_SIZE,
			settings.width, settings.height), "setting video frame size");
		require_hr(MFSetAttributeRatio(video_out.Get(), MF_MT_FRAME_RATE,
			settings.fps, 1), "setting video frame rate");
		require_hr(MFSetAttributeRatio(video_out.Get(), MF_MT_PIXEL_ASPECT_RATIO,
			1, 1), "setting video pixel aspect");
		require_hr(writer_->AddStream(video_out.Get(), &video_stream_),
			"adding video stream");

		ComPtr<IMFMediaType> video_in;
		require_hr(MFCreateMediaType(video_in.GetAddressOf()), "creating video input type");
		require_hr(video_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
			"setting video input major type");
		require_hr(video_in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32),
			"setting RGB32 input subtype");
		require_hr(video_in->SetUINT32(MF_MT_INTERLACE_MODE,
			MFVideoInterlace_Progressive), "setting input progressive video");
		require_hr(MFSetAttributeSize(video_in.Get(), MF_MT_FRAME_SIZE,
			settings.width, settings.height), "setting input frame size");
		require_hr(MFSetAttributeRatio(video_in.Get(), MF_MT_FRAME_RATE,
			settings.fps, 1), "setting input frame rate");
		require_hr(MFSetAttributeRatio(video_in.Get(), MF_MT_PIXEL_ASPECT_RATIO,
			1, 1), "setting input pixel aspect");
		require_hr(video_in->SetUINT32(MF_MT_DEFAULT_STRIDE,
			settings.width * 4U), "setting input video stride");
		require_hr(writer_->SetInputMediaType(video_stream_, video_in.Get(), nullptr),
			"setting video input type");

		ComPtr<IMFMediaType> audio_out;
		require_hr(MFCreateMediaType(audio_out.GetAddressOf()), "creating audio output type");
		require_hr(audio_out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
			"setting audio major type");
		require_hr(audio_out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC),
			"setting AAC output subtype");
		require_hr(audio_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2),
			"setting AAC channels");
		require_hr(audio_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate),
			"setting AAC sample rate");
		require_hr(audio_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16),
			"setting AAC bits per sample");
		require_hr(audio_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
			settings.audio_bitrate_kbps * 1000U / 8U), "setting AAC bitrate");
		require_hr(writer_->AddStream(audio_out.Get(), &audio_stream_),
			"adding audio stream");

		ComPtr<IMFMediaType> audio_in;
		require_hr(MFCreateMediaType(audio_in.GetAddressOf()), "creating audio input type");
		require_hr(audio_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
			"setting audio input major type");
		require_hr(audio_in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM),
			"setting PCM input subtype");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2),
			"setting PCM channels");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate),
			"setting PCM sample rate");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16),
			"setting PCM bits per sample");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4),
			"setting PCM block alignment");
		require_hr(audio_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
			sample_rate * 4U), "setting PCM byte rate");
		require_hr(writer_->SetInputMediaType(audio_stream_, audio_in.Get(), nullptr),
			"setting audio input type");

		require_hr(writer_->BeginWriting(), "beginning MP4 write");
		write_thread_ = std::thread([this]() { write_queued_samples(); });
	}

	void write_video_frame(const std::uint8_t* rgb32_top_down, std::uint64_t frame)
	{
		const DWORD bytes = width_ * height_ * 4U;
		ComPtr<IMFMediaBuffer> buffer;
		require_hr(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()),
			"creating video sample buffer");
		BYTE* destination = nullptr;
		DWORD maximum = 0;
		DWORD current = 0;
		require_hr(buffer->Lock(&destination, &maximum, &current),
			"locking video sample buffer");
		std::memcpy(destination, rgb32_top_down, bytes);
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

	void write_audio_frames(const std::int16_t* pcm_interleaved,
		std::uint32_t frames, std::uint64_t first_frame)
	{
		if (frames == 0)
			return;
		const DWORD bytes = frames * 2U * static_cast<DWORD>(sizeof(std::int16_t));
		ComPtr<IMFMediaBuffer> buffer;
		require_hr(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()),
			"creating audio sample buffer");
		BYTE* destination = nullptr;
		DWORD maximum = 0;
		DWORD current = 0;
		require_hr(buffer->Lock(&destination, &maximum, &current),
			"locking audio sample buffer");
		std::memcpy(destination, pcm_interleaved, bytes);
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
			require_hr(result, "finalizing MP4");
		}
	}

private:
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

	ComPtr<IMFSinkWriter> writer_;
	DWORD video_stream_ = 0;
	DWORD audio_stream_ = 0;
	std::uint32_t width_ = 0;
	std::uint32_t height_ = 0;
	std::uint32_t fps_ = 0;
	std::uint32_t sample_rate_ = 0;
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

	void create(std::uint32_t width, std::uint32_t height)
	{
		width_ = width;
		height_ = height;
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
		glClearColor(0.02f, 0.025f, 0.03f, 1.0f);
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

void float_to_pcm16(const float* source, std::uint32_t frames,
	std::vector<std::int16_t>& destination)
{
	destination.resize(static_cast<std::size_t>(frames) * 2);
	for (std::size_t i = 0; i < static_cast<std::size_t>(frames) * 2; ++i)
	{
		const float value = std::clamp(source[i], -1.0f, 1.0f);
		destination[i] = static_cast<std::int16_t>(
			std::lrint(value * (value < 0.0f ? 32768.0f : 32767.0f)));
	}
}

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
		publish_audio_progress(*bridge.state, value, bridge.lead_in_frames);
		const char* stage = value.stage == safsyn::SmfRenderProgressStage::Preparing
			? "Preparing audio" : "Rendering audio";
		return report_progress(bridge.progress, bridge.progress_user_data,
			*bridge.state, stage, bridge.cancel);
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
	std::vector<std::int16_t> pcm;
	std::exception_ptr error;
};

bool write_media_foundation_pcm(const float* audio, std::uint32_t frames,
	std::uint64_t first_frame, void* user_data) noexcept
{
	auto& sink = *static_cast<media_foundation_pcm_sink*>(user_data);
	try
	{
		if (frames == 0)
			return true;
		float_to_pcm16(audio, frames, sink.pcm);
		sink.writer->write_audio_frames(sink.pcm.data(), frames,
			sink.output_offset + first_frame);
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
	const simple_player_video_settings& settings, std::uint64_t tail_frames,
	audio_progress_bridge& progress)
{
	safsyn::SmfRenderOptions options;
	options.sample_rate = settings.audio_sample_rate;
	options.voice_capacity = 256;
	options.voice_model = safsyn::VoiceModel::Cohorts;
	options.maximum_cohorts = settings.maximum_cohorts;
	options.render_threads = render_threads_from_preferences(preferences);
	options.tail_frames = tail_frames;
	options.block_frames = std::clamp(preferences.buffer_frames, 256U, 8192U);
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
	std::vector<std::int16_t> silence(static_cast<std::size_t>(block_frames) * 2, 0);
	std::uint64_t cursor = 0;
	while (cursor < lead_in_frames)
	{
		if (cancel && cancel->requested())
			throw std::runtime_error("cancelled");
		const auto frames = static_cast<std::uint32_t>((std::min)(
			lead_in_frames - cursor, static_cast<std::uint64_t>(block_frames)));
		writer.write_audio_frames(silence.data(), frames, cursor);
		cursor += frames;
		progress_state.completed_audio_frames.store(cursor, std::memory_order_relaxed);
		if (!report_progress(progress, progress_user_data, progress_state,
			"Rendering audio lead-in", cancel))
			throw std::runtime_error("cancelled");
	}
}

template<class Render>
std::uint64_t render_audio_stream(media_foundation_writer& writer,
	const syncore_preferences& preferences, const simple_player_video_settings& settings,
	std::uint64_t lead_in_frames, std::uint64_t tail_frames,
	export_progress_state& progress_state, render_cancellation* cancel,
	simple_player_video_progress_callback progress, void* progress_user_data,
	Render&& render)
{
	const auto block_frames = std::clamp(preferences.buffer_frames, 256U, 8192U);
	write_audio_lead_in(writer, lead_in_frames, block_frames, progress_state,
		cancel, progress, progress_user_data);

	audio_progress_bridge bridge{&progress_state, lead_in_frames, cancel,
		progress, progress_user_data};
	auto options = make_audio_options(preferences, settings, tail_frames, bridge);
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
	surface.create(settings.width, settings.height);
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
	validate_paths(source_path, output_path, bank_path);
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
		settings.audio_sample_rate);
	const auto tail_frames = seconds_to_frames(
		settings.tail_seconds, settings.audio_sample_rate);
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
	(void)frame_to_time(total_audio_frames, settings.audio_sample_rate);
	(void)video_sample_time(total_video_frames, settings.fps);
	progress_state.total_frames.store(total_video_frames, std::memory_order_relaxed);
	progress_state.total_audio_frames.store(total_audio_frames, std::memory_order_relaxed);

	media_runtime runtime;
	temporary_mp4_output temporary(output_path);
	media_foundation_writer writer;
	writer.open(temporary.path(), settings, settings.audio_sample_rate);

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
				audio_frames = render_audio_stream(writer, synth_preferences, settings,
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
		"Finalizing MP4", &effective_cancel, nullptr, 0, 0, 0, true))
		throw std::runtime_error("cancelled");
	writer.finalize();
	temporary.commit();

	simple_player_video_result result;
	result.ok = true;
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
		validate_paths(midi_path, output_path, bank_path);
		if (cancel && cancel->load(std::memory_order_acquire))
			throw std::runtime_error("cancelled");

		safsyn::SmfFile file;
		if (!file.load_bytes(read_file_bytes(midi_path)))
			throw std::runtime_error(first_smf_error(file.diagnostics(),
				"invalid MIDI file"));

		safsyn::SmfAnalysisOptions analysis_options;
		analysis_options.sample_rate = settings.audio_sample_rate;
		safsyn::SmfAnalysis analysis;
		if (!safsyn::analyze_smf(file, analysis_options, analysis))
			throw std::runtime_error(first_smf_error(analysis.diagnostics,
				"could not analyze MIDI file"));

		scheduled_smf_visual_source visual_events(
			file, analysis, settings.audio_sample_rate);
		const auto duration_us = scale_time(
			analysis.duration_frames, 1'000'000ULL, settings.audio_sample_rate);
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
		validate_paths(source_path, output_path, bank_path);
		if (cancel && cancel->load(std::memory_order_acquire))
			throw std::runtime_error("cancelled");

		const auto duration_us = audio_events.total_duration_us();
		if (duration_us != visual_events.total_duration_us())
			throw std::runtime_error(
				"prepared audio and visual event sources have different durations");
		const auto duration_frames = scale_time(
			duration_us, settings.audio_sample_rate, 1'000'000ULL);
		auto render_audio = [&](const safsyn::Soundfont& bank,
			render_cancellation& cancellation,
			const safsyn::SmfRenderOptions& options,
			safsyn::SmfRenderResult& render_result,
			safsyn::StereoPcmWriteCallback write_pcm, void* write_user_data)
		{
			audio_events.rewind();
			playback_audio_source source{
				&audio_events, settings.audio_sample_rate, &cancellation};
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
