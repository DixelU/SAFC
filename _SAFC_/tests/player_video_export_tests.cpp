#define NOMINMAX
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "../SAFC_InnerModules/simple_player_video_export.h"
#include "../SAFC_InnerModules/playback_event_source.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"
#include "../SAFC_InnerModules/simple_player.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../SAFGUIF/textbox.h"
#include "../SAFC_InnerModules/video_frame_conversion.h"
#include "../SAFC_InnerModules/video_parameter_sets.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

void throw_alert_warning(std::string&&) {}
void throw_alert_error(std::string&&) {}

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

void test_text_box_layout_isolation()
{
	single_text_line_settings shared_style("_", 0.0f, 0.0f, 5.0f, 0xFFFFFFFF);
	text_box first("first", shared_style, 0.0f, 0.0f, 20.0f, 100.0f,
		5.0f, 0, 0, 0, _Align::center);
	text_box second("second", shared_style, 100.0f, 100.0f, 20.0f, 100.0f,
		5.0f, 0, 0, 0, _Align::center);
	check(&first.stls != &second.stls && &first.stls != &shared_style &&
		&second.stls != &shared_style,
		"text boxes keep independent mutable layout cursors");
}

void test_visual_storage_has_no_artificial_limit()
{
	simple_player::visuals_viewport visuals;
	constexpr std::uint8_t key = 60;
	visuals.push_note_on(key, 10, 0, 0, 100);
	visuals.push_note_on(key, 10, 0, 0, 100);
	check(visuals.buffered_note_count() == 2,
		"identical notes retain separate exact visual spans");
	visuals.push_note_off(key, 20, 0, 0);
	visuals.push_note_off(key, 30, 0, 0);
	const auto matched = visuals.notes_for_key(key);
	check(visuals.buffered_note_count() == 2 && matched.size() == 2 &&
		matched.front().end_time_us == 30,
		"exact visual spans retain independent note-off matching");
	visuals.clear();

	// Cross the former 2,000,000-note abort threshold and the removed
	// per-key detail threshold. Every note must still have its own span.
	constexpr std::size_t note_count = 2'004'096;
	for (std::size_t index = 0; index < note_count; ++index)
		visuals.push_note_on(key, static_cast<std::uint64_t>(index), 0, 0, 100);
	check(visuals.buffered_note_count() == note_count,
		"visual storage has no artificial note cap or low-detail fallback");
}

void test_held_notes_do_not_block_reclamation()
{
	simple_player::visuals_viewport visuals;
	constexpr std::uint8_t first_key = 60;
	constexpr std::size_t held_keys = 4;
	constexpr std::size_t completed_per_key = 4096;

	for (std::size_t key_offset = 0; key_offset < held_keys; ++key_offset)
	{
		const auto key = static_cast<std::uint8_t>(first_key + key_offset);
		visuals.push_note_on(key, 0, 0, 0, 100);
		for (std::size_t index = 0; index < completed_per_key; ++index)
		{
			const std::uint64_t start = 10 + index * 2;
			visuals.push_note_on(key, start, 1, 0, 100);
			visuals.push_note_off(key, start + 1, 1, 0);
		}
	}

	const std::int64_t cutoff = 10 + completed_per_key * 2 + 2;
	visuals.cull_expired(cutoff);
	check(visuals.buffered_note_count() == held_keys,
		"held notes do not pin later expired notes on the same keys");
	check(visuals.retained_note_count() == held_keys,
		"expired notes behind held ones are compacted out of the rings");
	for (std::size_t key_offset = 0; key_offset < held_keys; ++key_offset)
	{
		const auto key = static_cast<std::uint8_t>(first_key + key_offset);
		const auto notes = visuals.notes_for_key(key);
		check(notes.size() == 1 &&
			notes.front().end_time_us == simple_player::visuals_viewport::held_end,
			"each held key retains only its live note");
		visuals.push_note_off(key, static_cast<std::uint64_t>(cutoff), 0, 0);
	}

	visuals.cull_expired(cutoff + 1);
	check(visuals.buffered_note_count() == 0,
		"released held notes are reclaimed after crossing the cutoff");
	check(visuals.retained_note_count() == 0 &&
		visuals.rings[0].capacity() == 0 && visuals.rings[1].capacity() == 0,
		"empty rings release their retained storage");
}

class seek_probe_source final : public playback_event_source
{
public:
	std::uint64_t total_duration_us() const override { return 20'000'000; }

	void rewind() override
	{
		cursor_ = 0;
		rewind_calls.fetch_add(1, std::memory_order_release);
	}

	void seek(std::uint64_t target_us) override
	{
		last_seek_us.store(target_us, std::memory_order_release);
		seek_calls.fetch_add(1, std::memory_order_release);
		rewind();
	}

	bool next(generated_event& output) override
	{
		if (cursor_ >= 2)
			return false;
		output = {};
		output.time_us = cursor_++ == 0 ? 0 : 19'000'000;
		return true;
	}

	std::atomic_uint32_t rewind_calls{0};
	std::atomic_uint32_t seek_calls{0};
	std::atomic_uint64_t last_seek_us{0};

private:
	std::size_t cursor_ = 0;
};

class distant_event_source final : public playback_event_source
{
public:
	std::uint64_t total_duration_us() const override { return 4'000'000; }
	void rewind() override { emitted_ = false; }
	bool next(generated_event& output) override
	{
		if (emitted_)
			return false;
		emitted_ = true;
		output = {};
		output.time_us = 4'000'000;
		output.short_msg = 0x000000C0;
		return true;
	}

private:
	bool emitted_ = false;
};

template<class Predicate>
bool wait_until(Predicate&& predicate)
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return predicate();
}

void test_player_slider_seek_restart()
{
	simple_player player;
	seek_probe_source source;
	std::thread playback([&]() { player.run_from_external(&source, 0.0, true); });

	const bool started = wait_until([&]() {
		return player.is_playing() && player.is_paused();
	});
	check(started, "player reaches its initial paused state");
	if (started)
	{
		player.seek_to(0.75);
		check(player.is_seeking(),
			"seek state covers the request-to-parser-restart gap");
		const bool sought_forward = wait_until([&]() {
			return source.seek_calls.load(std::memory_order_acquire) != 0 &&
				!player.is_seeking() && player.is_paused() &&
				player.get_position_us() == 15'000'000;
		});
		check(sought_forward &&
			source.last_seek_us.load(std::memory_order_acquire) == 15'000'000,
			"paused player fast-forward lands at the slider target and stays paused");

		player.resume();
		player.seek_to(0.25);
		const bool sought_while_playing = wait_until([&]() {
			return source.seek_calls.load(std::memory_order_acquire) >= 2 &&
				!player.is_seeking() && !player.is_paused() &&
				player.get_position_us() >= 5'000'000;
		});
		check(sought_while_playing &&
			source.last_seek_us.load(std::memory_order_acquire) == 5'000'000,
			"playing player fast-forward lands at the slider target and keeps playing");
		player.pause();

		const auto rewinds_before_start_seek =
			source.rewind_calls.load(std::memory_order_acquire);
		player.seek_to(0.0);
		const bool sought_to_start = wait_until([&]() {
			return source.rewind_calls.load(std::memory_order_acquire) >
				rewinds_before_start_seek && player.is_paused() &&
				player.get_position_us() == 0;
		});
		check(sought_to_start,
			"slider seek to the beginning resets the playback clock");
	}

	player.stop();
	playback.join();
}

void test_stop_interrupts_distant_event_wait()
{
	simple_player player;
	distant_event_source source;
	std::thread playback([&]() { player.run_from_external(&source, 0.0, false); });
	const bool waiting = wait_until([&]() {
		return player.is_playing() &&
			player.get_state().parser_done.load(std::memory_order_acquire);
	});
	check(waiting, "player queues a distant event before the stop test");
	const auto stop_started = std::chrono::steady_clock::now();
	player.shutdown();
	playback.join();
	const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
	check(stop_elapsed < std::chrono::milliseconds(250),
		"Shutdown interrupts a sender waiting for a distant event and releases the player run");
}

void test_shutdown_rejects_late_playback()
{
	simple_player player;
	distant_event_source source;
	player.shutdown();

	const auto started = std::chrono::steady_clock::now();
	player.run_from_external(&source, 0.0, false);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	check(!player.is_playing() && elapsed < std::chrono::milliseconds(50),
		"Shutdown is terminal and rejects playback queued behind teardown");
}

void test_file_player_slider_seek(const std::filesystem::path& midi_path)
{
	simple_player player;
	std::thread playback([&]() { player.simple_run(midi_path.wstring()); });
	const bool started = wait_until([&]() {
		return player.is_playing() && player.is_paused();
	});
	check(started, "file player reaches its initial paused state");
	if (started)
	{
		const auto target_us = player.get_info().total_duration_us / 2;
		player.seek_to(0.5);
		check(wait_until([&]() {
			return !player.is_seeking() && player.is_paused() &&
				player.get_position_us() == target_us;
		}), "file player fast-forward lands at the slider target");
		check(player.get_visuals().buffered_note_count() == 1 &&
			player.get_state().send_buffer.approximate_size() == 1,
			"file seek restores a note held across the target for visuals and audio");

		player.resume();
		const auto backward_target_us = player.get_info().total_duration_us / 4;
		player.seek_to(0.25);
		check(wait_until([&]() {
			return !player.is_seeking() && !player.is_paused() &&
				player.get_position_us() == backward_target_us;
		}), "playing file player accepts a backward slider seek");
	}
	player.stop();
	playback.join();
}

void append_be16(std::vector<std::uint8_t>& output, std::uint16_t value)
{
	output.push_back(static_cast<std::uint8_t>(value >> 8));
	output.push_back(static_cast<std::uint8_t>(value));
}

void append_be32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
	output.push_back(static_cast<std::uint8_t>(value >> 24));
	output.push_back(static_cast<std::uint8_t>(value >> 16));
	output.push_back(static_cast<std::uint8_t>(value >> 8));
	output.push_back(static_cast<std::uint8_t>(value));
}

void append_vlq(std::vector<std::uint8_t>& output, std::uint32_t value)
{
	std::uint8_t bytes[4]{};
	std::size_t count = 0;
	bytes[count++] = static_cast<std::uint8_t>(value & 0x7fU);
	while ((value >>= 7) != 0)
		bytes[count++] = static_cast<std::uint8_t>((value & 0x7fU) | 0x80U);
	while (count != 0)
		output.push_back(bytes[--count]);
}

std::vector<std::uint8_t> smpte_fixture()
{
	std::vector<std::uint8_t> track{0x00, 0x90, 0x3c, 0x64};
	append_vlq(track, 1000); // -25 fps * 40 ticks/frame = exactly one second.
	track.insert(track.end(), {0x80, 0x3c, 0x00, 0x00, 0xff, 0x2f, 0x00});
	std::vector<std::uint8_t> file{'M', 'T', 'h', 'd'};
	append_be32(file, 6);
	append_be16(file, 0);
	append_be16(file, 1);
	append_be16(file, 0xE728); // SMPTE -25, 40 ticks per frame.
	file.insert(file.end(), {'M', 'T', 'r', 'k'});
	append_be32(file, static_cast<std::uint32_t>(track.size()));
	file.insert(file.end(), track.begin(), track.end());
	return file;
}

std::vector<std::uint8_t> ppq_fixture()
{
	std::vector<std::uint8_t> track{0x00, 0x90, 0x3c, 0x64};
	append_vlq(track, 480);
	track.insert(track.end(), {0x80, 0x3c, 0x00, 0x00, 0xff, 0x2f, 0x00});
	std::vector<std::uint8_t> file{'M', 'T', 'h', 'd'};
	append_be32(file, 6);
	append_be16(file, 0);
	append_be16(file, 1);
	append_be16(file, 480);
	file.insert(file.end(), {'M', 'T', 'r', 'k'});
	append_be32(file, static_cast<std::uint32_t>(track.size()));
	file.insert(file.end(), track.begin(), track.end());
	return file;
}

void write_bytes(const std::filesystem::path& path,
	const std::vector<std::uint8_t>& bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		return {};
	const auto length = input.tellg();
	if (length < 0)
		return {};
	std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
	input.seekg(0);
	if (!result.empty())
		input.read(reinterpret_cast<char*>(result.data()), length);
	return result;
}

bool contains_atom(const std::vector<std::uint8_t>& bytes, const char atom[5])
{
	for (std::size_t index = 0; index + 4 <= bytes.size(); ++index)
		if (bytes[index] == atom[0] && bytes[index + 1] == atom[1] &&
			bytes[index + 2] == atom[2] && bytes[index + 3] == atom[3])
			return true;
	return false;
}

bool no_partial_outputs(const std::filesystem::path& directory,
	const std::filesystem::path& destination)
{
	const auto prefix = destination.stem().wstring() + L".safc-part-";
	for (const auto& entry : std::filesystem::directory_iterator(directory))
		if (entry.path().filename().wstring().starts_with(prefix))
			return false;
	return true;
}

simple_player_video_settings test_settings(std::uint32_t sample_rate = 48000,
	std::uint32_t audio_bitrate = 192)
{
	simple_player_video_settings settings;
	settings.width = 64;
	settings.height = 64;
	settings.fps = 1;
	settings.video_bitrate_kbps = 500;
	settings.audio_bitrate_kbps = audio_bitrate;
	settings.audio_sample_rate = sample_rate;
	settings.tail_seconds = 0.0;
	settings.visible_seconds = 1.0;
	return settings;
}

syncore_preferences test_preferences()
{
	syncore_preferences result;
	result.buffer_frames = 256;
	result.render_threads = 1;
	result.output_gain_db = 0.0;
	result.limiter_enabled = false;
	return result;
}

struct progress_capture
{
	std::uint32_t calls = 0;
	std::uint64_t maximum_active_cohorts = 0;
	std::uint64_t maximum_reported_peak_cohorts = 0;
	bool cancel_after_video_frame = false;
	bool cancel_during_preparation = false;
	bool saw_preparation_start = false;
	bool saw_preparation_complete = false;
	bool saw_rendering_after_preparation = false;
	bool preparation_status_valid = true;
	bool saw_accelerated_video = false;
	bool saw_software_video = false;
	bool saw_framebuffer_video = false;
	bool saw_backbuffer_video = false;
	bool saw_non_background_preview = false;
};

bool capture_progress(const simple_player_video_progress& progress,
	void* user_data) noexcept
{
	auto& capture = *static_cast<progress_capture*>(user_data);
	++capture.calls;
	if (progress.audio_preparing && progress.preparation_total != 0)
	{
		capture.saw_preparation_start |= progress.preparation_completed == 0;
		capture.saw_preparation_complete |=
			progress.preparation_completed == progress.preparation_total;
		capture.preparation_status_valid &=
			progress.preparation_completed <= progress.preparation_total &&
			progress.stage.find("Prerendering sample variants ") != std::string::npos;
		if (capture.cancel_during_preparation)
			return false;
	}
	else if (capture.saw_preparation_complete &&
		progress.stage.find("Rendering audio") != std::string::npos)
		capture.saw_rendering_after_preparation = true;
	capture.maximum_active_cohorts = (std::max)(
		capture.maximum_active_cohorts, progress.active_cohorts);
	capture.maximum_reported_peak_cohorts = (std::max)(
		capture.maximum_reported_peak_cohorts, progress.peak_active_cohorts);
	capture.saw_accelerated_video = capture.saw_accelerated_video ||
		progress.stage.find("accelerated OpenGL") != std::string::npos;
	capture.saw_software_video = capture.saw_software_video ||
		progress.stage.find("software OpenGL") != std::string::npos;
	capture.saw_framebuffer_video = capture.saw_framebuffer_video ||
		progress.stage.find("OpenGL FBO") != std::string::npos;
	capture.saw_backbuffer_video = capture.saw_backbuffer_video ||
		progress.stage.find("OpenGL backbuffer") != std::string::npos;
	if (progress.preview_bgra && progress.preview_width != 0 &&
		progress.preview_height != 0 && progress.preview_stride >= progress.preview_width * 4U)
	{
		for (std::uint32_t y = 0; y < progress.preview_height &&
			!capture.saw_non_background_preview; ++y)
		{
			const auto* row = progress.preview_bgra +
				static_cast<std::size_t>(y) * progress.preview_stride;
			for (std::uint32_t x = 0; x < progress.preview_width; ++x)
				if (row[x * 4] > 32 || row[x * 4 + 1] > 32 || row[x * 4 + 2] > 32)
				{
					capture.saw_non_background_preview = true;
					break;
				}
		}
	}
	return !capture.cancel_after_video_frame || progress.completed_frames == 0;
}

class vector_event_source final : public playback_event_source
{
public:
	vector_event_source(std::uint64_t duration, std::vector<generated_event> events,
		bool fail = false)
		: duration_(duration), events_(std::move(events)), fail_(fail) {}

	std::uint64_t total_duration_us() const override { return duration_; }
	void rewind() override { cursor_ = 0; }
	bool next(generated_event& output) override
	{
		if (fail_)
		{
			fail_ = false;
			throw std::runtime_error("synthetic event source failure");
		}
		if (cursor_ == events_.size())
			return false;
		output = events_[cursor_++];
		return true;
	}

private:
	std::uint64_t duration_ = 0;
	std::vector<generated_event> events_;
	std::size_t cursor_ = 0;
	bool fail_ = false;
};

generated_event short_event(std::uint64_t time_us, std::uint8_t status,
	std::uint8_t key, std::uint8_t velocity)
{
	generated_event event;
	event.time_us = time_us;
	event.short_msg = status | (static_cast<std::uint32_t>(key) << 8) |
		(static_cast<std::uint32_t>(velocity) << 16);
	event.key = key;
	event.velocity = velocity;
	event.channel = status & 0x0f;
	event.k = (status & 0xf0) == 0x90 && velocity != 0
		? generated_event::kind::note_on : generated_event::kind::note_off;
	return event;
}

std::array<std::uint8_t, 4> convert_pixel(video_yuv_layout layout,
	simple_player_video_matrix matrix, bool full_range, std::uint8_t r, std::uint8_t g,
	std::uint8_t b)
{
	// A 2x2 frame of one colour, so every layout yields its single Y, Cb, Cr.
	std::vector<std::uint8_t> bgra;
	for (int pixel = 0; pixel < 4; ++pixel)
		bgra.insert(bgra.end(), {b, g, r, 0xFF});
	std::vector<std::uint8_t> output(video_yuv_frame_bytes(layout, 2, 2));
	video_yuv_converter(layout, matrix, full_range, simple_player_video_chroma_filter::box)
		.convert(bgra.data(), 2, 2, output.data(), 0, 2);
	const auto word = [&](std::size_t offset)
	{
		return static_cast<std::uint16_t>(output[offset] | (output[offset + 1] << 8));
	};
	switch (layout)
	{
	case video_yuv_layout::nv12: return {output[0], output[4], output[5], 0};
	case video_yuv_layout::ayuv: return {output[2], output[1], output[0], output[3]};
	case video_yuv_layout::yuy2: return {output[0], output[1], output[3], 0};
	default: break;
	}
	// Ten-bit layouts report value >> 2, enough for these checks.
	if (layout == video_yuv_layout::p010)
		return {static_cast<std::uint8_t>(word(0) >> 8), static_cast<std::uint8_t>(word(8) >> 8),
			static_cast<std::uint8_t>(word(10) >> 8), 0};
	if (layout == video_yuv_layout::y210)
		return {static_cast<std::uint8_t>(word(0) >> 8), static_cast<std::uint8_t>(word(2) >> 8),
			static_cast<std::uint8_t>(word(6) >> 8), 0};
	std::uint32_t packed = 0;
	std::memcpy(&packed, output.data(), 4);
	return {static_cast<std::uint8_t>(((packed >> 10) & 0x3FF) >> 2),
		static_cast<std::uint8_t>((packed & 0x3FF) >> 2),
		static_cast<std::uint8_t>(((packed >> 20) & 0x3FF) >> 2),
		static_cast<std::uint8_t>(packed >> 30)};
}

bool near_value(int actual, int expected, int tolerance = 1)
{
	return actual >= expected - tolerance && actual <= expected + tolerance;
}

void test_yuv_conversion()
{
	using layout = video_yuv_layout;
	const auto bt709 = simple_player_video_matrix::bt709;
	const auto white = convert_pixel(layout::nv12, bt709, false, 255, 255, 255);
	const auto black = convert_pixel(layout::nv12, bt709, false, 0, 0, 0);
	check(white[0] == 235 && white[1] == 128 && white[2] == 128 &&
		black[0] == 16 && black[1] == 128 && black[2] == 128,
		"limited-range white and black map to 235 and 16 with neutral chroma");
	const auto full_white = convert_pixel(layout::nv12, bt709, true, 255, 255, 255);
	const auto full_black = convert_pixel(layout::nv12, bt709, true, 0, 0, 0);
	check(full_white[0] == 255 && full_black[0] == 0 && full_white[1] == 128,
		"full-range white and black use the whole code range");
	// BT.709 red: Y = 16 + 219 * 0.2126, Cb = 128 - 224 * 0.1146, Cr = 240.
	const auto red = convert_pixel(layout::nv12, bt709, false, 255, 0, 0);
	check(near_value(red[0], 63) && near_value(red[1], 102) && red[2] == 240,
		"BT.709 red has the specified luma and chroma");
	const auto red601 = convert_pixel(layout::nv12, simple_player_video_matrix::bt601,
		false, 255, 0, 0);
	check(near_value(red601[0], 81) && near_value(red601[1], 90) && red601[2] == 240,
		"BT.601 red uses the BT.601 coefficients");
	const auto colour = convert_pixel(layout::nv12, bt709, false, 32, 160, 240);
	for (const auto other : {layout::p010, layout::yuy2, layout::y210, layout::ayuv, layout::y410})
	{
		const auto value = convert_pixel(other, bt709, false, 32, 160, 240);
		check(near_value(value[0], colour[0]) && near_value(value[1], colour[1]) &&
			near_value(value[2], colour[2]),
			"every YUV layout stores the same colour as NV12");
	}
	check(convert_pixel(layout::ayuv, bt709, false, 1, 2, 3)[3] == 0xFF &&
		convert_pixel(layout::y410, bt709, false, 1, 2, 3)[3] == 3,
		"packed 4:4:4 layouts are opaque");

	// A 2x2 block of red over black: box averages to half red, nearest keeps
	// the top-left red, and 4:4:4 keeps black where the frame is black.
	const std::vector<std::uint8_t> split{0, 0, 255, 255, 0, 0, 255, 255,
		0, 0, 0, 255, 0, 0, 0, 255};
	std::vector<std::uint8_t> nv12(video_yuv_frame_bytes(layout::nv12, 2, 2));
	video_yuv_converter(layout::nv12, bt709, false, simple_player_video_chroma_filter::box)
		.convert(split.data(), 2, 2, nv12.data(), 0, 2);
	const auto box_cr = nv12[5];
	video_yuv_converter(layout::nv12, bt709, false, simple_player_video_chroma_filter::nearest)
		.convert(split.data(), 2, 2, nv12.data(), 0, 2);
	check(near_value(box_cr, 184) && nv12[5] == 240 && nv12[0] == nv12[1] && nv12[2] == 16,
		"4:2:0 chroma filters average or pick the block as documented");
	std::vector<std::uint8_t> ayuv(video_yuv_frame_bytes(layout::ayuv, 2, 2));
	video_yuv_converter(layout::ayuv, bt709, false, simple_player_video_chroma_filter::box)
		.convert(split.data(), 2, 2, ayuv.data(), 0, 2);
	check(ayuv[0] == 240 && ayuv[8] == 128 && ayuv[10] == 16,
		"4:4:4 keeps each pixel's own chroma");
}

// One MP4 box's payload after its type, from the first occurrence of `type`.
const std::uint8_t* find_box(const std::vector<std::uint8_t>& bytes, const char (&type)[5],
	std::size_t minimum_payload)
{
	for (std::size_t index = 0; index + 4 + minimum_payload <= bytes.size(); ++index)
		if (std::memcmp(bytes.data() + index, type, 4) == 0)
			return bytes.data() + index + 4;
	return nullptr;
}

// Offset of a top-level MP4 box, or SIZE_MAX.
std::size_t top_level_box(const std::vector<std::uint8_t>& bytes, const char (&type)[5])
{
	std::size_t offset = 0;
	while (offset + 8 <= bytes.size())
	{
		std::uint64_t size = (static_cast<std::uint64_t>(bytes[offset]) << 24) |
			(bytes[offset + 1] << 16) | (bytes[offset + 2] << 8) | bytes[offset + 3];
		if (std::memcmp(bytes.data() + offset + 4, type, 4) == 0)
			return offset;
		if (size == 1 && offset + 16 <= bytes.size())
		{
			size = 0;
			for (int byte = 0; byte < 8; ++byte)
				size = (size << 8) | bytes[offset + 8 + byte];
		}
		if (size < 8)
			break;
		offset += static_cast<std::size_t>(size);
	}
	return SIZE_MAX;
}

// The colour signalling of the first SPS in an MP4's avcC or hvcC box.
video_signal_description stream_signal(const std::vector<std::uint8_t>& bytes, bool hevc)
{
	const auto* end = bytes.data() + bytes.size();
	if (!hevc)
	{
		const auto* avcc = find_box(bytes, "avcC", 8);
		if (!avcc || (avcc[5] & 0x1F) == 0)
			return {};
		const std::size_t length = (static_cast<std::size_t>(avcc[6]) << 8) | avcc[7];
		return avcc + 8 + length <= end ? read_h264_sps_signal(avcc + 8, length)
			: video_signal_description{};
	}
	const auto* hvcc = find_box(bytes, "hvcC", 23);
	if (!hvcc)
		return {};
	const auto* cursor = hvcc + 22;
	const unsigned arrays = *cursor++;
	for (unsigned array = 0; array < arrays && cursor + 3 <= end; ++array)
	{
		const unsigned type = cursor[0] & 0x3F;
		const unsigned count = (static_cast<unsigned>(cursor[1]) << 8) | cursor[2];
		cursor += 3;
		for (unsigned unit = 0; unit < count && cursor + 2 <= end; ++unit)
		{
			const std::size_t length = (static_cast<std::size_t>(cursor[0]) << 8) | cursor[1];
			cursor += 2;
			if (cursor + length > end)
				return {};
			if (type == 33)
				return read_hevc_sps_signal(cursor, length);
			cursor += length;
		}
	}
	return {};
}

bool is_asf(const std::vector<std::uint8_t>& bytes)
{
	static const std::uint8_t header[] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
		0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
	return bytes.size() > sizeof(header) && std::memcmp(bytes.data(), header, sizeof(header)) == 0;
}

// Decodes the first frame through Media Foundation's own colour conversion,
// which reads the matrix and range the encoder signalled.
bool decoded_pixel(const std::filesystem::path& path, std::uint32_t x, std::uint32_t y,
	std::array<std::uint8_t, 3>& bgr)
{
	using Microsoft::WRL::ComPtr;
	const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(MFStartup(MF_VERSION)))
		return false;
	bool ok = false;
	{
		ComPtr<IMFAttributes> attributes;
		ComPtr<IMFSourceReader> reader;
		ComPtr<IMFMediaType> rgb;
		ComPtr<IMFMediaType> current;
		if (SUCCEEDED(MFCreateAttributes(attributes.GetAddressOf(), 1)) &&
			SUCCEEDED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) &&
			SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), reader.GetAddressOf())) &&
			SUCCEEDED(MFCreateMediaType(rgb.GetAddressOf())) &&
			SUCCEEDED(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
			SUCCEEDED(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) &&
			SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgb.Get())) &&
			SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, current.GetAddressOf())))
		{
			UINT32 width = 0, height = 0;
			MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
			const auto stride = static_cast<INT32>(MFGetAttributeUINT32(current.Get(),
				MF_MT_DEFAULT_STRIDE, width * 4));
			for (int attempt = 0; attempt < 16 && !ok; ++attempt)
			{
				DWORD flags = 0;
				ComPtr<IMFSample> sample;
				if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
					&flags, nullptr, sample.GetAddressOf())) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
					break;
				ComPtr<IMFMediaBuffer> buffer;
				BYTE* data = nullptr;
				DWORD length = 0;
				if (!sample || FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())) ||
					FAILED(buffer->Lock(&data, nullptr, &length)))
					continue;
				// A negative stride stores the bottom row first.
				const std::size_t row = stride < 0 ? height - 1 - y : y;
				const std::size_t offset = row * static_cast<std::size_t>(std::abs(stride)) + x * 4;
				if (x < width && y < height && offset + 3 <= length)
				{
					bgr = {data[offset], data[offset + 1], data[offset + 2]};
					ok = true;
				}
				buffer->Unlock();
			}
		}
	}
	MFShutdown();
	if (SUCCEEDED(com))
		CoUninitialize();
	return ok;
}

simple_player_video_result render_codec_case(const std::filesystem::path& output,
	const simple_player_video_settings& settings, const syncore_preferences& preferences)
{
	constexpr std::uint64_t duration_us = 1'000'000;
	std::vector<generated_event> events{
		short_event(0, 0x90, 60, 100),
		short_event(duration_us / 2, 0x80, 60, 0)};
	vector_event_source audio(duration_us, events);
	vector_event_source visual(duration_us, events);
	std::filesystem::remove(output);
	return render_simple_player_video_events(L"codec.mid", audio, visual, events.size(),
		output.wstring(), {}, preferences, settings, nullptr, nullptr, nullptr);
}

void report_case(const char* label, const simple_player_video_result& result)
{
	std::cout << label << ": " << (result.ok ? "ok" : result.error) <<
		(result.video_encoder.empty() ? "" : " [" + result.video_encoder + "]") <<
		(result.warning.empty() ? "" : " warnings: " + result.warning) << '\n';
}

void test_codecs(const std::filesystem::path& directory, const syncore_preferences& preferences)
{
	const auto encoders = simple_player_video_encoders();
	check(std::any_of(encoders.begin(), encoders.end(), [](const auto& encoder)
		{
			return encoder.codec == simple_player_video_codec::h264 && encoder.inputs[0][0];
		}), "an H.264 encoder that takes NV12 is enumerated");
	for (const auto& encoder : encoders)
		std::cout << "Encoder " << simple_player_video_codec_name(encoder.codec) << ": " <<
			encoder.name << (encoder.hardware ? " (hardware)" : "") <<
			(encoder.writable ? "" : " (container cannot store it)") << '\n';
	const auto accepts = [&](simple_player_video_codec codec, simple_player_video_chroma chroma,
		int depth)
	{
		return std::any_of(encoders.begin(), encoders.end(), [&](const auto& encoder)
		{
			return encoder.codec == codec && encoder.writable &&
				encoder.inputs[static_cast<int>(chroma)][depth];
		});
	};

	simple_player_video_settings base;
	base.width = 256;
	base.height = 144;
	base.fps = 10;
	base.tail_seconds = 0.0;
	base.visible_seconds = 1.0;

	{
		auto settings = base;
		settings.rate_control = simple_player_video_rate_control::quality;
		settings.quality = 80;
		settings.audio_codec = simple_player_video_audio_codec::alac;
		settings.audio_bits = 24;
		settings.audio_sample_rate = 0;
		settings.gop_size = 30;
		settings.fast_start = true;
		const auto output = directory / "h264-quality-alac.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case("H.264 quality + ALAC 24", result);
		const auto bytes = read_bytes(output);
		check(result.ok && !result.video_encoder.empty() && contains_atom(bytes, "avc1") &&
			contains_atom(bytes, "alac"), "H.264 Quality mode muxes with 24-bit ALAC");
		check(top_level_box(bytes, "moov") < top_level_box(bytes, "mdat"),
			"fast start writes the MP4 index before the media data");
	}
	{
		auto settings = base;
		settings.rate_control = simple_player_video_rate_control::constant_qp;
		settings.qp = 24;
		settings.qp_p_offset = 2;
		settings.qp_b_offset = 4;
		settings.audio_codec = simple_player_video_audio_codec::flac;
		settings.audio_dither = true;
		settings.audio_sample_rate = 44100;
		const auto output = directory / "h264-qp-flac.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case("H.264 constant QP + FLAC 16 resampled", result);
		const auto bytes = read_bytes(output);
		check(result.ok && contains_atom(bytes, "avc1") && contains_atom(bytes, "fLaC"),
			"H.264 constant QP muxes with resampled, dithered FLAC");
	}
	{
		auto settings = base;
		settings.hardware_encoder = false;
		settings.rate_control = simple_player_video_rate_control::peak_constrained_vbr;
		settings.video_bitrate_kbps = 800;
		settings.peak_bitrate_kbps = 2000;
		settings.b_frames = 2;
		settings.quality_vs_speed = 100;
		settings.cabac = 1;
		const auto output = directory / "h264-software.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case("H.264 software peak VBR", result);
		check(result.ok && result.video_encoder.find("software") != std::string::npos &&
			contains_atom(read_bytes(output), "avc1"),
			"software-only H.264 uses the Microsoft encoder with B-frames and peak VBR");
	}
	{
		auto settings = base;
		settings.hardware_encoder = false;
		settings.rate_control = simple_player_video_rate_control::low_delay_vbr;
		const auto output = directory / "rejected-setting.mp4";
		const std::vector<std::uint8_t> sentinel{'k', 'e', 'e', 'p'};
		write_bytes(output, sentinel);
		vector_event_source audio(1'000'000, {});
		vector_event_source visual(1'000'000, {});
		const auto result = render_simple_player_video_events(L"codec.mid", audio, visual,
			0, output.wstring(), {}, preferences, settings, nullptr, nullptr, nullptr);
		report_case("software H.264 low-delay VBR", result);
		check(!result.ok && result.error.find("rate control mode") != std::string::npos &&
			read_bytes(output) == sentinel && no_partial_outputs(directory, output),
			"an encoder-rejected setting is named before rendering and keeps the destination");
	}

	const struct
	{
		const char* label;
		simple_player_video_chroma chroma;
		std::uint32_t depth;
		std::uint8_t chroma_format;
	} hevc_cases[] = {
		{"HEVC 4:2:0 10-bit", simple_player_video_chroma::yuv420, 10, 1},
		{"HEVC 4:2:2 8-bit", simple_player_video_chroma::yuv422, 8, 2},
		{"HEVC 4:4:4 8-bit", simple_player_video_chroma::yuv444, 8, 3},
		{"HEVC 4:4:4 10-bit", simple_player_video_chroma::yuv444, 10, 3},
	};
	for (const auto& hevc : hevc_cases)
	{
		if (!accepts(simple_player_video_codec::hevc, hevc.chroma, hevc.depth > 8))
		{
			std::cout << hevc.label << ": no encoder, skipped\n";
			continue;
		}
		auto settings = base;
		settings.codec = simple_player_video_codec::hevc;
		settings.chroma = hevc.chroma;
		settings.bit_depth = hevc.depth;
		settings.matrix = simple_player_video_matrix::bt601;
		settings.rate_control = simple_player_video_rate_control::constant_qp;
		settings.qp = 20;
		settings.audio_codec = simple_player_video_audio_codec::flac;
		settings.audio_bits = 24;
		settings.audio_sample_rate = 0;
		const auto output = directory / "hevc-case.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case(hevc.label, result);
		const auto bytes = read_bytes(output);
		const auto* hvcc = find_box(bytes, "hvcC", 18);
		check(result.ok && hvcc && (hvcc[16] & 3) == hevc.chroma_format &&
			(hvcc[17] & 7) + 8 == hevc.depth,
			"HEVC stores the requested chroma format and bit depth");
		const auto signal = stream_signal(bytes, true);
		check(signal.parsed && !signal.full_range && signal.colour_description_present &&
			(signal.matrix == 5 || signal.matrix == 6),
			"HEVC signals limited range and the BT.601 matrix it was given");
	}
	if (accepts(simple_player_video_codec::hevc, simple_player_video_chroma::yuv420, 0))
	{
		// Encoders that cannot mark full range are refused before rendering.
		auto settings = base;
		settings.codec = simple_player_video_codec::hevc;
		settings.full_range = true;
		const auto output = directory / "hevc-full-range.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case("HEVC full range", result);
		const auto signal = stream_signal(read_bytes(output), true);
		check(result.ok ? signal.parsed && signal.signal_type_present && signal.full_range
			: result.error.find("marks the video as limited range") != std::string::npos &&
				no_partial_outputs(directory, output),
			"HEVC full range is either signalled or refused before rendering");
	}

	if (std::any_of(encoders.begin(), encoders.end(), [](const auto& encoder)
		{
			return encoder.codec == simple_player_video_codec::av1;
		}))
	{
		auto settings = base;
		settings.width = 1280;
		settings.height = 720;
		settings.codec = simple_player_video_codec::av1;
		settings.bit_depth = 10;
		settings.rate_control = simple_player_video_rate_control::quality;
		const auto output = directory / "av1.mp4";
		const std::vector<std::uint8_t> sentinel{'k', 'e', 'e', 'p'};
		write_bytes(output, sentinel);
		const auto result = render_codec_case(output, settings, preferences);
		report_case("AV1 10-bit", result);
		if (accepts(simple_player_video_codec::av1, simple_player_video_chroma::yuv420, 1))
			check(result.ok && contains_atom(read_bytes(output), "av01"),
				"AV1 renders where the MP4 writer can store it");
		else
			check(!result.ok && result.error.find("cannot store AV1") != std::string::npos &&
				no_partial_outputs(directory, output),
				"AV1 without MP4 support fails before rendering and leaves no partial file");
	}

	const struct
	{
		const char* label;
		simple_player_video_codec codec;
		simple_player_video_audio_codec audio;
		std::uint32_t bits;
	} asf_cases[] = {
		{"WMV 9 + WMA", simple_player_video_codec::wmv9, simple_player_video_audio_codec::wma, 16},
		{"VC-1 + WMA Pro 24", simple_player_video_codec::vc1, simple_player_video_audio_codec::wma_pro, 24},
		{"WMV 9 + PCM float", simple_player_video_codec::wmv9, simple_player_video_audio_codec::pcm, 32},
	};
	for (const auto& asf : asf_cases)
	{
		if (!accepts(asf.codec, simple_player_video_chroma::yuv420, 0))
		{
			std::cout << asf.label << ": no encoder, skipped\n";
			continue;
		}
		auto settings = base;
		settings.codec = asf.codec;
		settings.audio_codec = asf.audio;
		settings.audio_bits = asf.bits;
		settings.audio_bitrate_kbps = 128;
		settings.rate_control = simple_player_video_rate_control::quality;
		const auto output = directory / "asf-case.wmv";
		const auto result = render_codec_case(output, settings, preferences);
		report_case(asf.label, result);
		check(result.ok && is_asf(read_bytes(output)), "WMV codecs write an ASF file");
	}

	{
		// 1080p converts in parallel row bands; the second pixel lies in a
		// late band, above the keyboard.
		auto settings = base;
		settings.width = 1920;
		settings.height = 1080;
		settings.fps = 2;
		settings.background_rgb = 0x20A0F0;
		settings.rate_control = simple_player_video_rate_control::constant_qp;
		settings.qp = 12;
		const auto output = directory / "colour.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		for (const std::uint32_t y : {8U, 860U})
		{
			std::array<std::uint8_t, 3> bgr{};
			const bool decoded = result.ok && decoded_pixel(output, 8, y, bgr);
			std::cout << "Background at row " << y << " decodes as " << int(bgr[2]) << ',' <<
				int(bgr[1]) << ',' << int(bgr[0]) << '\n';
			check(decoded && near_value(bgr[2], 0x20, 6) && near_value(bgr[1], 0xA0, 6) &&
				near_value(bgr[0], 0xF0, 6),
				"the background colour survives BT.709 limited-range H.264 in every row band");
		}
		const auto signal = stream_signal(read_bytes(output), false);
		check(signal.parsed && !signal.full_range,
			"limited-range H.264 is not marked full range");
	}
	for (const bool hardware : {true, false})
	{
		// Media Foundation's decoder ignores the full-range flag, so the
		// stream's own signalling is what the check reads.
		auto settings = base;
		settings.hardware_encoder = hardware;
		settings.full_range = true;
		const auto output = directory / "full-range.mp4";
		const auto result = render_codec_case(output, settings, preferences);
		report_case(hardware ? "H.264 full range" : "software H.264 full range", result);
		const auto signal = stream_signal(read_bytes(output), false);
		check(result.ok ? signal.parsed && signal.signal_type_present && signal.full_range
			: result.error.find("marks the video as limited range") != std::string::npos,
			"H.264 full range is either signalled or refused before rendering");
	}

	{
		auto settings = test_settings();
		settings.codec = simple_player_video_codec::wmv9;
		settings.audio_codec = simple_player_video_audio_codec::wma;
		settings.audio_bitrate_kbps = 128;
		check(simple_player_video_settings_problem(settings, preferences).empty(),
			"WMV with WMA is a valid combination");
		const auto output = directory / "wrong-extension.mp4";
		vector_event_source audio(1'000'000, {});
		vector_event_source visual(1'000'000, {});
		const auto result = render_simple_player_video_events(L"codec.mid", audio, visual,
			0, output.wstring(), {}, preferences, settings, nullptr, nullptr, nullptr);
		check(!result.ok && result.error.find(".wmv") != std::string::npos,
			"WMV codecs require the .wmv extension");
		settings.audio_codec = simple_player_video_audio_codec::aac;
		check(simple_player_video_settings_problem(settings, preferences).find("WMV video carries") == 0,
			"AAC is rejected in WMV");
		settings = test_settings();
		settings.chroma = simple_player_video_chroma::yuv444;
		check(simple_player_video_settings_problem(settings, preferences).find("Only HEVC") == 0,
			"H.264 4:4:4 is rejected");
		settings = test_settings();
		settings.cabac = 1;
		settings.h264_profile = simple_player_video_h264_profile::baseline;
		check(simple_player_video_settings_problem(settings, preferences).find("CABAC") == 0,
			"CABAC with the Baseline profile is rejected");
	}
}
}

int main(int argc, char** argv)
{
	test_text_box_layout_isolation();
	test_visual_storage_has_no_artificial_limit();
	test_held_notes_do_not_block_reclamation();
	test_player_slider_seek_restart();
	test_stop_interrupts_distant_event_wait();
	test_shutdown_rejects_late_playback();
	const auto directory = std::filesystem::absolute(
		argc > 1 ? std::filesystem::path(argv[1]) :
		std::filesystem::path("player-video-test-data"));
	std::filesystem::create_directories(directory);
	const auto midi_path = directory / "smpte.mid";
	write_bytes(midi_path, smpte_fixture());
	test_file_player_slider_seek(midi_path);
	const auto prepared_midi_path = directory / "prepared.mid";
	write_bytes(prepared_midi_path, ppq_fixture());
	const std::vector<std::uint8_t> sentinel{'k', 'e', 'e', 'p'};
	const auto preferences = test_preferences();

	{
		std::string error;
		auto prepared = compressed_midi_event_source::open(
			prepared_midi_path.wstring(), {}, nullptr, error);
		check(prepared != nullptr && error.empty(),
			"plain MIDI can be prepared into the compressed page store");
		if (prepared)
		{
			auto audio = prepared->fork_reader();
			auto visual = prepared->fork_reader();
			generated_event first_audio;
			generated_event first_visual;
			check(audio->next(first_audio) && visual->next(first_visual) &&
				first_audio.short_msg == first_visual.short_msg &&
				first_audio.time_us == first_visual.time_us,
				"prepared page-store forks have independent equivalent cursors");
			audio->rewind();
			visual->rewind();
			const auto output = directory / "prepared-page-store.mp4";
			std::filesystem::remove(output);
			const auto result = render_simple_player_video_events(
				prepared_midi_path.wstring(), *audio, *visual, prepared->event_count(),
				output.wstring(), {}, preferences, test_settings(), nullptr, nullptr, nullptr);
			check(result.ok && !read_bytes(output).empty(),
				"prepared compressed-page cursors render without raw MIDI materialization");
		}
	}

	{
		const auto output = directory / "invalid-aac-bitrate.mp4";
		write_bytes(output, sentinel);
		auto settings = test_settings(48000, 200);
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, preferences, settings, nullptr, nullptr, nullptr);
		check(!result.ok && result.error.find("AAC bitrate") != std::string::npos &&
			read_bytes(output) == sentinel && no_partial_outputs(directory, output),
			"unsupported AAC bitrate is rejected without touching the destination");
	}

	{
		const auto output = directory / "invalid-aac-rate.mp4";
		write_bytes(output, sentinel);
		auto settings = test_settings(96000, 192);
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, preferences, settings, nullptr, nullptr, nullptr);
		check(!result.ok && result.error.find("AAC sample rate") != std::string::npos &&
			read_bytes(output) == sentinel && no_partial_outputs(directory, output),
			"unsupported AAC rate is rejected without touching the destination");
	}

	{
		const auto output = directory / "pre-cancel.mp4";
		write_bytes(output, sentinel);
		std::atomic_bool cancel{true};
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, preferences, test_settings(), &cancel, nullptr, nullptr);
		check(result.cancelled && cancel.load() && read_bytes(output) == sentinel &&
			no_partial_outputs(directory, output),
			"a caller-owned pre-cancel is preserved and no output is started");
	}

	{
		const auto collision = directory / "collision.mp4";
		write_bytes(collision, smpte_fixture());
		const auto before = read_bytes(collision);
		const auto result = render_simple_player_video(collision.wstring(), collision.wstring(),
			{}, preferences, test_settings(), nullptr, nullptr, nullptr);
		check(!result.ok && result.error.find("must not overwrite") != std::string::npos &&
			read_bytes(collision) == before,
			"source/output path collisions are rejected before opening the destination");
	}

	{
		const auto output = directory / "smpte-supported.mp4";
		std::filesystem::remove(output);
		progress_capture progress;
		auto mux_preferences = preferences;
		mux_preferences.limiter_enabled = true;
		mux_preferences.phase_mode = syncore_phase_mode::analytic;
		auto mux_settings = test_settings(44100, 96);
		mux_settings.width = 640;
		mux_settings.height = 360;
		mux_settings.fps = 60;
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, mux_preferences, mux_settings, nullptr,
			capture_progress, &progress);
		const auto bytes = read_bytes(output);
		check(result.ok && result.video_frames == 240 && result.audio_frames == 192000 &&
			progress.calls < 100 && contains_atom(bytes, "vide") &&
			contains_atom(bytes, "soun") && contains_atom(bytes, "avc1") &&
			contains_atom(bytes, "mp4a") &&
			(progress.saw_accelerated_video || progress.saw_software_video) &&
			(progress.saw_framebuffer_video || progress.saw_backbuffer_video) &&
			progress.saw_non_background_preview,
			"SMPTE timing, SYNCore-to-AAC resampling, bounded progress, and muxed streams render together");
		check(progress.saw_preparation_start && progress.saw_preparation_complete &&
			progress.saw_rendering_after_preparation && progress.preparation_status_valid,
			"sample preparation counts reach the export UI from zero through completion and then rendering");
		std::cout << "SMPTE video path: " <<
			(progress.saw_accelerated_video ? "accelerated OpenGL" :
				"software OpenGL fallback") <<
			(progress.saw_framebuffer_video ? " FBO" : " backbuffer") << '\n';
	}

	{
		constexpr std::uint64_t program_duration_us = 30'000'000;
		std::vector<generated_event> events{
			short_event(0, 0x90, 60, 100),
			short_event(program_duration_us, 0x80, 60, 0)};
		vector_event_source audio(program_duration_us, events);
		vector_event_source visual(program_duration_us, events);
		auto settings = test_settings();
		settings.width = 320;
		settings.height = 180;
		settings.fps = 60;
		const auto output = directory / "sustained-mux.mp4";
		std::filesystem::remove(output);
		const auto started = std::chrono::steady_clock::now();
		const auto result = render_simple_player_video_events(L"sustained.mid",
			audio, visual, events.size(), output.wstring(), {}, preferences, settings,
			nullptr, nullptr, nullptr);
		const auto elapsed = std::chrono::steady_clock::now() - started;
		const auto elapsed_milliseconds =
			std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
		check(result.ok && result.video_frames == 1980 &&
			result.audio_frames == 1'584'000 && elapsed < std::chrono::seconds(60),
			"sustained timestamp-ordered audio/video muxing does not stall");
		std::cout << "Sustained 33-second mux render: " << elapsed_milliseconds << " ms\n";
	}

	{
		const auto output = directory / "preparation-cancel.mp4";
		write_bytes(output, sentinel);
		progress_capture progress;
		progress.cancel_during_preparation = true;
		auto phase_preferences = preferences;
		phase_preferences.phase_mode = syncore_phase_mode::analytic;
		std::atomic_bool cancel{false};
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, phase_preferences, test_settings(), &cancel, capture_progress, &progress);
		check(progress.saw_preparation_start && result.cancelled && !cancel.load() &&
			read_bytes(output) == sentinel && no_partial_outputs(directory, output),
			"cancelling at known preparation progress preserves the destination and caller state");
	}

	{
		const auto output = directory / "mid-cancel.mp4";
		write_bytes(output, sentinel);
		progress_capture progress;
		progress.cancel_after_video_frame = true;
		std::atomic_bool cancel{false};
		const auto result = render_simple_player_video(midi_path.wstring(), output.wstring(),
			{}, preferences, test_settings(), &cancel, capture_progress, &progress);
		check(result.cancelled && !cancel.load() && read_bytes(output) == sentinel &&
			no_partial_outputs(directory, output),
			"mid-render cancellation preserves output and does not mutate caller state");
	}

	{
		const auto output = directory / "source-error.mp4";
		write_bytes(output, sentinel);
		vector_event_source audio(100000, {}, true);
		vector_event_source visual(100000, {});
		std::atomic_bool cancel{false};
		const auto result = render_simple_player_video_events(L"synthetic.mid", audio, visual,
			0, output.wstring(), {}, preferences, test_settings(), &cancel, nullptr, nullptr);
		check(!result.ok && !result.cancelled &&
			result.error.find("synthetic event source failure") != std::string::npos &&
			!cancel.load() && read_bytes(output) == sentinel,
			"a substantive worker exception wins over peer cancellation");
	}

	{
		std::vector<generated_event> events;
		for (std::uint8_t key = 0; key < 64; ++key)
			events.push_back(short_event(0, 0x90, key, 100));
		for (std::uint8_t key = 0; key < 64; ++key)
			events.push_back(short_event(100000, 0x80, key, 0));
		vector_event_source audio(100000, events);
		vector_event_source visual(100000, events);
		auto settings = test_settings();
		auto constrained_preferences = preferences;
		constrained_preferences.maximum_cohorts = 1;
		progress_capture progress;
		const auto output = directory / "prepared-cohort-warning.mp4";
		std::filesystem::remove(output);
		const auto result = render_simple_player_video_events(L"prepared.mid", audio, visual,
			events.size(), output.wstring(), {}, constrained_preferences, settings,
			nullptr, capture_progress, &progress);
		check(result.ok && result.cohort_capacity_steals != 0 && !result.warning.empty() &&
			progress.maximum_active_cohorts <= constrained_preferences.maximum_cohorts &&
			progress.maximum_reported_peak_cohorts <= constrained_preferences.maximum_cohorts &&
			!read_bytes(output).empty(),
			"prepared-event export enforces and reports the configured cohort ceiling");
	}

	test_yuv_conversion();
	test_codecs(directory, preferences);

	if (failures != 0)
	{
		std::cerr << failures << " player video export test(s) failed\n";
		return 1;
	}
	std::cout << "All player video export tests passed\n";
	return 0;
}
