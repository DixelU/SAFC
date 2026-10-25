#define NOMINMAX
#include "preferences.h"
#include <type_traits>
#include "../WinRegWrappers.h"
#include <algorithm>
#include <cmath>
#include <thread>

namespace safc::imgui_ui
{
namespace
{
constexpr std::uint32_t minimum_sample_rate = 8000;
constexpr std::uint32_t maximum_sample_rate = 192000;
constexpr std::uint32_t minimum_buffer_frames = 256;
constexpr std::uint32_t maximum_buffer_frames = 1048576;
constexpr std::uint32_t minimum_cohorts = 1;
constexpr std::uint32_t maximum_cohorts = 1048576;
constexpr std::uint32_t maximum_render_threads = 64;
constexpr double minimum_gain_db = -60.;
constexpr double maximum_gain_db = 12.;

// Encoder options stored as DWORDs; -1 (encoder default) round-trips as 0xFFFFFFFF.
template<class Field>
void video_encoder_fields(simple_player_video_settings& video, Field&& field)
{
	field(L"PLAYER_RENDER_BACKGROUND", video.background_rgb);
	field(L"PLAYER_RENDER_CODEC", video.codec);
	field(L"PLAYER_RENDER_RATE_CONTROL", video.rate_control);
	field(L"PLAYER_RENDER_QUALITY", video.quality);
	field(L"PLAYER_RENDER_QP", video.qp);
	field(L"PLAYER_RENDER_QP_P_OFFSET", video.qp_p_offset);
	field(L"PLAYER_RENDER_QP_B_OFFSET", video.qp_b_offset);
	field(L"PLAYER_RENDER_PEAK_KBPS", video.peak_bitrate_kbps);
	field(L"PLAYER_RENDER_BUFFER_KBITS", video.buffer_kbits);
	field(L"PLAYER_RENDER_MIN_QP", video.min_qp);
	field(L"PLAYER_RENDER_MAX_QP", video.max_qp);
	field(L"PLAYER_RENDER_SPEED_QUALITY", video.quality_vs_speed);
	field(L"PLAYER_RENDER_GOP", video.gop_size);
	field(L"PLAYER_RENDER_B_FRAMES", video.b_frames);
	field(L"PLAYER_RENDER_REFERENCES", video.max_reference_frames);
	field(L"PLAYER_RENDER_ENCODER_THREADS", video.worker_threads);
	field(L"PLAYER_RENDER_CABAC", video.cabac);
	field(L"PLAYER_RENDER_LOW_LATENCY", video.low_latency);
	field(L"PLAYER_RENDER_HARDWARE", video.hardware_encoder);
	field(L"PLAYER_RENDER_H264_PROFILE", video.h264_profile);
	field(L"PLAYER_RENDER_CHROMA", video.chroma);
	field(L"PLAYER_RENDER_BIT_DEPTH", video.bit_depth);
	field(L"PLAYER_RENDER_MATRIX", video.matrix);
	field(L"PLAYER_RENDER_FULL_RANGE", video.full_range);
	field(L"PLAYER_RENDER_CHROMA_FILTER", video.chroma_filter);
	field(L"PLAYER_RENDER_FAST_START", video.fast_start);
	field(L"PLAYER_RENDER_AUDIO_CODEC", video.audio_codec);
	field(L"PLAYER_RENDER_AUDIO_BITS", video.audio_bits);
	field(L"PLAYER_RENDER_AUDIO_DITHER", video.audio_dither);
}

// Unknown stored enum values fall back to the defaults, so combos never
// index past their names; other bad values are reported by the exporter.
void sanitize_video_enums(simple_player_video_settings& video)
{
	const simple_player_video_settings defaults;
	if (video.codec > simple_player_video_codec::vc1)
		video.codec = defaults.codec;
	if (video.rate_control > simple_player_video_rate_control::constant_qp)
		video.rate_control = defaults.rate_control;
	if (video.h264_profile > simple_player_video_h264_profile::high)
		video.h264_profile = defaults.h264_profile;
	if (video.chroma > simple_player_video_chroma::yuv444)
		video.chroma = defaults.chroma;
	if (video.matrix > simple_player_video_matrix::bt2020)
		video.matrix = defaults.matrix;
	if (video.chroma_filter > simple_player_video_chroma_filter::nearest)
		video.chroma_filter = defaults.chroma_filter;
	if (video.audio_codec > simple_player_video_audio_codec::pcm)
		video.audio_codec = defaults.audio_codec;
}
}

bool valid_synth_preferences(const syncore_preferences& preferences)
{
	return preferences.sample_rate >= minimum_sample_rate && preferences.sample_rate <= maximum_sample_rate &&
		preferences.buffer_frames >= minimum_buffer_frames && preferences.buffer_frames <= maximum_buffer_frames &&
		preferences.maximum_cohorts >= minimum_cohorts && preferences.maximum_cohorts <= maximum_cohorts &&
		preferences.render_threads <= maximum_render_threads && std::isfinite(preferences.output_gain_db) &&
		preferences.output_gain_db >= minimum_gain_db && preferences.output_gain_db <= maximum_gain_db;
}

application_preferences preferences_store::load() const
{
	application_preferences p;
	p.processing_threads = static_cast<std::uint16_t>(std::clamp(std::thread::hardware_concurrency(), 1u, 64u));
	WinReg::RegKey key;
	try
	{
		key.Open(HKEY_CURRENT_USER, L"Software\\SAFC\\", KEY_READ);
	}
	catch (...)
	{
		return p;
	}
	auto number = [&](const wchar_t* name, auto& value)
	{
		try
		{
			value = static_cast<std::remove_reference_t<decltype(value)>>(key.GetDwordValue(name));
		}
		catch (...)
		{
		}
	};
	auto text = [&](const wchar_t* name, std::wstring& value)
	{
		try
		{
			value = key.GetStringValue(name);
		}
		catch (...)
		{
		}
	};
	number(L"DEFAULT_BOOL_SETTINGS", p.processing_flags);
	number(L"AS_THREADS_COUNT", p.processing_threads);
	number(L"SPLIT_TRACKS", p.split_channels);
	number(L"COLLAPSE_MIDI", p.collapse_tracks);
	number(L"APPLY_OFFSET_AFTER", p.apply_offset_after);
	number(L"AS_INPLACE_FLAG", p.inplace_merge);
	number(L"RSB_COMPRESS", p.rsb_compression);
	number(L"ALLOW_SYSEX", p.allow_sysex);
	number(L"AUTOUPDATECHECK", p.automatic_updates);
	number(L"AS_BCKGID", p.background);
	text(L"MIDI_DEVICE_NAME", p.midi_device);
	text(L"SYNCORE_BANK_PATH", p.sound_bank);
	number(L"SYNCORE_SAMPLE_RATE", p.synth.sample_rate);
	number(L"SYNCORE_BUFFER_FRAMES", p.synth.buffer_frames);
	number(L"SYNCORE_MAX_COHORTS", p.synth.maximum_cohorts);
	number(L"SYNCORE_RENDER_THREADS", p.synth.render_threads);
	number(L"SYNCORE_PHASE_MODE", p.synth.phase_mode);
	number(L"SYNCORE_LIMITER", p.synth.limiter_enabled);
	number(L"SYNCORE_SHED_NOTES", p.synth.shed_quiet_notes);
	number(L"PLAYER_RENDER_WIDTH", p.video.width);
	number(L"PLAYER_RENDER_HEIGHT", p.video.height);
	number(L"PLAYER_RENDER_FPS", p.video.fps);
	number(L"PLAYER_RENDER_VIDEO_KBPS", p.video.video_bitrate_kbps);
	number(L"PLAYER_RENDER_AUDIO_KBPS", p.video.audio_bitrate_kbps);
	number(L"PLAYER_RENDER_AUDIO_RATE", p.video.audio_sample_rate);
	video_encoder_fields(p.video, number);
	sanitize_video_enums(p.video);
	try
	{
		p.video.tail_seconds = std::stod(key.GetStringValue(L"PLAYER_RENDER_TAIL_SECONDS"));
	}
	catch (...)
	{
	}
	try
	{
		p.video.visible_seconds = std::stod(key.GetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS"));
	}
	catch (...)
	{
	}
	try
	{
		p.synth.output_gain_db = std::stod(key.GetStringValue(L"SYNCORE_GAIN_DB"));
	}
	catch (...)
	{
	}
	try
	{
		p.ui_scale = std::stof(key.GetStringValue(L"IMGUI_UI_SCALE"));
	}
	catch (...)
	{
	}
	p.processing_threads = std::clamp<std::uint16_t>(p.processing_threads, 1, 256);
	// Two backgrounds exist (the Background combo's items).
	p.background = std::clamp(p.background, 0, 1);
	p.synth.sample_rate = std::clamp(p.synth.sample_rate, minimum_sample_rate, maximum_sample_rate);
	p.synth.buffer_frames = std::clamp(p.synth.buffer_frames, minimum_buffer_frames, maximum_buffer_frames);
	p.synth.maximum_cohorts = std::clamp(p.synth.maximum_cohorts, minimum_cohorts, maximum_cohorts);
	p.synth.render_threads = std::min(p.synth.render_threads, maximum_render_threads);
	p.synth.phase_mode = syncore_phase_mode_from_stored(static_cast<std::uint32_t>(p.synth.phase_mode));
	p.synth.output_gain_db = std::isfinite(p.synth.output_gain_db)
		? std::clamp(p.synth.output_gain_db, minimum_gain_db, maximum_gain_db)
		: syncore_preferences{}.output_gain_db;
	p.ui_scale = std::isfinite(p.ui_scale) ? std::clamp(p.ui_scale, .75f, 2.f) : 1.f;
	return p;
}

void preferences_store::save(const application_preferences& p) const
{
	WinReg::RegKey key;
	key.Create(HKEY_CURRENT_USER, L"Software\\SAFC\\");
	key.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", p.processing_flags);
	key.SetDwordValue(L"AS_THREADS_COUNT", p.processing_threads);
	key.SetDwordValue(L"SPLIT_TRACKS", p.split_channels);
	key.SetDwordValue(L"COLLAPSE_MIDI", p.collapse_tracks);
	key.SetDwordValue(L"APPLY_OFFSET_AFTER", p.apply_offset_after);
	key.SetDwordValue(L"AS_INPLACE_FLAG", p.inplace_merge);
	key.SetDwordValue(L"RSB_COMPRESS", p.rsb_compression);
	key.SetDwordValue(L"ALLOW_SYSEX", p.allow_sysex);
	key.SetDwordValue(L"AUTOUPDATECHECK", p.automatic_updates);
	key.SetDwordValue(L"AS_BCKGID", p.background);
	key.SetStringValue(L"IMGUI_UI_SCALE", std::to_wstring(p.ui_scale));
	key.SetStringValue(L"MIDI_DEVICE_NAME", p.midi_device);
	key.SetStringValue(L"SYNCORE_BANK_PATH", p.sound_bank);
	key.SetDwordValue(L"SYNCORE_SAMPLE_RATE", p.synth.sample_rate);
	key.SetDwordValue(L"SYNCORE_BUFFER_FRAMES", p.synth.buffer_frames);
	key.SetDwordValue(L"SYNCORE_MAX_COHORTS", p.synth.maximum_cohorts);
	key.SetDwordValue(L"SYNCORE_RENDER_THREADS", p.synth.render_threads);
	key.SetDwordValue(L"SYNCORE_PHASE_MODE", static_cast<std::uint32_t>(p.synth.phase_mode));
	key.SetDwordValue(L"SYNCORE_LIMITER", p.synth.limiter_enabled);
	key.SetDwordValue(L"SYNCORE_SHED_NOTES", p.synth.shed_quiet_notes);
	key.SetStringValue(L"SYNCORE_GAIN_DB", std::to_wstring(p.synth.output_gain_db));
	key.SetDwordValue(L"PLAYER_RENDER_WIDTH", p.video.width);
	key.SetDwordValue(L"PLAYER_RENDER_HEIGHT", p.video.height);
	key.SetDwordValue(L"PLAYER_RENDER_FPS", p.video.fps);
	key.SetDwordValue(L"PLAYER_RENDER_VIDEO_KBPS", p.video.video_bitrate_kbps);
	key.SetDwordValue(L"PLAYER_RENDER_AUDIO_KBPS", p.video.audio_bitrate_kbps);
	key.SetDwordValue(L"PLAYER_RENDER_AUDIO_RATE", p.video.audio_sample_rate);
	auto video = p.video;
	video_encoder_fields(video, [&](const wchar_t* name, const auto& value)
	{
		key.SetDwordValue(name, static_cast<DWORD>(value));
	});
	key.SetStringValue(L"PLAYER_RENDER_TAIL_SECONDS", std::to_wstring(p.video.tail_seconds));
	key.SetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS", std::to_wstring(p.video.visible_seconds));
}

void preferences_store::save_update_preference(bool enabled) const
{
	WinReg::RegKey key;
	key.Create(HKEY_CURRENT_USER, L"Software\\SAFC\\");
	key.SetDwordValue(L"AUTOUPDATECHECK", enabled);
}
}
