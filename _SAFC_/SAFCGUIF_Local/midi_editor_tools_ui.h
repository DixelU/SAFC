#pragma once
#ifndef SAFC_MIDI_EDITOR_TOOLS_UI
#define SAFC_MIDI_EDITOR_TOOLS_UI

#include <functional>
#include <string>

void on_editor_open_chopper();
void on_editor_open_flip();
void on_editor_open_claw();
void on_editor_open_lfo();

void initialize_midi_editor_tool_windows(
	std::function<void(const std::string&)> status_callback,
	std::function<void(const std::string&)> warning_callback,
	unsigned background_color, unsigned header_color, unsigned border_color);

#endif
