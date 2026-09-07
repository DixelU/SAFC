#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../SAFC_InnerModules/syncore_output.h"

struct playback_event_source;

namespace safc::imgui_ui
{
struct playback_snapshot
{
	bool busy = false;
	bool playing = false;
	bool paused = false;
	bool seeking = false;
	bool stopping = false;
	std::uint64_t position_us = 0;
	std::uint64_t duration_us = 0;
	std::uint64_t scanned_bytes = 0;
	std::uint64_t total_bytes = 0;
	std::uint64_t lead_in_us = 0;
	std::string message;
	std::string error;
	bool waiting_for_member = false;
	std::uint32_t archive_layer = 0;
	std::vector<std::string> archive_members;
	std::uint64_t source_events = 0;
	std::uint16_t source_tracks = 0;
	std::uint32_t source_archive_depth = 0;
};

// Owns one playback run and its output. Public methods belong to the UI thread;
// parsing, output setup, and blocking playback run on the owned worker.
class playback_session
{
public:
	playback_session();
	~playback_session();
	playback_session(const playback_session&) = delete;
	playback_session& operator=(const playback_session&) = delete;

	bool open(std::wstring path, bool silent = false, bool start_paused = true);
	bool restart(bool silent = false, bool start_paused = false);
	using source_factory = std::function<std::shared_ptr<playback_event_source>()>;
	bool open_external(std::shared_ptr<playback_event_source> source,
		bool start_paused = false, double seek_fraction = 0,
		source_factory export_factory = {});
	void choose_archive_member(std::size_t index);
	// The active cursor must never be iterated by another consumer. Export uses
	// export_source_factory() to create independent cursors over immutable data.
	std::shared_ptr<playback_event_source> current_source() const;
	source_factory export_source_factory() const;
	std::wstring current_path() const;
	std::wstring bank_path() const;
	syncore_preferences synth_preferences() const;
	bool audition_note(std::uint8_t key, std::uint8_t velocity,
		std::uint8_t channel, bool on);
	void set_visual_options(bool simulated_lag, std::uint8_t overlap_mode);
	void toggle_pause();
	void seek(double fraction);
	void stop();
	void shutdown();
	playback_snapshot snapshot();

	std::vector<std::string> device_names() const;
	void select_device(std::size_t index);
	std::size_t selected_device() const;
	bool syncore_available() const;
	void configure_synth(std::wstring bank, const syncore_preferences& preferences);

	// Draws in the caller's compatibility OpenGL context and 0..width/0..height
	// projection. All GL resources and per-frame draw data stay on the UI thread.
	void draw_visuals(float width, float height, float visible_seconds);

private:
	bool start(std::wstring path, std::shared_ptr<playback_event_source> source,
		bool silent, bool start_paused, double seek_fraction, source_factory export_factory);
	struct impl;
	std::unique_ptr<impl> impl_;
};
}
