#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/midi_editor.h"
#include "midi_editor_viewer.h"
#include "midi_editor_tools_ui.h"

namespace
{
	std::function<void(const std::string&)> tool_status_callback;
	std::function<void(const std::string&)> tool_warning_callback;

	template<typename ui_part_type>
	ui_part_type* tool_ui(const char* window, const char* element)
		requires std::is_base_of_v<handleable_ui_part, ui_part_type>
	{
		if (!global_window_handler)
			return nullptr;
		handleable_ui_part* part = ((*(*global_window_handler)[window])[element]);
		return dynamic_cast<ui_part_type*>(part);
	}

	void midi_tool_status(const std::string& message)
	{
		if (tool_status_callback)
			tool_status_callback(message);
	}

	void midi_tool_warning(const std::string& message)
	{
		if (tool_warning_callback)
			tool_warning_callback(message);
	}
}

#define _WH_t tool_ui

void close_editor_tool(const char* id)
{
	if (editor)
		editor->cancel_tool_preview();
	global_window_handler->disable_window(id);
}

void open_editor_tool(const char* id)
{
	if (!editor || !editor->is_file_loaded())
	{
		midi_tool_warning("Load a MIDI file before using score tools");
		return;
	}
	editor->cancel_tool_preview();
	global_window_handler->enable_window(id);
}

void preview_editor_chopper();
void preview_editor_flip();
void preview_editor_claw();
void preview_editor_lfo();

void on_editor_open_chopper() { open_editor_tool("MIDI_CHOPPER"); preview_editor_chopper(); }
void on_editor_open_flip() { open_editor_tool("MIDI_FLIP"); preview_editor_flip(); }
void on_editor_open_claw() { open_editor_tool("MIDI_CLAW"); preview_editor_claw(); }

midi_editor::lfo_shape editor_lfo_shape = midi_editor::lfo_shape::sine;

void update_editor_lfo_shape_button()
{
	auto btn = _WH_t<button>("MIDI_LFO", "SHAPE");
	if (!btn)
		return;
	const char* label = editor_lfo_shape == midi_editor::lfo_shape::triangle
		? "Triangle" : editor_lfo_shape == midi_editor::lfo_shape::square ? "Square" : "Sine";
	btn->safe_string_replace(std::string("Shape: ") + label);
}

void on_editor_lfo_shape_cycle()
{
	editor_lfo_shape = static_cast<midi_editor::lfo_shape>(
		(static_cast<unsigned>(editor_lfo_shape) + 1) % 3);
	update_editor_lfo_shape_button();
	preview_editor_lfo();
}

double editor_tool_value(const char* window, const char* id, double fallback)
{
	auto control = _WH_t<wheel_variable_changer>(window, id);
	if (!control)
		return fallback;
	try
	{
		control->checkup_inputs();
		return control->variable;
	}
	catch (...)
	{
		return fallback;
	}
}

bool editor_tool_checked(const char* window, const char* id)
{
	auto control = _WH_t<checkbox>(window, id);
	return control && control->state;
}

void preview_editor_chopper()
{
	if (!editor || !editor->is_file_loaded()) return;
	editor->chop_tool(
		unsigned(std::lround(editor_tool_value("MIDI_CHOPPER", "DIVISIONS", 4))),
		editor_tool_value("MIDI_CHOPPER", "TIME", 1.0),
		editor_tool_value("MIDI_CHOPPER", "GAP", 0.0),
		editor_tool_checked("MIDI_CHOPPER", "ABSOLUTE"), true);
}

void on_editor_apply_chopper()
{
	preview_editor_chopper();
	const auto count = editor->selection_count();
	editor->accept_tool_preview();
	close_editor_tool("MIDI_CHOPPER");
	midi_tool_status("Chopper: " + std::to_string(count) + " notes");
}

void preview_editor_flip()
{
	if (!editor || !editor->is_file_loaded()) return;
	editor->flip_tool(editor_tool_checked("MIDI_FLIP", "HORIZONTAL"),
		editor_tool_checked("MIDI_FLIP", "PRESERVE"),
		editor_tool_checked("MIDI_FLIP", "VERTICAL"), true);
}

void on_editor_apply_flip()
{
	preview_editor_flip();
	editor->accept_tool_preview();
	close_editor_tool("MIDI_FLIP");
	midi_tool_status("Score flipped");
}

void preview_editor_claw()
{
	if (!editor || !editor->is_file_loaded()) return;
	editor->claw_tool(
		editor_tool_value("MIDI_CLAW", "PERIOD", 1.0),
		unsigned(std::lround(editor_tool_value("MIDI_CLAW", "TRASH", 4))),
		editor_tool_value("MIDI_CLAW", "DISTORTION", 0.5),
		editor_tool_checked("MIDI_CLAW", "REMOVE_SHORT"),
		editor_tool_checked("MIDI_CLAW", "STRETCH"), true);
}

void on_editor_apply_claw()
{
	preview_editor_claw();
	const auto count = editor->selection_count();
	editor->accept_tool_preview();
	close_editor_tool("MIDI_CLAW");
	midi_tool_status("Claw: " + std::to_string(count) + " notes");
}

void on_editor_open_lfo()
{
	if (!editor || !editor->is_file_loaded())
	{
		open_editor_tool("MIDI_LFO");
		return;
	}
	auto view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	const bool pitch = view && view->current_lane_mode == midi_editor_viewer::lane_mode::pitch_bend;
	if (auto center = _WH_t<wheel_variable_changer>("MIDI_LFO", "CENTER"))
	{
		center->variable = pitch ? 8192.0 : 64.0;
		center->var_if->update_input_string(std::to_string(center->variable));
	}
	if (auto range = _WH_t<wheel_variable_changer>("MIDI_LFO", "RANGE"))
	{
		range->variable = pitch ? 8191.0 : 63.0;
		range->var_if->update_input_string(std::to_string(range->variable));
	}
	open_editor_tool("MIDI_LFO");
	preview_editor_lfo();
}

void preview_editor_lfo()
{
	auto view = _WH_t<midi_editor_viewer>("MIDI_EDITOR", "VIEW");
	if (!view || !editor || !editor->is_file_loaded())
		return;
	const double center = editor_tool_value("MIDI_LFO", "CENTER", 64.0);
	const double range = editor_tool_value("MIDI_LFO", "RANGE", 63.0);
	const double cycles = editor_tool_value("MIDI_LFO", "CYCLES", 1.0);
	const double phase = editor_tool_value("MIDI_LFO", "PHASE", 0.0) / 360.0;
	if (view->current_lane_mode == midi_editor_viewer::lane_mode::note_velocity)
		editor->lfo_velocity_tool(center, range, cycles, phase, editor_lfo_shape, true);
	else
	{
		midi_editor::tick_type begin = editor->get_view_start_tick();
		midi_editor::tick_type end = begin + editor->get_view_duration_ticks();
		std::uint8_t low = 0, high = 0;
		if (!editor->get_selection_bounds(begin, end, low, high))
			end = begin + editor->get_view_duration_ticks();
		midi_editor::control_lane lane = midi_editor::control_lane::channel_volume;
		if (view->current_lane_mode == midi_editor_viewer::lane_mode::pitch_bend)
			lane = midi_editor::control_lane::pitch_bend;
		else if (view->current_lane_mode == midi_editor_viewer::lane_mode::pan)
			lane = midi_editor::control_lane::pan;
		editor->lfo_control_tool(lane, view->effective_draw_channel(), begin, end,
			std::max<midi_editor::tick_type>(1, view->snap_ticks(editor->get_ppqn())),
			center, range, cycles, phase, editor_lfo_shape, true);
	}
}

void on_editor_apply_lfo()
{
	preview_editor_lfo();
	editor->accept_tool_preview();
	close_editor_tool("MIDI_LFO");
	midi_tool_status("LFO applied");
}

void initialize_midi_editor_tool_windows(
	std::function<void(const std::string&)> status_callback,
	std::function<void(const std::string&)> warning_callback,
	unsigned background_color, unsigned header_color, unsigned border_color)
{
	tool_status_callback = std::move(status_callback);
	tool_warning_callback = std::move(warning_callback);
	moveable_window* window = nullptr;
	const unsigned BACKGROUND_OPQ = background_color;
	const unsigned HEADER = header_color;
	const unsigned BORDER = border_color;

// Score tools use dedicated modal-style settings windows. Wheel the right
// half of a value control or type directly; the lower field is its step.
window = new moveable_fui_window("Chopper (Alt+U)", system_white,
	-125, 115 + moveable_window::window_header_size, 250, 230 + moveable_window::window_header_size,
	100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
window->on_close = []() { close_editor_tool("MIDI_CHOPPER"); };
(*window)["INFO"] = new text_box("Slice selected notes (or the active track)", legacy_white,
	0, 88, 12, 210, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["DIVISIONS"] = new wheel_variable_changer([](double) { preview_editor_chopper(); }, -55, 48, 4, 1,
	system_white, "Slices/beat", "Step", wheel_variable_changer::Type::addictable);
(*window)["TIME"] = new wheel_variable_changer([](double) { preview_editor_chopper(); }, 55, 48, 1, .25,
	system_white, "Time mult.", "Step", wheel_variable_changer::Type::addictable);
(*window)["GAP"] = new wheel_variable_changer([](double) { preview_editor_chopper(); }, -55, -18, 0, 5,
	system_white, "Gap %", "Step", wheel_variable_changer::Type::addictable);
(*window)["ABSOLUTE"] = new checkbox(35, -15, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF,
	1, false, &system_white, _Align::right, "Absolute pattern (align slices to score grid)",
	[](bool) { preview_editor_chopper(); });
(*window)["ABS_LABEL"] = new text_box("Absolute pattern", legacy_white, 75, -10, 12, 70, 10,
	0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["APPLY"] = new button("Accept", system_white, on_editor_apply_chopper, -42, -88, 70, 12, 1,
	0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Apply as one undoable edit");
(*window)["CANCEL"] = new button("Cancel", system_white, []() { close_editor_tool("MIDI_CHOPPER"); },
	42, -88, 70, 12, 1, 0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, "Close without editing");
(*global_window_handler)["MIDI_CHOPPER"] = window;

window = new moveable_fui_window("Flip score (Alt+Y)", system_white,
	-105, 80 + moveable_window::window_header_size, 210, 160 + moveable_window::window_header_size,
	100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
window->on_close = []() { close_editor_tool("MIDI_FLIP"); };
(*window)["HORIZONTAL"] = new checkbox(-75, 40, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF, 1, true,
	&system_white, _Align::right, "Flip horizontally (time)", [](bool) { preview_editor_flip(); });
(*window)["H_LABEL"] = new text_box("Horizontal", legacy_white, -25, 45, 12, 80, 10, 0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["PRESERVE"] = new checkbox(-75, 10, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF, 1, false,
	&system_white, _Align::right, "Preserve note start times while reversing their order", [](bool) { preview_editor_flip(); });
(*window)["P_LABEL"] = new text_box("Preserve starts", legacy_white, -15, 15, 12, 100, 10, 0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["VERTICAL"] = new checkbox(-75, -20, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF, 1, false,
	&system_white, _Align::right, "Flip vertically (pitch)", [](bool) { preview_editor_flip(); });
(*window)["V_LABEL"] = new text_box("Vertical", legacy_white, -25, -15, 12, 80, 10, 0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["APPLY"] = new button("Accept", system_white, on_editor_apply_flip, -38, -58, 62, 12, 1,
	0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Apply as one undoable edit");
(*window)["CANCEL"] = new button("Cancel", system_white, []() { close_editor_tool("MIDI_FLIP"); }, 38, -58, 62, 12, 1,
	0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, "Close without editing");
(*global_window_handler)["MIDI_FLIP"] = window;

window = new moveable_fui_window("Claw machine (Alt+W)", system_white,
	-140, 125 + moveable_window::window_header_size, 280, 250 + moveable_window::window_header_size,
	100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
window->on_close = []() { close_editor_tool("MIDI_CLAW"); };
(*window)["PERIOD"] = new wheel_variable_changer([](double) { preview_editor_claw(); }, -65, 70, 1, .25,
	system_white, "Period beats", "Step", wheel_variable_changer::Type::addictable);
(*window)["TRASH"] = new wheel_variable_changer([](double) { preview_editor_claw(); }, 65, 70, 4, 1,
	system_white, "Trash every", "Step", wheel_variable_changer::Type::addictable);
(*window)["DISTORTION"] = new wheel_variable_changer([](double) { preview_editor_claw(); }, -65, 5, .5, .05,
	system_white, "Distortion", "Step", wheel_variable_changer::Type::addictable);
(*window)["REMOVE_SHORT"] = new checkbox(35, 20, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF, 1, true,
	&system_white, _Align::right, "Remove notes shorter than 1/64 beat", [](bool) { preview_editor_claw(); });
(*window)["RS_LABEL"] = new text_box("Remove short", legacy_white, 85, 25, 12, 85, 10, 0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["STRETCH"] = new checkbox(35, -15, 11, 0xFFFFFFFF, 0x202020FF, 0x00AF7FFF, 1, false,
	&system_white, _Align::right, "Stretch output back to the original selection length", [](bool) { preview_editor_claw(); });
(*window)["ST_LABEL"] = new text_box("Stretch to fit", legacy_white, 85, -10, 12, 85, 10, 0, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["APPLY"] = new button("Accept", system_white, on_editor_apply_claw, -42, -98, 70, 12, 1,
	0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Apply as one undoable edit");
(*window)["CANCEL"] = new button("Cancel", system_white, []() { close_editor_tool("MIDI_CLAW"); }, 42, -98, 70, 12, 1,
	0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, "Close without editing");
(*global_window_handler)["MIDI_CLAW"] = window;

window = new moveable_fui_window("LFO (Alt+O)", system_white,
	-150, 140 + moveable_window::window_header_size, 300, 280 + moveable_window::window_header_size,
	100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
window->on_close = []() { close_editor_tool("MIDI_LFO"); };
(*window)["INFO"] = new text_box("Targets the selected bottom lane and visible/selected time", legacy_white,
	0, 112, 12, 260, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
(*window)["CENTER"] = new wheel_variable_changer([](double) { preview_editor_lfo(); }, -70, 65, 64, 1,
	system_white, "Value", "Step", wheel_variable_changer::Type::addictable);
(*window)["RANGE"] = new wheel_variable_changer([](double) { preview_editor_lfo(); }, 70, 65, 63, 1,
	system_white, "Range", "Step", wheel_variable_changer::Type::addictable);
(*window)["CYCLES"] = new wheel_variable_changer([](double) { preview_editor_lfo(); }, -70, 0, 1, .25,
	system_white, "Cycles", "Step", wheel_variable_changer::Type::addictable);
(*window)["PHASE"] = new wheel_variable_changer([](double) { preview_editor_lfo(); }, 70, 0, 0, 15,
	system_white, "Phase deg", "Step", wheel_variable_changer::Type::addictable);
(*window)["SHAPE"] = new button("Shape: Sine", system_white, on_editor_lfo_shape_cycle, 0, -55, 110, 12, 1,
	0x7F3FFF3F, 0x7F3FFFFF, 0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr, "Cycle sine / triangle / square");
(*window)["APPLY"] = new button("Accept", system_white, on_editor_apply_lfo, -42, -108, 70, 12, 1,
	0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Write LFO values as one undoable edit");
(*window)["CANCEL"] = new button("Cancel", system_white, []() { close_editor_tool("MIDI_LFO"); }, 42, -108, 70, 12, 1,
	0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, "Close without editing");
(*global_window_handler)["MIDI_LFO"] = window;
}

#undef _WH_t
