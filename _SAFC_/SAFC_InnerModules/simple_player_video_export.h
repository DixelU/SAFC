#pragma once
#ifndef SAFC_SIMPLE_PLAYER_VIDEO_EXPORT
#define SAFC_SIMPLE_PLAYER_VIDEO_EXPORT

#include "syncore_output.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct playback_event_source;

// H.264, HEVC and AV1 are written to MP4; WMV 9 and VC-1 to ASF (.wmv).
enum class simple_player_video_codec : std::uint8_t
{
	h264 = 0,
	hevc = 1,
	av1 = 2,
	wmv9 = 3,
	vc1 = 4,
};

// The first seven values are eAVEncCommonRateControlMode. Constant QP is the
// encoders' Quality mode with an explicit QP, which is how Media Foundation
// encoders expose it.
enum class simple_player_video_rate_control : std::uint8_t
{
	cbr = 0,
	peak_constrained_vbr = 1,
	unconstrained_vbr = 2,
	quality = 3,
	low_delay_vbr = 4,
	global_vbr = 5,
	global_low_delay_vbr = 6,
	constant_qp = 7,
};

enum class simple_player_video_h264_profile : std::uint8_t
{
	automatic = 0,
	baseline = 1,
	main = 2,
	high = 3,
};

enum class simple_player_video_chroma : std::uint8_t
{
	yuv420 = 0,
	yuv422 = 1,
	yuv444 = 2,
};

enum class simple_player_video_matrix : std::uint8_t
{
	bt601 = 0,
	bt709 = 1,
	bt2020 = 2,
};

// How 4:2:0 and 4:2:2 chroma is reduced from the rendered RGB frame.
enum class simple_player_video_chroma_filter : std::uint8_t
{
	// Averages each 2x2 (4:2:0) or 2x1 (4:2:2) block; chroma sits centred.
	box = 0,
	// [1 2 1] horizontally on even columns; the MPEG-2/H.264 default siting.
	cosited = 1,
	// The top-left sample of each block.
	nearest = 2,
};

// MP4 carries AAC, ALAC and FLAC; WMV carries WMA, WMA Pro and PCM.
enum class simple_player_video_audio_codec : std::uint8_t
{
	aac = 0,
	alac = 1,
	flac = 2,
	wma = 3,
	wma_pro = 4,
	pcm = 5,
};

// Optional encoder properties use this value to leave the encoder's default.
constexpr std::int32_t simple_player_video_encoder_default = -1;

struct simple_player_video_settings
{
	std::uint32_t width = 1280;
	std::uint32_t height = 720;
	std::uint32_t fps = 60;
	// Mean rate for CBR and the VBR modes. It is also the stream's nominal
	// rate in the Quality and constant-QP modes, where the encoder ignores it.
	std::uint32_t video_bitrate_kbps = 12000;
	std::uint32_t audio_bitrate_kbps = 192;
	// Output rate. SYNCore renders at syncore_preferences::sample_rate; Media
	// Foundation resamples that PCM for the encoder when the rates differ.
	// 0 uses the SYNCore rate, or 48000 Hz where AAC and WMA cannot take it.
	std::uint32_t audio_sample_rate = 48000;
	double tail_seconds = 2.0;
	double visible_seconds = 0.25;
	// Matches simple_player::draw_data: 0 = Naive OR, 1 = R/t OR, 0xFF = overlaps drawn.
	std::uint8_t remove_overlaps = 0;
	// 0xRRGGBB behind the notes. A flat colour makes keying the video easier.
	std::uint32_t background_rgb = 0x050608;

	simple_player_video_codec codec = simple_player_video_codec::h264;
	simple_player_video_rate_control rate_control = simple_player_video_rate_control::cbr;
	// 0-100, Quality mode.
	std::uint32_t quality = 70;
	// Constant-QP mode. P and B frames add their offsets to the I-frame QP.
	std::uint32_t qp = 22;
	std::int32_t qp_p_offset = 0;
	std::int32_t qp_b_offset = 0;
	// Peak-constrained VBR ceiling; 0 leaves it to the encoder.
	std::uint32_t peak_bitrate_kbps = 0;
	// VBV/HRD buffer in kilobits; 0 leaves it to the encoder.
	std::uint32_t buffer_kbits = 0;
	// QP limits for the bitrate modes (simple_player_video_encoder_default = none).
	std::int32_t min_qp = simple_player_video_encoder_default;
	std::int32_t max_qp = simple_player_video_encoder_default;
	// 0 favours speed, 100 favours quality.
	std::int32_t quality_vs_speed = simple_player_video_encoder_default;
	// Frames between keyframes.
	std::int32_t gop_size = simple_player_video_encoder_default;
	std::int32_t b_frames = simple_player_video_encoder_default;
	std::int32_t max_reference_frames = simple_player_video_encoder_default;
	// Software encoders only.
	std::int32_t worker_threads = simple_player_video_encoder_default;
	// 0 = off, 1 = on, simple_player_video_encoder_default = encoder default.
	std::int32_t cabac = simple_player_video_encoder_default;
	std::int32_t low_latency = simple_player_video_encoder_default;
	// Lets Media Foundation pick GPU encoders. HEVC and AV1 usually have no
	// software encoder.
	bool hardware_encoder = true;
	simple_player_video_h264_profile h264_profile = simple_player_video_h264_profile::high;
	// Only HEVC encodes 4:2:2/4:4:4 and only HEVC/AV1 encode 10-bit, when
	// the installed encoder accepts that input.
	simple_player_video_chroma chroma = simple_player_video_chroma::yuv420;
	std::uint32_t bit_depth = 8;
	simple_player_video_matrix matrix = simple_player_video_matrix::bt709;
	bool full_range = false;
	simple_player_video_chroma_filter chroma_filter = simple_player_video_chroma_filter::box;
	// Writes the MP4 index before the media data, for progressive playback.
	bool fast_start = false;

	simple_player_video_audio_codec audio_codec = simple_player_video_audio_codec::aac;
	// 16 or 24 for ALAC, FLAC, WMA Pro and PCM; PCM also takes 32 (float).
	std::uint32_t audio_bits = 16;
	// TPDF dither when SAFC reduces SYNCore's float output to 16-bit samples.
	bool audio_dither = false;
};

struct simple_player_video_progress
{
	std::uint64_t completed_frames = 0;
	std::uint64_t total_frames = 0;
	std::uint64_t completed_audio_frames = 0;
	std::uint64_t total_audio_frames = 0;
	std::uint64_t preparation_completed = 0;
	std::uint64_t preparation_total = 0;
	bool audio_preparing = false;
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
	// The Media Foundation encoder that produced the video stream.
	std::string video_encoder;
	std::uint64_t video_frames = 0;
	std::uint64_t audio_frames = 0;
	std::uint64_t cohort_capacity_steals = 0;
};

// One installed Media Foundation video encoder and the inputs it accepts.
struct simple_player_video_encoder_info
{
	simple_player_video_codec codec = simple_player_video_codec::h264;
	std::string name;
	bool hardware = false;
	// False when this Windows version's container writer cannot store the
	// codec's stream; its MP4 writer may lack AV1, for example.
	bool writable = true;
	// Indexed by simple_player_video_chroma; [0] is 8-bit and [1] is 10-bit.
	bool inputs[3][2]{};
};

bool simple_player_video_export_available() noexcept;

// Installed encoders, enumerated on a private thread. Empty when the export
// is unavailable.
std::vector<simple_player_video_encoder_info> simple_player_video_encoders() noexcept;

// Display name, output extension (".mp4" or ".wmv") and container checks.
const char* simple_player_video_codec_name(simple_player_video_codec codec) noexcept;
const char* simple_player_video_audio_codec_name(simple_player_video_audio_codec codec) noexcept;
const wchar_t* simple_player_video_extension(simple_player_video_codec codec) noexcept;
bool simple_player_video_audio_supported(simple_player_video_codec video,
	simple_player_video_audio_codec audio) noexcept;

// Why these settings cannot be rendered, or empty. The installed encoders
// can still reject a valid combination; the render then reports why before
// any frame is rendered.
std::string simple_player_video_settings_problem(
	const simple_player_video_settings& settings,
	const syncore_preferences& synth_preferences);

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
