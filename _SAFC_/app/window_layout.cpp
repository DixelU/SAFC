#include "app_state.h"
#include "window_layout.h"

bool simplayer_maximised = false;
bool midieditor_maximised = false;

namespace
{
struct simplayer_saved_state
{
	float window_x, window_y, window_width, window_height;
	float text_x, text_y;
	float pause_x, pause_y;
	float stop_x, stop_y;
	float render_x, render_y;
	float buf_switch_x, buf_switch_y;
	float overlap_switch_x, overlap_switch_y;
	float max_x, max_y;
	float vls_x, vls_y;
	float seek_x, seek_y, seek_track_length;
	float devlist_cx, devlist_y;
	float view_x, view_y, view_width, view_height;
	std::string previous_main_window_id;
} saved_simplayer_state;

struct midieditor_saved_state
{
	float window_x, window_y, window_width, window_height;
	float text_x, text_y;
	float load_file_x, load_file_y;
	float save_file_x, save_file_y;
	float chan_x[16], chan_y[16];
	float zoom_in_x, zoom_in_y;
	float zoom_out_x, zoom_out_y;
	float play_x, play_y;
	float play_from_x, play_from_y;
	float track_list_x, track_list_y;
	float track_next_x, track_next_y;
	float track_prev_x, track_prev_y;
	float rename_track_x, rename_track_y;
	float snap_x, snap_y;
	float undo_x, undo_y;
	float redo_x, redo_y;
	float back_x, back_y;
	float max_x, max_y;
	float lane_x, lane_y;
	float lane_mode_x[5], lane_mode_y[5];
	float tool_x[4], tool_y[4];
	float view_x, view_y, view_width, view_height;
	std::string previous_main_window_id;
} saved_midieditor_state;

}

void on_simplayer_close()
{
	if (simplayer_maximised)
		global_window_handler->main_window_id = saved_simplayer_state.previous_main_window_id;

	if (player)
		player->stop();
}

void apply_midieditor_maximised_layout()
{
	float half_w = internal_range * (wind_x / window_base_width);
	float half_h = internal_range * (wind_y / window_base_height);
	float full_width = 2.0f * half_w;
	float full_height = 2.0f * half_h + moveable_window::window_header_size;

	auto window = (*global_window_handler)["MIDI_EDITOR"];
	auto editor_view = (midi_editor_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto load_file_btn = (button*)(*window)["LOAD_FILE"];
	auto save_file_btn = (button*)(*window)["SAVE_FILE"];
	auto play_btn = (button*)(*window)["PLAY"];
	auto play_from_btn = (button*)(*window)["PLAY_FROM"];
	auto track_list = (selectable_properted_list*)(*window)["TRACK_LIST"];
	auto track_next_btn = (button*)(*window)["TRACK_NEXT"];
	auto track_prev_btn = (button*)(*window)["TRACK_PREV"];
	auto rename_track_btn = (button*)(*window)["RENAME_TRACK"];
	auto snap_btn = (button*)(*window)["SNAP"];
	auto undo_btn = (button*)(*window)["UNDO"];
	auto redo_btn = (button*)(*window)["REDO"];
	auto back_btn = (button*)(*window)["BACK_TO_MAIN"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto lane_btn = (button*)(*window)["VEL_LANE"];
	button* lane_mode_btns[] = {
		(button*)(*window)["LANE_VEL"],
		(button*)(*window)["LANE_PITCH"],
		(button*)(*window)["LANE_PAN"],
		(button*)(*window)["LANE_CCVOL"],
		(button*)(*window)["LANE_TEMPO"]
	};
	button* tool_btns[] = {
		(button*)(*window)["TOOL_CHOP"], (button*)(*window)["TOOL_FLIP"],
		(button*)(*window)["TOOL_CLAW"], (button*)(*window)["TOOL_LFO"]
	};

	// move window so top-left aligns with viewport top-left
	float dx = (-half_w) - window->x_window_pos;
	float dy = (half_h)-window->y_window_pos + moveable_window::window_header_size;
	window->safe_move(dx, dy);

	// Resize window frame
	window->not_safe_resize(full_height, full_width);
	auto fui = (moveable_fui_window*)window;
	fui->safe_window_rename(window->window_name->current_text, false);

	// Button column on the right
	float button_x = half_w - 45;

	// File operations
	float row_y = half_h - 10;
	load_file_btn->safe_change_position(button_x, row_y);
	save_file_btn->safe_change_position(button_x, row_y -= 15);
	tool_btns[0]->safe_change_position(button_x - 20, row_y -= 15);
	tool_btns[1]->safe_change_position(button_x + 20, row_y);
	tool_btns[2]->safe_change_position(button_x - 20, row_y -= 13);
	tool_btns[3]->safe_change_position(button_x + 20, row_y);

	// Playback, maximise, velocity lane
	play_btn->safe_change_position(button_x, row_y -= 18);
	play_from_btn->safe_change_position(button_x, row_y -= 15);
	max_btn->safe_change_position(button_x, row_y -= 15);
	lane_btn->safe_change_position(button_x, row_y -= 15);

	float lane_mode_x = -half_w + 18.f;
	for (int i = 0; i < 5; ++i)
		lane_mode_btns[i]->safe_change_position(lane_mode_x + i * 38.f, half_h - 39.f);

	// Channel selector row along the top, over the viewer
	for (int channel = 0; channel < 16; ++channel)
	{
		auto chan_btn = (button*)(*window)["CH" + std::to_string(channel)];
		if (chan_btn)
			chan_btn->safe_change_position(-half_w + 10.f + 15.f * channel, half_h - 25.f);
	}

	track_list->safe_change_position(button_x, row_y -= 20);
	rename_track_btn->safe_change_position(button_x, row_y - 72.f);

	// Track / snap / edit operations (bottom cluster)
	float bottom_y = -half_h + 110;
	track_next_btn->safe_change_position(button_x, bottom_y);
	track_prev_btn->safe_change_position(button_x, bottom_y -= 15);
	snap_btn->safe_change_position(button_x, bottom_y -= 20);
	undo_btn->safe_change_position(button_x, bottom_y -= 20);
	redo_btn->safe_change_position(button_x, bottom_y -= 15);

	// Back button
	back_btn->safe_change_position(button_x, -half_h + 15);

	// Resize editor viewer to fill the window area (minus button space and
	// the status text + channel selector row at the top)
	float view_margin = 95.f; // Space for the button column on the right
	float view_width = full_width - view_margin;
	float view_height = full_height - 82;
	float view_center_x = -view_margin / 2.f;
	float view_center_y = -18.5f;

	editor_view->data.width = view_width;
	editor_view->data.height = view_height;
	editor_view->xpos = view_center_x;
	editor_view->ypos = view_center_y;

	// Status text above the viewer
	text->safe_change_position(view_center_x, half_h - 10);
}

void switch_midieditor_maximise()
{
	auto window = (*global_window_handler)["MIDI_EDITOR"];
	auto editor_view = (midi_editor_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto load_file_btn = (button*)(*window)["LOAD_FILE"];
	auto save_file_btn = (button*)(*window)["SAVE_FILE"];
	auto play_btn = (button*)(*window)["PLAY"];
	auto play_from_btn = (button*)(*window)["PLAY_FROM"];
	auto track_list = (selectable_properted_list*)(*window)["TRACK_LIST"];
	auto track_next_btn = (button*)(*window)["TRACK_NEXT"];
	auto track_prev_btn = (button*)(*window)["TRACK_PREV"];
	auto rename_track_btn = (button*)(*window)["RENAME_TRACK"];
	auto snap_btn = (button*)(*window)["SNAP"];
	auto undo_btn = (button*)(*window)["UNDO"];
	auto redo_btn = (button*)(*window)["REDO"];
	auto back_btn = (button*)(*window)["BACK_TO_MAIN"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto lane_btn = (button*)(*window)["VEL_LANE"];
	button* lane_mode_btns[] = {
		(button*)(*window)["LANE_VEL"],
		(button*)(*window)["LANE_PITCH"],
		(button*)(*window)["LANE_PAN"],
		(button*)(*window)["LANE_CCVOL"],
		(button*)(*window)["LANE_TEMPO"]
	};
	button* tool_btns[] = {
		(button*)(*window)["TOOL_CHOP"], (button*)(*window)["TOOL_FLIP"],
		(button*)(*window)["TOOL_CLAW"], (button*)(*window)["TOOL_LFO"]
	};

	if (!midieditor_maximised)
	{
		// Save current state
		auto& state = saved_midieditor_state;
		state.window_x = window->x_window_pos;
		state.window_y = window->y_window_pos;
		state.window_width = window->width;
		state.window_height = window->height;
		state.text_x = text->x_pos;
		state.text_y = text->y_pos;
		state.load_file_x = load_file_btn->x_pos;
		state.load_file_y = load_file_btn->y_pos;
		state.save_file_x = save_file_btn->x_pos;
		state.save_file_y = save_file_btn->y_pos;
		for (int channel = 0; channel < 16; ++channel)
		{
			auto chan_btn = (button*)(*window)["CH" + std::to_string(channel)];
			state.chan_x[channel] = chan_btn->x_pos;
			state.chan_y[channel] = chan_btn->y_pos;
		}
		state.play_x = play_btn->x_pos;
		state.play_y = play_btn->y_pos;
		state.play_from_x = play_from_btn->x_pos;
		state.play_from_y = play_from_btn->y_pos;
		state.track_list_x = track_list->header_cx_pos;
		state.track_list_y = track_list->header_y_pos;
		state.track_next_x = track_next_btn->x_pos;
		state.track_next_y = track_next_btn->y_pos;
		state.track_prev_x = track_prev_btn->x_pos;
		state.track_prev_y = track_prev_btn->y_pos;
		state.rename_track_x = rename_track_btn->x_pos;
		state.rename_track_y = rename_track_btn->y_pos;
		state.snap_x = snap_btn->x_pos;
		state.snap_y = snap_btn->y_pos;
		state.undo_x = undo_btn->x_pos;
		state.undo_y = undo_btn->y_pos;
		state.redo_x = redo_btn->x_pos;
		state.redo_y = redo_btn->y_pos;
		state.back_x = back_btn->x_pos;
		state.back_y = back_btn->y_pos;
		state.max_x = max_btn->x_pos;
		state.max_y = max_btn->y_pos;
		state.lane_x = lane_btn->x_pos;
		state.lane_y = lane_btn->y_pos;
		for (int i = 0; i < 5; ++i)
		{
			state.lane_mode_x[i] = lane_mode_btns[i]->x_pos;
			state.lane_mode_y[i] = lane_mode_btns[i]->y_pos;
		}
		for (int i = 0; i < 4; ++i)
		{
			state.tool_x[i] = tool_btns[i]->x_pos;
			state.tool_y[i] = tool_btns[i]->y_pos;
		}
		state.view_x = editor_view->xpos;
		state.view_y = editor_view->ypos;
		state.view_width = editor_view->data.width;
		state.view_height = editor_view->data.height;
		state.previous_main_window_id = global_window_handler->main_window_id;

		apply_midieditor_maximised_layout();

		// Make the editor the sole window
		global_window_handler->main_window_id = "MIDI_EDITOR";
		global_window_handler->disable_all_windows();

		midieditor_maximised = true;
		max_btn->safe_string_replace("Restore");
	}
	else
	{
		auto& state = saved_midieditor_state;

		// move window back to original position
		float dx = state.window_x - window->x_window_pos;
		float dy = state.window_y - window->y_window_pos;
		window->safe_move(dx, dy);
		window->not_safe_resize(state.window_height, state.window_width);
		auto fui = (moveable_fui_window*)window;
		fui->safe_window_rename(window->window_name->current_text, false);

		// Restore each child to saved position
		text->safe_change_position(state.text_x, state.text_y);
		load_file_btn->safe_change_position(state.load_file_x, state.load_file_y);
		save_file_btn->safe_change_position(state.save_file_x, state.save_file_y);
		for (int channel = 0; channel < 16; ++channel)
		{
			auto chan_btn = (button*)(*window)["CH" + std::to_string(channel)];
			chan_btn->safe_change_position(state.chan_x[channel], state.chan_y[channel]);
		}
		play_btn->safe_change_position(state.play_x, state.play_y);
		play_from_btn->safe_change_position(state.play_from_x, state.play_from_y);
		track_list->safe_change_position(state.track_list_x, state.track_list_y);
		track_next_btn->safe_change_position(state.track_next_x, state.track_next_y);
		track_prev_btn->safe_change_position(state.track_prev_x, state.track_prev_y);
		rename_track_btn->safe_change_position(state.rename_track_x, state.rename_track_y);
		snap_btn->safe_change_position(state.snap_x, state.snap_y);
		undo_btn->safe_change_position(state.undo_x, state.undo_y);
		redo_btn->safe_change_position(state.redo_x, state.redo_y);
		back_btn->safe_change_position(state.back_x, state.back_y);
		max_btn->safe_change_position(state.max_x, state.max_y);
		lane_btn->safe_change_position(state.lane_x, state.lane_y);
		for (int i = 0; i < 5; ++i)
		{
			lane_mode_btns[i]->safe_change_position(state.lane_mode_x[i], state.lane_mode_y[i]);
		}
		for (int i = 0; i < 4; ++i)
		{
			tool_btns[i]->safe_change_position(state.tool_x[i], state.tool_y[i]);
		}

		editor_view->xpos = state.view_x;
		editor_view->ypos = state.view_y;
		editor_view->data.width = state.view_width;
		editor_view->data.height = state.view_height;

		// Restore window management
		global_window_handler->main_window_id = state.previous_main_window_id;
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("MIDI_EDITOR");

		midieditor_maximised = false;
		max_btn->safe_string_replace("Maximise");
	}
}

void apply_simplayer_maximised_layout()
{
	float half_w = internal_range * (wind_x / window_base_width);
	float half_h = internal_range * (wind_y / window_base_height);
	float full_width = 2.0f * half_w;
	float full_height = 2.0f * half_h + moveable_window::window_header_size;

	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto pause_btn = (button*)(*window)["PAUSE"];
	auto stop_btn = (button*)(*window)["STOP"];
	auto render_btn = (button*)(*window)["RENDER_VIDEO"];
	auto vls = (slider*)(*window)["VIEW_LEN_SLIDER"];
	auto buf_sw = (button*)(*window)["BUFFERING_SWITCH"];
	auto overlap_sw = (button*)(*window)["OVERLAP_SWITCH"];
	auto seek_to = (slider*)(*window)["SEEK_TO"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto dev_list = (selectable_properted_list*)(*window)["DEVICE_LIST"];

	// move window so top-left aligns with viewport top-left
	float dx = (-half_w) - window->x_window_pos;
	float dy = (half_h)-window->y_window_pos + moveable_window::window_header_size;
	window->safe_move(dx, dy);

	// Resize window frame
	window->not_safe_resize(full_height, full_width);
	auto fui = (moveable_fui_window*)window;
	fui->safe_window_rename(window->window_name->current_text, false);

	// Position controls near the top
	float row1_y = half_h - 10;
	float row2_y = row1_y - 25;
	float row3_y = row2_y - 5;

	pause_btn->safe_change_position(-half_w + 10, row1_y);
	stop_btn->safe_change_position(-half_w + 25, row1_y);
	vls->safe_change_position(-half_w + 75, row1_y);
	text->safe_change_position(0, row1_y - 20);
	buf_sw->safe_change_position(half_w - 50, row1_y);

	render_btn->safe_change_position(half_w - 70, row2_y + 10);
	max_btn->safe_change_position(half_w - 30, row2_y + 10);
	dev_list->safe_change_position(-half_w + 60, row2_y + 15);

	overlap_sw->safe_change_position(half_w - 50, row3_y);

	// Stretch seek slider
	float seek_y = row2_y - 15;
	seek_to->safe_change_position(0, seek_y - 10);
	seek_to->track_length = full_width - 30;

	// Compute player_viewer geometry
	float width_factor_change = full_width / player_view->data->width;
	//float keyboard_height = 40.0f;
	float notes_top = seek_y - 15;
	float notes_bottom = -half_h + player_view->data->last_keyboard_height * width_factor_change;
	float notes_height = notes_top - notes_bottom;
	float view_center_y = (notes_top + notes_bottom) * 0.5f;

	player_view->rescale_and_reposition(0, view_center_y, full_width, notes_height);
}

void switch_maximise()
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto pause_btn = (button*)(*window)["PAUSE"];
	auto stop_btn = (button*)(*window)["STOP"];
	auto render_btn = (button*)(*window)["RENDER_VIDEO"];
	auto vls = (slider*)(*window)["VIEW_LEN_SLIDER"];
	auto buf_sw = (button*)(*window)["BUFFERING_SWITCH"];
	auto overlap_sw = (button*)(*window)["OVERLAP_SWITCH"];
	auto seek_to = (slider*)(*window)["SEEK_TO"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto dev_list = (selectable_properted_list*)(*window)["DEVICE_LIST"];

	if (!simplayer_maximised)
	{
		// Save current state
		auto& state = saved_simplayer_state;
		state.window_x = window->x_window_pos;
		state.window_y = window->y_window_pos;
		state.window_width = window->width;
		state.window_height = window->height;
		state.text_x = text->x_pos;
		state.text_y = text->y_pos;
		state.pause_x = pause_btn->x_pos;
		state.pause_y = pause_btn->y_pos;
		state.stop_x = stop_btn->x_pos;
		state.stop_y = stop_btn->y_pos;
		state.render_x = render_btn->x_pos;
		state.render_y = render_btn->y_pos;
		state.vls_x = vls->x_pos;
		state.vls_y = vls->y_pos;
		state.buf_switch_x = buf_sw->x_pos;
		state.buf_switch_y = buf_sw->y_pos;
		state.overlap_switch_x = overlap_sw->x_pos;
		state.overlap_switch_y = overlap_sw->y_pos;
		state.seek_x = seek_to->x_pos;
		state.seek_y = seek_to->y_pos;
		state.seek_track_length = seek_to->track_length;
		state.max_x = max_btn->x_pos;
		state.max_y = max_btn->y_pos;
		state.devlist_cx = dev_list->header_cx_pos;
		state.devlist_y = dev_list->header_y_pos;
		state.view_x = player_view->xpos;
		state.view_y = player_view->ypos;
		state.view_width = player_view->data->width;
		state.view_height = player_view->data->height;
		state.previous_main_window_id = global_window_handler->main_window_id;

		// Apply maximized layout
		apply_simplayer_maximised_layout();

		// Make SIMPLAYER the sole window
		global_window_handler->main_window_id = "SIMPLAYER";
		global_window_handler->disable_all_windows();

		simplayer_maximised = true;
		max_btn->safe_string_replace("Restore");
	}
	else
	{
		auto& state = saved_simplayer_state;

		// move window back to original position
		float dx = state.window_x - window->x_window_pos;
		float dy = state.window_y - window->y_window_pos;
		window->safe_move(dx, dy);
		window->not_safe_resize(state.window_height, state.window_width);
		auto fui = (moveable_fui_window*)window;
		fui->safe_window_rename(window->window_name->current_text, false);

		// Restore each child to saved position
		text->safe_change_position(state.text_x, state.text_y);
		pause_btn->safe_change_position(state.pause_x, state.pause_y);
		stop_btn->safe_change_position(state.stop_x, state.stop_y);
		render_btn->safe_change_position(state.render_x, state.render_y);
		vls->safe_change_position(state.vls_x, state.vls_y);
		buf_sw->safe_change_position(state.buf_switch_x, state.buf_switch_y);
		overlap_sw->safe_change_position(state.overlap_switch_x, state.overlap_switch_y);
		seek_to->safe_change_position(state.seek_x, state.seek_y);
		seek_to->track_length = state.seek_track_length;
		max_btn->safe_change_position(state.max_x, state.max_y);
		dev_list->safe_change_position(state.devlist_cx, state.devlist_y);

		// Restore player_viewer
		player_view->rescale_and_reposition(state.view_x, state.view_y, state.view_width, state.view_height);

		// Restore window management
		global_window_handler->main_window_id = state.previous_main_window_id;
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("SIMPLAYER");

		simplayer_maximised = false;
		max_btn->safe_string_replace("Maximise");
	}
}

