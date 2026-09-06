#include "app_state.h"
#include "player_controls.h"
#include "playback_source.h"
#include "settings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>

void report_player_output_error()
{
	auto detail = player ? player->get_last_output_error() : std::string{};
	if (detail.empty())
		throw_alert_error(std::string("No MIDI output device is available for playback"));
	else
		throw_alert_error(std::move(detail));
}

static void player_watch_func(std::stop_token stop_token)
{
	if (gui_stop_requested(stop_token))
		return;
	global_window_handler->enable_window("SIMPLAYER");
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto textbox = (text_box*)(*window)["TEXT"];
	auto seek_to_slider = (slider*)(*window)["SEEK_TO"];

	textbox->safe_string_replace("Opening and reading first track");

	// Populate the device list
	update_device_list();

	// Update pause button to reflect initial paused state
	auto pause_button = (button*)(*window)["PAUSE"];
	pause_button->safe_string_replace("\200");  // Play symbol (since it's paused)

	auto& info = player->get_info();
	while (!gui_stop_requested(stop_token))
	{
		if (!window->drawable)
		{
			player->stop();
			return;
		}

		uint64_t scanned = info.scanned;
		uint64_t size = info.size;

		auto str = std::format("Read {} out of {} ~ {:3.2f}%", scanned, size, scanned * 100.f / size);
		textbox->safe_string_replace(str);

		if (info.open_complete)
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if (gui_stop_requested(stop_token))
		return;

	auto& state = player->get_state();

	bool was_playing = false;
	while (!gui_stop_requested(stop_token) &&
		(!was_playing || state.playing.load(std::memory_order_acquire)))
	{
		if (!window->drawable)
		{
			player->stop();
			break;
		}

		const auto current_us = player->get_position_us();
		auto seconds = current_us / 1000000;
		auto parts_of_second = current_us % 1000000;

		auto total_us = player->get_info().total_duration_us;
		auto position = total_us == 0 ? 0.f : float(current_us) / total_us;

		auto str = std::format("{:0>2}:{:0>2}:{:0>2}", seconds / 60, seconds % 60, parts_of_second / 10000);
		const auto lead_in_us = player->get_start_lead_in_remaining_us();
		if (lead_in_us != 0)
			str = std::format("Starts in {:.1f}s", lead_in_us / 1000000.0);
		textbox->safe_string_replace(str);

		if (!seek_to_slider->dragging && !player->is_seeking())
			seek_to_slider->set_value(position, false);

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		bool is_playing = state.playing;
		if (is_playing)
			was_playing = true;
	}
}

bool try_open_drop_in_player(const std::vector<std::wstring>& filenames)
{
	if (!global_window_handler || !player)
		return false;

	std::string active_window;
	{
		std::lock_guard locker(global_window_handler->lock);
		const auto& active_windows = global_window_handler->active_windows;
		if (active_windows.empty())
			return false;
		active_window = active_windows.front()->first;
		if (active_window != "SIMPLAYER" && active_window != "ARCHIVE_SOURCE")
			return false;
	}

	auto filename = std::find_if(filenames.begin(), filenames.end(),
		[](const std::wstring& value) { return !value.empty(); });
	if (filename == filenames.end())
		return true;

	open_player_file(*filename);

	return true;
}

void update_device_list()
{
	if (!player)
		return;

	auto device_list = _WH_t<selectable_properted_list>("SIMPLAYER", "DEVICE_LIST");

	// Rebuild both the text model and its visible row buttons. Clearing only
	// selectors_text leaves the previous buttons alive and stacks duplicate
	// rows every time the MIDI devices are refreshed.
	device_list->safe_clear();

	// Get device names from player
	auto device_names = player->get_device_names();

	// Populate the list
	for (const auto& name : device_names)
		device_list->safe_push_back_new_string(name);

	// Select the current device
	size_t current = player->get_current_device();
	if (current < device_names.size())
		device_list->selected_id.push_back(static_cast<uint32_t>(current));

	device_list->safe_update_lines();
}

void on_device_select(int device_id)
{
	worker_singleton<struct midi_out_selct>::instance().push([device_id](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		if (!player->set_device(device_id))
		{
			if (player->is_playing())
				throw_alert_warning("Stop playback before changing the MIDI output device");
			return;
		}

		auto device_list = _WH_t<selectable_properted_list>("SIMPLAYER", "DEVICE_LIST");

		// Clear previous selection and select the new device
		device_list->selected_id.clear();
		device_list->selected_id.push_back(static_cast<uint32_t>(device_id));

		// Save the device name to registry
		auto device_names = player->get_device_names();
		if (device_id >= 0 && device_id < device_names.size())
		{
			std::string device_name = device_names[device_id];
			std::wstring wdevice_name(device_name.begin(), device_name.end());
			saved_midi_device_name = wdevice_name;

			try
			{
				settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
				settings::regestry_access.SetStringValue(L"MIDI_DEVICE_NAME", wdevice_name);
				settings::regestry_access.Close();
			}
			catch (...)
			{
				std::cout << "Exception thrown while saving MIDI_DEVICE_NAME to registry\n";
			}
		}
	});
}

void on_player_pause_toggle()
{
	if (!player->is_playing())
	{
		if (restart_selected_compressed_source())
			return;

		std::wstring filename = player->get_filename();
		if (filename.empty())
			return;

		worker_singleton<struct player_watcher>::instance().push(player_watch_func);
		worker_singleton<struct player_thread>::instance().push([filename](std::stop_token stop_token)
		{
			if (gui_stop_requested(stop_token))
				return;
			std::stop_callback cancellation(stop_token, []()
			{
				if (player)
					player->stop();
			});
			if (!player->ensure_output(saved_midi_device_name))
			{
				if (!gui_stop_requested(stop_token))
					report_player_output_error();
				return;
			}
			if (gui_stop_requested(stop_token))
				return;
			player->simple_run(filename);
		});
		return;
	}

	player->toggle_pause();

	auto window = (*global_window_handler)["SIMPLAYER"];
	auto pause = (button*)(*window)["PAUSE"];

	if (player->is_paused())
		pause->safe_string_replace("\200");
	else
		pause->safe_string_replace("\202");
}

void on_player_stop()
{
	player->stop();

	worker_singleton<struct player_thread>::instance().push([](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		auto window = (*global_window_handler)["SIMPLAYER"];
		auto pause = (button*)(*window)["PAUSE"];
		auto textbox = (text_box*)(*window)["TEXT"];

		if (player->is_paused())
			pause->safe_string_replace("\200");
		else
			pause->safe_string_replace("\202");

		textbox->safe_string_replace("Playback was reset, please restart the player");
	});
}

void on_view_length_change(float value)
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];

	player_view->data->scroll_window_us = std::pow(2, value);
}

void on_unbuffered_switch()
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto buffering_switch = (button*)(*window)["BUFFERING_SWITCH"];

	player_view->data->enable_simulated_lag ^= true;

	buffering_switch->safe_string_replace(player_view->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered");
}

void on_overlap_removal_switch_action(bool with_increment)
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto overlap_switch = (button*)(*window)["OVERLAP_SWITCH"];

	if (with_increment)
	{
		player_view->data->remove_overlaps += 1;
		if (player_view->data->remove_overlaps > 1)
			player_view->data->remove_overlaps = 0xFF;
	}

	const char* states[] = {"Overlaps drawn", "Naive OR", "R/t OR (Beta)"};

	overlap_switch->safe_string_replace(states[(player_view->data->remove_overlaps + 1) & 0xFF]);
}

void on_overlap_removal_switch()
{
	on_overlap_removal_switch_action(true);
}

void on_playback_seek_to(float value)
{
	if (!player || !player->is_playing())
		return;

	player->seek_to(value);
}

void open_regular_midi_file(std::wstring filename)
{
	if (filename.empty() || !player)
		return;

	select_regular_player_source();
	player->stop();
	global_window_handler->disable_window("ARCHIVE_SOURCE");

	worker_singleton<struct player_thread>::instance().push(
		[filename = std::move(filename)](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		std::stop_callback cancellation(stop_token, []()
		{
			if (player)
				player->stop();
		});
		if (!player->ensure_output(saved_midi_device_name))
		{
			if (!gui_stop_requested(stop_token))
				report_player_output_error();
			return;
		}
		if (gui_stop_requested(stop_token))
			return;

		worker_singleton<struct player_watcher>::instance().push(player_watch_func);
		player->simple_run(filename);
	});
}

