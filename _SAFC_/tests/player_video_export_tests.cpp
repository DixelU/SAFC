#include <array>

#include "../SAFC_InnerModules/simple_player_video_export.h"
#include "../SAFC_InnerModules/playback_event_source.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
	settings.maximum_cohorts = 0;
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
	bool cancel_after_video_frame = false;
	bool saw_accelerated_video = false;
	bool saw_software_video = false;
	bool saw_non_background_preview = false;
};

bool capture_progress(const simple_player_video_progress& progress,
	void* user_data) noexcept
{
	auto& capture = *static_cast<progress_capture*>(user_data);
	++capture.calls;
	capture.saw_accelerated_video = capture.saw_accelerated_video ||
		progress.stage.find("accelerated OpenGL") != std::string::npos;
	capture.saw_software_video = capture.saw_software_video ||
		progress.stage.find("software OpenGL") != std::string::npos;
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
	const auto directory = std::filesystem::absolute(
		argc > 1 ? std::filesystem::path(argv[1]) :
		std::filesystem::path("player-video-test-data"));
	std::filesystem::create_directories(directory);
	const auto midi_path = directory / "smpte.mid";
	write_bytes(midi_path, smpte_fixture());
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
		check(result.ok && result.video_frames == 240 && result.audio_frames == 176400 &&
			progress.calls < 100 && contains_atom(bytes, "vide") &&
			contains_atom(bytes, "soun") && contains_atom(bytes, "avc1") &&
			contains_atom(bytes, "mp4a") &&
			(progress.saw_accelerated_video || progress.saw_software_video) &&
			progress.saw_non_background_preview,
			"SMPTE timing, supported AAC, bounded progress, and muxed streams render together");
		std::cout << "SMPTE video path: " <<
			(progress.saw_accelerated_video ? "accelerated OpenGL" :
				"software OpenGL fallback") << '\n';
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
		settings.maximum_cohorts = 1;
		const auto output = directory / "prepared-cohort-warning.mp4";
		std::filesystem::remove(output);
		const auto result = render_simple_player_video_events(L"prepared.mid", audio, visual,
			events.size(), output.wstring(), {}, preferences, settings, nullptr, nullptr, nullptr);
		check(result.ok && result.cohort_capacity_steals != 0 && !result.warning.empty() &&
			!read_bytes(output).empty(),
			"prepared-event export works and reports an explicit cohort-capacity warning");
	}

	if (failures != 0)
	{
		std::cerr << failures << " player video export test(s) failed\n";
		return 1;
	}
	std::cout << "All player video export tests passed\n";
	return 0;
}
