#define NOMINMAX
#include <array>

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
	check(visuals.falling_notes[key].front().end_time_us.load(
		std::memory_order_relaxed) == 30,
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
	player.stop();
	playback.join();
	const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
	check(stop_elapsed < std::chrono::milliseconds(250),
		"Stop interrupts a sender waiting for a distant event");
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
}

int main(int argc, char** argv)
{
	test_text_box_layout_isolation();
	test_visual_storage_has_no_artificial_limit();
	test_player_slider_seek_restart();
	test_stop_interrupts_distant_event_wait();
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

	if (failures != 0)
	{
		std::cerr << failures << " player video export test(s) failed\n";
		return 1;
	}
	std::cout << "All player video export tests passed\n";
	return 0;
}
