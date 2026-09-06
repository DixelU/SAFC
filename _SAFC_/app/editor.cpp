#include "app_state.h"
#include "editor.h"
#include "dialogs.h"
#include "playback_source.h"
#include "player_controls.h"

#include <algorithm>
#include <format>

void update_channel_indicator()
{
	auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	if (!editor_view)
		return;

	// Brackets mark the channel new notes will be drawn on
	const int current = editor_view->effective_draw_channel();
	const int active_track = editor && editor->is_file_loaded()
		? editor->get_active_track()
		: 0;
	for (int channel = 0; channel < 16; ++channel)
	{
		auto chan_btn = _WH_t<button>("MIDI_EDITOR", ("CH" + std::to_string(channel)).c_str());
		if (chan_btn)
		{
			chan_btn->safe_string_replace(std::to_string(channel + 1));
			chan_btn->rgba_background = editor_channel_button_color(active_track, channel);
			chan_btn->hovered_rgba_background = editor_channel_button_color(active_track, channel, true);
			chan_btn->rgba_border = channel == current ? 0xFFFFFFFF : 0xFFFFFF7F;
			chan_btn->hovered_rgba_border = channel == current ? 0xFFFFFFFF : 0xFFFFFFFF;
			chan_btn->border_width = channel == current ? 2 : 1;
		}
	}
}

std::uint32_t editor_channel_button_color(int track, int channel, bool hover)
{
	std::uint8_t r, g, b;
	midi_editor_viewer::hsv_to_rgb(
		midi_editor_viewer::track_hue(std::uint8_t(track & 0xFF), std::uint8_t(channel & 0x0F)),
		hover ? 0.70f : 0.85f,
		hover ? 0.95f : 0.68f,
		r, g, b);
	return (std::uint32_t(r) << 24) | (std::uint32_t(g) << 16) |
		(std::uint32_t(b) << 8) | 0xCF;
}

void update_editor_track_list()
{
	auto track_list = _WH_t<selectable_properted_list>("MIDI_EDITOR", "TRACK_LIST");
	if (!track_list || !editor)
		return;

	track_list->safe_clear();

	if (!editor->is_file_loaded())
	{
		track_list->safe_push_back_new_string("No MIDI loaded");
		return;
	}

	const auto active_track = editor->get_active_track();
	for (const auto& [track, info] : editor->get_tracks())
	{
		const auto label = editor->get_track_label(track);
		track_list->safe_push_back_new_string(label);
		if (track == active_track)
			track_list->selected_id.push_back(track_list->selectors_text.size() - 1);
	}
}

void update_editor_status_text()
{
	auto textbox = _WH_t<text_box>("MIDI_EDITOR", "TEXT");
	if (!textbox || !editor)
		return;

	// The effective draw channel follows the active track unless overridden
	update_channel_indicator();
	update_editor_track_list();

	if (!editor->is_file_loaded())
	{
		textbox->safe_string_replace("Load a MIDI file to begin editing");
		return;
	}

	auto filename = editor->get_filename();
	textbox->safe_string_replace(
		std::format("{} | {} notes",
			editor->get_track_label(editor->get_active_track()),
			editor->get_note_count()));
}

void on_editor_load_file()
{
	if (gui_stop_requested())
		return;
	auto filenames = multiple_open_file_dialog(L"Select MIDI file to edit");
	if (filenames.empty() || filenames[0].empty() || gui_stop_requested())
		return;
	auto filename = std::move(filenames[0]);

	worker_singleton<struct editor_load>::instance().push(
		[filename = std::move(filename)](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;

		// Large files parse for a while; keep the status line moving
		editor_flash_status("Loading...");
		editor->on_load_progress = [](std::uint64_t done, std::uint64_t total)
		{
			if (total)
				editor_flash_status("Loading... " +
					std::to_string(done * 100 / total) + "%");
		};

		if (editor->load_file(filename))
		{
			// Update editor viewer; the editor fits the view to the file on load
			auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
			if (editor_view)
				editor_view->set_editor(editor.get());

			update_editor_status_text();
		}
		else
		{
			throw_alert_error("Failed to load MIDI file");
		}
	});
}

void on_editor_save_file()
{
	if (!editor || !editor->is_file_loaded())
	{
		throw_alert_warning("No MIDI file loaded for editing");
		return;
	}

	auto save_path = save_open_file_dialog(L"Save edited MIDI as...");
	if (save_path.empty() || gui_stop_requested())
		return;

	worker_singleton<struct editor_save>::instance().push(
		[save_path = std::move(save_path)](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;

		if (editor->save_file(save_path))
		{
			auto textbox = _WH_t<text_box>("MIDI_EDITOR", "TEXT");
			if (textbox)
				textbox->safe_string_replace("File saved successfully!");
		}
		else
		{
			throw_alert_error("Failed to save MIDI file");
		}
	});
}

void on_editor_undo()
{
	if (!editor || !editor->is_file_loaded())
		return;

	editor->undo();
}

void on_editor_redo()
{
	if (!editor || !editor->is_file_loaded())
		return;

	editor->redo();
}

// The global player is shared with the SIMPLAYER window; the editor's playback
// cursor is only meaningful while the player is streaming the editor's notes
std::atomic<bool> editor_playback_active = false;
static std::atomic<bool> editor_playback_requested = false;

void update_editor_playback_status()
{
	if (!global_window_handler || !player)
		return;

	static std::string last_timer_text;
	static bool timer_was_active = false;

	const bool timer_active =
		editor_playback_active.load(std::memory_order_acquire) && player->is_playing();

	auto pad2 = [](long long value)
	{
		auto text = std::to_string(value);
		return text.size() < 2 ? "0" + text : text;
	};
	auto mmss_tenths = [&](double seconds)
	{
		seconds = std::max(0.0, seconds);
		const long long tenths = static_cast<long long>(seconds * 10.0 + 0.5);
		return std::to_string(tenths / 600) + ":" +
			pad2((tenths / 10) % 60) + "." + std::to_string(tenths % 10);
	};
	auto mmss = [&](double seconds)
	{
		seconds = std::max(0.0, seconds);
		const long long whole_seconds = static_cast<long long>(seconds + 0.5);
		return std::to_string(whole_seconds / 60) + ":" + pad2(whole_seconds % 60);
	};

	if (timer_active)
	{
		const double current = double(player->get_position_us()) / 1000000.0;
		const double total = double(player->get_info().total_duration_us) / 1000000.0;
		std::string text = mmss_tenths(current) + " / " + mmss(total);
		if (text != last_timer_text)
		{
			if (auto* status = _WH_t<text_box>("MIDI_EDITOR", "TEXT"))
				status->safe_string_replace(text);
			last_timer_text = std::move(text);
		}
		timer_was_active = true;
	}
	else if (timer_was_active)
	{
		const double total = double(player->get_info().total_duration_us) / 1000000.0;
		if (auto* status = _WH_t<text_box>("MIDI_EDITOR", "TEXT"))
			status->safe_string_replace("Stopped  (" + mmss(total) + " total)");
		last_timer_text.clear();
		timer_was_active = false;
	}
}

static void on_editor_play_from(bool from_view_start)
{
	if (!editor || !editor->is_file_loaded() || !player)
		return;

	auto play_btn = _WH_t<button>("MIDI_EDITOR", "PLAY");

	// Toggle: stop if already playing
	if (player->is_playing())
	{
		player->stop();
		if (play_btn)
			play_btn->safe_string_replace("Play");
		return;
	}
	select_regular_player_source();

	// Close the window between the UI click and the worker publishing
	// state.playing. Without this, rapid clicks queue another full playback that
	// starts unexpectedly after the first one finishes.
	bool expected = false;
	if (!editor_playback_requested.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel))
		return;

	double seek_fraction = 0.0;
	if (from_view_start)
	{
		auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
		const auto start_tick = editor_view ? editor->get_view_start_tick() : 0;
		const auto total_seconds = editor->get_total_seconds();
		if (total_seconds > 0.0)
			seek_fraction = std::clamp(editor->get_seconds_at_tick(start_tick) / total_seconds, 0.0, 1.0);
	}

	// run_from_external blocks until playback ends, keep it off the UI thread
	worker_singleton<struct editor_playback>::instance().push(
		[play_btn, seek_fraction](std::stop_token stop_token)
	{
		struct request_guard
		{
			~request_guard()
			{
				editor_playback_requested.store(false, std::memory_order_release);
			}
		} guard;
		if (gui_stop_requested(stop_token))
			return;
		std::stop_callback cancellation(stop_token, []()
		{
			if (player)
				player->stop();
		});

		// Stream the current in-memory state (unsaved edits included) straight
		// into the player — no .mid written to disk, no re-parse.
		auto source = editor->make_playback_source();
		if (!source)
		{
			throw_alert_error("Failed to prepare playback");
			return;
		}

		if (!player->ensure_output(saved_midi_device_name))
		{
			if (!gui_stop_requested(stop_token))
				report_player_output_error();
			return;
		}
		if (gui_stop_requested(stop_token))
			return;
		editor_playback_active = true;
		if (play_btn)
			play_btn->safe_string_replace("Stop");
		// Editor playback has already prepared its output, so start immediately.
		// This avoids the separate polling/unpause worker and its timeout races.
		player->run_from_external(source.get(), seek_fraction, false);
		editor_playback_active = false;
		if (play_btn && !gui_stop_requested(stop_token))
			play_btn->safe_string_replace("Play");
	});

}

void on_editor_play()
{
	on_editor_play_from(false);
}

void on_editor_play_from_view()
{
	on_editor_play_from(true);
}

void on_editor_channel_select(int channel)
{
	auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	if (!editor_view)
		return;

	editor_view->set_draw_channel(channel);
	update_channel_indicator();
}

void on_editor_toggle_lane()
{
	auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	auto lane_btn = _WH_t<button>("MIDI_EDITOR", "VEL_LANE");
	if (!editor_view || !lane_btn)
		return;

	editor_view->toggle_velocity_lane();
	lane_btn->safe_string_replace(editor_view->velocity_lane_visible ? "Hide Lane" : "Show Lane");
}

void editor_flash_status(const std::string& message)
{
	auto textbox = _WH_t<text_box>("MIDI_EDITOR", "TEXT");
	if (textbox)
		textbox->safe_string_replace(message);
}

void on_editor_snap_cycle()
{
	auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	auto snap_btn = _WH_t<button>("MIDI_EDITOR", "SNAP");
	if (!editor_view || !snap_btn)
		return;

	snap_btn->safe_string_replace(editor_view->cycle_snap());
}

static void editor_switch_track(int direction)
{
	if (!editor || !editor->is_file_loaded())
		return;

	// Cycle only through tracks that actually contain notes
	editor->set_active_track(editor->next_track_with_notes(direction));
	update_editor_status_text();
}

void on_editor_track_next() { editor_switch_track(1); }
void on_editor_track_prev() { editor_switch_track(-1); }

void on_editor_track_list_select(int index)
{
	if (!editor || !editor->is_file_loaded() || index < 0)
		return;

	int cur = 0;
	for (const auto& [track, _] : editor->get_tracks())
	{
		if (cur++ == index)
		{
			editor->set_active_track(track);
			update_editor_status_text();
			return;
		}
	}
}

void on_editor_rename_track()
{
	if (!editor || !editor->is_file_loaded())
		return;

	const auto track = editor->get_active_track();
	std::string current_name;
	auto tracks = editor->get_tracks();
	auto it = tracks.find(track);
	if (it != tracks.end())
		current_name = it->second.name;

	global_window_handler->throw_prompt(
		"New name for " + editor->get_track_label(track),
		"Rename track",
		[track]()
	{
		auto prompt = (*global_window_handler)["PROMPT"];
		auto field = (input_field*)(*prompt)["FLD"].get();
		const auto new_name = field->current_string.empty() && field->stl
			? field->stl->current_text
			: field->current_string;
		global_window_handler->disable_window("PROMPT");
		if (!new_name.empty())
		{
			editor->set_track_name(track, new_name);
			update_editor_status_text();
		}
	},
		_Align::center,
		input_field::Type::text,
		current_name,
		32);
}

void on_editor_lane_mode(midi_editor_viewer::lane_mode mode)
{
	auto editor_view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	if (!editor_view)
		return;

	editor_view->set_lane_mode(mode);
	const char* ids[] = {"LANE_VEL", "LANE_PITCH", "LANE_PAN", "LANE_CCVOL", "LANE_TEMPO"};
	for (auto id : ids)
	{
		auto btn = _WH_t<button>("MIDI_EDITOR", id);
		if (btn)
			btn->border_width = 1;
	}

	const char* active_id = "LANE_VEL";
	if (mode == midi_editor_viewer::lane_mode::pitch_bend)
		active_id = "LANE_PITCH";
	else if (mode == midi_editor_viewer::lane_mode::pan)
		active_id = "LANE_PAN";
	else if (mode == midi_editor_viewer::lane_mode::channel_volume)
		active_id = "LANE_CCVOL";
	else if (mode == midi_editor_viewer::lane_mode::tempo)
		active_id = "LANE_TEMPO";

	if (auto btn = _WH_t<button>("MIDI_EDITOR", active_id))
		btn->border_width = 2;
}

