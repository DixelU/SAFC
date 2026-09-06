#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "../SAFCGUIF_Local/midi_editor_viewer.h"

// The player is shared; the editor cursor only follows editor playback.
extern std::atomic<bool> editor_playback_active;

std::uint32_t editor_channel_button_color(int track, int channel, bool hover = false);
void editor_flash_status(const std::string& message);
void update_channel_indicator();
void update_editor_track_list();
void update_editor_status_text();
void update_editor_playback_status();

void on_editor_load_file();
void on_editor_save_file();
void on_editor_undo();
void on_editor_redo();
void on_editor_play();
void on_editor_play_from_view();
void on_editor_channel_select(int channel);
void on_editor_toggle_lane();
void on_editor_snap_cycle();
void on_editor_track_next();
void on_editor_track_prev();
void on_editor_track_list_select(int index);
void on_editor_rename_track();
void on_editor_lane_mode(midi_editor_viewer::lane_mode mode);
