#include "app_state.h"
#include "syncore_settings.h"
#include "dialogs.h"
#include "player_controls.h"
#include "settings.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>

static const char* syncore_phase_mode_name(syncore_phase_mode mode)
{
	switch (mode)
	{
		case syncore_phase_mode::coherent: return "Coherent";
		case syncore_phase_mode::random_polarity: return "Random polarity";
		case syncore_phase_mode::analytic: return "Analytic";
		case syncore_phase_mode::smooth_field: return "Smooth field";
		case syncore_phase_mode::independent_bins: return "Independent bins";
	}
	return "Coherent";
}

static std::string syncore_bank_display_name()
{
	if (saved_syncore_bank_path.empty())
		return "Bank: built-in sine (choose SF2/SFZ below)";

	const auto filename = std::filesystem::path(saved_syncore_bank_path).filename().wstring();
	std::string result = "Bank: ";
	result.reserve(result.size() + filename.size());
	for (const auto character : filename)
		result.push_back(character >= 32 && character < 127 ? static_cast<char>(character) : '?');
	return result;
}

static std::atomic_bool syncore_status_watcher_running{false};

static bool syncore_settings_is_open()
{
	if (!global_window_handler)
		return false;
	std::lock_guard locker(global_window_handler->lock);
	for (const auto& active_window : global_window_handler->active_windows)
		if (active_window->first == "SYNCORE_SETTINGS")
			return true;
	return false;
}

static std::string syncore_runtime_status_text()
{
	if (!player)
		return "Status: unavailable";

	const auto status = player->get_syncore_runtime_status();
	if (!status.error.empty())
		return "Status: " + status.message + "\n" + status.error;

	if (status.preparing && status.preparation_total)
	{
		const double percent = 100.0 * status.preparation_completed /
			status.preparation_total;
		return std::format(
			"Status: {}\nPhase preparation: {} / {} ({:.1f}%) | {:.1f} / {:.1f} MiB",
			status.message, status.preparation_completed, status.preparation_total,
			percent, status.cache_bytes / 1048576.0,
			status.total_cache_bytes / 1048576.0);
	}

	return "Status: " + status.message;
}

static void refresh_syncore_runtime_status()
{
	if (!simple_player::syncore_available() || !global_window_handler)
		return;
	auto window = (*global_window_handler)["SYNCORE_SETTINGS"];
	if (window && (*window)["STATUS"])
		((text_box*)(*window)["STATUS"])->safe_string_replace(
			syncore_runtime_status_text());
}

static void start_syncore_status_watcher();

static void syncore_status_watch_func(std::stop_token stop_token)
{
	struct running_guard
	{
		bool active = true;
		void clear()
		{
			if (active)
				syncore_status_watcher_running.store(false, std::memory_order_release);
			active = false;
		}
		~running_guard()
		{
			clear();
		}
	} guard;

	unsigned inactive_ticks = 0;
	while (inactive_ticks < 5 && !gui_stop_requested(stop_token))
	{
		if (syncore_settings_is_open())
		{
			inactive_ticks = 0;
			refresh_syncore_runtime_status();
		}
		else
			++inactive_ticks;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	// Cover a close/reopen that lands between the last activity check and the
	// running-flag reset.
	guard.clear();
	if (!gui_stop_requested(stop_token) && syncore_settings_is_open())
		start_syncore_status_watcher();
}

static void start_syncore_status_watcher()
{
	if (application_shutting_down.load(std::memory_order_acquire))
		return;
	if (syncore_status_watcher_running.exchange(true, std::memory_order_acq_rel))
		return;
	const auto result = worker_singleton<struct syncore_status_watcher>::instance().push(
		syncore_status_watch_func);
	if (result != dixelu::background_worker_submit_result::accepted)
		syncore_status_watcher_running.store(false, std::memory_order_release);
}

static void refresh_syncore_setup_controls()
{
	if (!simple_player::syncore_available())
		return;
	auto window = (*global_window_handler)["SYNCORE_SETTINGS"];
	if (!window)
		return;

	((text_box*)(*window)["BANK"])->safe_string_replace(syncore_bank_display_name());
	((input_field*)(*window)["SAMPLE_RATE"])->safe_string_replace(
		std::to_string(syncore_preferences_draft.sample_rate));
	((input_field*)(*window)["BUFFER_FRAMES"])->safe_string_replace(
		std::to_string(syncore_preferences_draft.buffer_frames));
	((input_field*)(*window)["MAX_COHORTS"])->safe_string_replace(
		std::to_string(syncore_preferences_draft.maximum_cohorts));
	((input_field*)(*window)["RENDER_THREADS"])->safe_string_replace(
		std::to_string(syncore_preferences_draft.render_threads));
	((input_field*)(*window)["GAIN_DB"])->safe_string_replace(
		std::to_string(syncore_preferences_draft.output_gain_db));
	((button*)(*window)["PHASE_MODE"])->safe_string_replace(
		std::string("Phase: ") + syncore_phase_mode_name(syncore_preferences_draft.phase_mode));
	((checkbox*)(*window)["LIMITER"])->state = syncore_preferences_draft.limiter_enabled;
	refresh_syncore_runtime_status();
}

static void persist_syncore_preferences()
{
	settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
	settings::regestry_access.SetStringValue(L"MIDI_DEVICE_NAME", saved_midi_device_name);
	settings::regestry_access.SetStringValue(L"SYNCORE_BANK_PATH", saved_syncore_bank_path);
	settings::regestry_access.SetDwordValue(L"SYNCORE_SAMPLE_RATE", saved_syncore_preferences.sample_rate);
	settings::regestry_access.SetDwordValue(L"SYNCORE_BUFFER_FRAMES", saved_syncore_preferences.buffer_frames);
	settings::regestry_access.SetDwordValue(L"SYNCORE_MAX_COHORTS", saved_syncore_preferences.maximum_cohorts);
	settings::regestry_access.SetDwordValue(L"SYNCORE_RENDER_THREADS", saved_syncore_preferences.render_threads);
	settings::regestry_access.SetDwordValue(L"SYNCORE_PHASE_MODE",
		static_cast<std::uint32_t>(saved_syncore_preferences.phase_mode));
	settings::regestry_access.SetStringValue(L"SYNCORE_GAIN_DB",
		std::to_wstring(saved_syncore_preferences.output_gain_db));
	settings::regestry_access.SetDwordValue(L"SYNCORE_LIMITER", saved_syncore_preferences.limiter_enabled);
	settings::regestry_access.Close();
}

void on_syncore_setup_open()
{
	if (!player || !player->syncore_available())
	{
		throw_alert_warning("This SAFC build does not include embedded SYNCore");
		return;
	}
	syncore_preferences_draft = player->get_syncore_preferences();
	refresh_syncore_setup_controls();
	global_window_handler->enable_window("SYNCORE_SETTINGS");
	start_syncore_status_watcher();
}

void on_syncore_phase_cycle()
{
	auto mode = static_cast<std::uint32_t>(syncore_preferences_draft.phase_mode);
	mode = (mode + 1) % (static_cast<std::uint32_t>(syncore_phase_mode::independent_bins) + 1);
	syncore_preferences_draft.phase_mode = static_cast<syncore_phase_mode>(mode);
	((button*)(*(*global_window_handler)["SYNCORE_SETTINGS"])["PHASE_MODE"])->safe_string_replace(
		std::string("Phase: ") + syncore_phase_mode_name(syncore_preferences_draft.phase_mode));
}

void on_syncore_preferences_apply()
{
	auto window = (*global_window_handler)["SYNCORE_SETTINGS"];
	syncore_preferences preferences = syncore_preferences_draft;
	try
	{
		preferences.sample_rate = std::stoul(
			((input_field*)(*window)["SAMPLE_RATE"])->get_current_input("48000"));
		preferences.buffer_frames = std::stoul(
			((input_field*)(*window)["BUFFER_FRAMES"])->get_current_input("4096"));
		preferences.maximum_cohorts = std::stoul(
			((input_field*)(*window)["MAX_COHORTS"])->get_current_input("4096"));
		preferences.render_threads = std::stoul(
			((input_field*)(*window)["RENDER_THREADS"])->get_current_input("0"));
		preferences.output_gain_db = std::stod(
			((input_field*)(*window)["GAIN_DB"])->get_current_input("-12"));
		preferences.limiter_enabled = ((checkbox*)(*window)["LIMITER"])->state;
	}
	catch (...)
	{
		throw_alert_warning("One or more SYNCore settings are not valid numbers");
		return;
	}

	if (preferences.sample_rate < 8000 || preferences.sample_rate > 192000 ||
		preferences.buffer_frames < 256 || preferences.buffer_frames > 1048576 ||
		preferences.maximum_cohorts < 1 || preferences.maximum_cohorts > 1048576 ||
		preferences.render_threads > 64 ||
		preferences.output_gain_db < -60.0 || preferences.output_gain_db > 12.0)
	{
		throw_alert_warning(
			"SYNCore ranges: rate 8000-192000, buffer 256-1048576, cohorts 1-1048576, "
			"threads 0-64, gain -60 to +12 dB");
		return;
	}

	worker_singleton<struct midi_out_selct>::instance().push([preferences](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		if (!player->set_syncore_preferences(preferences))
		{
			throw_alert_warning("Stop playback before changing SYNCore preferences");
			return;
		}
		saved_syncore_preferences = preferences;
		syncore_preferences_draft = preferences;
		try
		{
			persist_syncore_preferences();
		}
		catch (...)
		{
			std::cout << "Exception thrown while saving SYNCore preferences\n";
		}
	});
}

void on_syncore_bank_select()
{
	if (!player || !player->syncore_available())
	{
		throw_alert_warning("This SAFC build does not include embedded SYNCore");
		return;
	}

	auto bank_path = syncore_bank_open_file_dialog();
	if (bank_path.empty())
		return;

	worker_singleton<struct midi_out_selct>::instance().push(
		[bank_path = std::move(bank_path)](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		if (!player->set_syncore_bank_path(bank_path))
		{
			throw_alert_warning("Stop playback before changing the SYNCore sound bank");
			return;
		}

		const auto device_id = player->get_syncore_device_index();
		if (device_id == ~size_t{0} || !player->set_device(device_id))
		{
			throw_alert_warning("Unable to select embedded SYNCore");
			return;
		}

		const auto device_names = player->get_device_names();
		if (device_id < device_names.size())
			saved_midi_device_name.assign(device_names[device_id].begin(), device_names[device_id].end());
		saved_syncore_bank_path = bank_path;

		try
		{
			persist_syncore_preferences();
		}
		catch (...)
		{
			std::cout << "Exception thrown while saving SYNCore settings to registry\n";
		}

		refresh_syncore_setup_controls();
		update_device_list();
	});
}

void on_syncore_use_builtin_bank()
{
	if (!player || !player->syncore_available())
		return;

	worker_singleton<struct midi_out_selct>::instance().push([](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		if (!player->set_syncore_bank_path({}))
		{
			throw_alert_warning("Stop playback before changing the SYNCore sound bank");
			return;
		}
		const auto device_id = player->get_syncore_device_index();
		if (device_id == ~size_t{0} || !player->set_device(device_id))
		{
			throw_alert_warning("Unable to select embedded SYNCore");
			return;
		}
		saved_syncore_bank_path.clear();
		saved_midi_device_name = L"SYNCore (embedded)";
		try { persist_syncore_preferences(); }
		catch (...) { std::cout << "Exception thrown while saving SYNCore settings\n"; }
		refresh_syncore_setup_controls();
		update_device_list();
	});
}

