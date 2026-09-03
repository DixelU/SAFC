#pragma once
#ifndef SAFC_SIMPLE_PLAYER_VIDEO_EXPORT
#define SAFC_SIMPLE_PLAYER_VIDEO_EXPORT

#include "syncore_output.h"

#include <atomic>
#include <cstdint>
#include <string>

struct playback_event_source;

struct simple_player_video_settings
{
	std::uint32_t width = 1280;
	std::uint32_t height = 720;
	std::uint32_t fps = 60;
	std::uint32_t video_bitrate_kbps = 12000;
	std::uint32_t audio_bitrate_kbps = 192;
	std::uint32_t audio_sample_rate = 48000;
	// Zero gives offline rendering dynamic cohort growth without stealing.
	std::uint32_t maximum_cohorts = 0;
	double tail_seconds = 2.0;
	double visible_seconds = 4.2;
};

struct simple_player_video_progress
{
	std::uint64_t completed_frames = 0;
	std::uint64_t total_frames = 0;
	std::uint64_t completed_audio_frames = 0;
	std::uint64_t total_audio_frames = 0;
	std::uint64_t completed_events = 0;
	std::uint64_t total_events = 0;
	std::uint64_t active_voices = 0;
	std::uint64_t active_cohorts = 0;
	std::uint64_t peak_active_voices = 0;
	std::uint64_t peak_active_cohorts = 0;
	std::uint64_t cohort_capacity_steals = 0;
	std::uint64_t parallel_render_calls = 0;
	std::uint64_t parallel_rendered_frames = 0;
	std::uint32_t requested_audio_render_threads = 0;
	std::uint32_t audio_render_threads = 0;
	std::uint32_t preview_width = 0;
	std::uint32_t preview_height = 0;
	std::uint32_t preview_stride = 0;
	const std::uint8_t* preview_bgra = nullptr;
	std::string stage;
};

using simple_player_video_progress_callback =
	bool (*)(const simple_player_video_progress&, void*) noexcept;

struct simple_player_video_result
{
	bool ok = false;
	bool cancelled = false;
	std::string error;
	std::string warning;
	std::uint64_t video_frames = 0;
	std::uint64_t audio_frames = 0;
	std::uint64_t cohort_capacity_steals = 0;
};

bool simple_player_video_export_available() noexcept;

simple_player_video_result render_simple_player_video(
	const std::wstring& midi_path,
	const std::wstring& output_path,
	const std::wstring& bank_path,
	const syncore_preferences& synth_preferences,
	const simple_player_video_settings& settings,
	std::atomic_bool* cancel,
	simple_player_video_progress_callback progress,
	void* progress_user_data) noexcept;

// Render from independent cursors over the same prepared event source. This is
// used by compressed/prepared MIDI input without materializing a raw SMF.
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
	void* progress_user_data) noexcept;

#endif
