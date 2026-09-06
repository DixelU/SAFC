#include "app_state.h"
#include "playback_source.h"
#include "dialogs.h"
#include "player_controls.h"

#include "../SAFC_InnerModules/compressed_midi_event_source.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <boost/algorithm/string.hpp>

// ============================================================================
// Unified MIDI / nested-archive playback source dialog
// ============================================================================

static std::mutex compressed_player_source_mutex;
static std::shared_ptr<compressed_midi_event_source> compressed_player_source;
static std::wstring compressed_player_filename;
std::atomic<bool> compressed_player_preparing = false;
std::atomic<bool> compressed_player_cancel = false;
static std::atomic<bool> compressed_player_playback_requested = false;
std::atomic<bool> compressed_player_source_selected = false;

void compressed_player_status(const std::string& message)
{
	if (auto status = _WH_t<text_box>("ARCHIVE_SOURCE", "TEXT"))
		status->safe_string_replace(message);
}

void select_regular_player_source()
{
	compressed_player_source_selected.store(false, std::memory_order_release);
}

std::shared_ptr<compressed_midi_event_source> current_compressed_player_source()
{
	std::lock_guard locker(compressed_player_source_mutex);
	return compressed_player_source;
}

std::wstring current_compressed_player_filename()
{
	std::lock_guard locker(compressed_player_source_mutex);
	return compressed_player_filename;
}

static void compressed_player_watch_func(std::stop_token stop_token)
{
	if (gui_stop_requested(stop_token))
		return;
	global_window_handler->enable_window("SIMPLAYER");
	update_device_list();

	if (auto pause = _WH_t<button>("SIMPLAYER", "PAUSE"))
		pause->safe_string_replace("\200");

	while (compressed_player_playback_requested.load(std::memory_order_acquire) &&
		!player->is_playing() && !gui_stop_requested(stop_token))
		std::this_thread::sleep_for(std::chrono::milliseconds(2));

	while (compressed_player_playback_requested.load(std::memory_order_acquire) &&
		player->is_playing() && !gui_stop_requested(stop_token))
	{
		auto window = (*global_window_handler)["SIMPLAYER"];
		if (!window->drawable)
		{
			player->stop();
			break;
		}

		const auto current_us = player->get_position_us();
		const auto total_us = player->get_info().total_duration_us;
		const auto seconds = current_us / 1000000;
		const auto parts_of_second = current_us % 1000000;
		if (auto status = _WH_t<text_box>("SIMPLAYER", "TEXT"))
		{
			auto str = std::format(
				"{:0>2}:{:0>2}:{:0>2}", seconds / 60, seconds % 60,
				parts_of_second / 10000);
			const auto lead_in_us = player->get_start_lead_in_remaining_us();
			if (lead_in_us != 0)
				str = std::format("Starts in {:.1f}s", lead_in_us / 1000000.0);
			status->safe_string_replace(str);
		}

		if (auto seek = _WH_t<slider>("SIMPLAYER", "SEEK_TO");
			seek && !seek->dragging && !player->is_seeking() && total_us != 0)
			seek->set_value(static_cast<float>(current_us) / total_us, false);

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	if (gui_stop_requested(stop_token))
		return;
	if (auto status = _WH_t<text_box>("SIMPLAYER", "TEXT"))
		status->safe_string_replace("Stopped - press Play to restart the prepared archive");
	if (auto pause = _WH_t<button>("SIMPLAYER", "PAUSE"))
		pause->safe_string_replace("\200");
}

static void play_compressed_source_in_worker(
	const std::shared_ptr<compressed_midi_event_source>& source,
	std::stop_token stop_token = {})
{
	if (!source || gui_stop_requested(stop_token))
		return;
	std::stop_callback cancellation(stop_token, []()
	{
		if (player)
			player->stop();
	});

	bool expected = false;
	if (!compressed_player_playback_requested.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel))
		return;

	struct playback_request_guard
	{
		~playback_request_guard()
		{
			compressed_player_playback_requested.store(false, std::memory_order_release);
		}
	} guard;

	if (!player->ensure_output(saved_midi_device_name))
	{
		if (!gui_stop_requested(stop_token))
			report_player_output_error();
		return;
	}
	if (gui_stop_requested(stop_token))
		return;

	worker_singleton<struct compressed_player_watcher>::instance().push(
		compressed_player_watch_func);
	// Match the normal player: prepare everything, then wait paused for Play.
	player->run_from_external(source.get(), 0.0, true);
}

bool restart_selected_compressed_source()
{
	if (!compressed_player_source_selected.load(std::memory_order_acquire))
		return false;

	auto source = current_compressed_player_source();
	if (!source || compressed_player_preparing.load(std::memory_order_acquire))
		return true;

	worker_singleton<struct player_thread>::instance().push(
		[source](std::stop_token stop_token)
	{
		play_compressed_source_in_worker(source, stop_token);
	});
	return true;
}

static void open_compressed_midi_file(std::wstring filename)
{
	if (filename.empty() || !player)
		return;

	bool expected = false;
	if (!compressed_player_preparing.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel))
	{
		throw_alert_warning("A compressed MIDI is already being prepared");
		return;
	}

	compressed_player_cancel.store(false, std::memory_order_release);
	select_regular_player_source();
	player->stop();

	global_window_handler->enable_window("ARCHIVE_SOURCE");
	compressed_player_status("Opening archive pipeline...");

	worker_singleton<struct player_thread>::instance().push(
		[filename = std::move(filename)](std::stop_token stop_token)
	{
		struct preparation_guard
		{
			~preparation_guard()
			{
				compressed_player_preparing.store(false, std::memory_order_release);
			}
		} guard;
		if (gui_stop_requested(stop_token))
			return;
		std::stop_callback cancellation(stop_token, []()
		{
			compressed_player_cancel.store(true, std::memory_order_release);
		});

		std::string error;
		auto source = compressed_midi_event_source::open(
			filename,
			[](const std::string& message) { compressed_player_status(message); },
			&compressed_player_cancel,
			error);
		if (!source)
		{
			if (error != "Compressed MIDI preparation was cancelled")
				throw_alert_error("Unable to prepare compressed MIDI: " + error);
			compressed_player_status(error);
			return;
		}
		if (gui_stop_requested(stop_token))
			return;

		{
			std::lock_guard locker(compressed_player_source_mutex);
			compressed_player_source = source;
			compressed_player_filename = filename;
		}

		compressed_player_source_selected.store(true, std::memory_order_release);
		compressed_player_preparing.store(false, std::memory_order_release);
		compressed_player_status(std::format(
			"Prepared {} events in {} tracks from {} archive layer(s)",
			source->event_count(), source->track_count(), source->archive_depth()));
		global_window_handler->enable_window("SIMPLAYER");
		global_window_handler->disable_window("ARCHIVE_SOURCE");
		play_compressed_source_in_worker(source, stop_token);
	});
}

static bool has_midi_extension(const std::wstring& filename)
{
	const auto extension = std::filesystem::path(filename).extension().wstring();
	return boost::iequals(extension, L".mid") || boost::iequals(extension, L".midi");
}

void open_player_file(std::wstring filename)
{
	if (filename.empty())
		return;

	if (has_midi_extension(filename))
	{
		if (compressed_player_preparing.load(std::memory_order_acquire))
		{
			throw_alert_warning("A compressed MIDI is already being prepared");
			return;
		}
		open_regular_midi_file(std::move(filename));
	}
	else
		open_compressed_midi_file(std::move(filename));
}

void on_player_source_open()
{
	auto filename = playback_source_open_file_dialog();
	if (gui_stop_requested())
		return;
	open_player_file(std::move(filename));
}

void on_compressed_preparation_cancel()
{
	if (compressed_player_preparing.load(std::memory_order_acquire))
	{
		compressed_player_cancel.store(true, std::memory_order_release);
		compressed_player_status("Cancelling preparation...");
	}
	else
		global_window_handler->disable_window("ARCHIVE_SOURCE");
}

