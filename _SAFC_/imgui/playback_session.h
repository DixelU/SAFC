#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../SAFC_InnerModules/syncore_output.h"

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
	struct impl;
	std::unique_ptr<impl> impl_;
};
}
