#pragma once

#include <string>
#include <vector>

// Playback controls and device selection shared by the player and editor UI.
void report_player_output_error();
void update_device_list();
bool try_open_drop_in_player(const std::vector<std::wstring>& filenames);
void open_regular_midi_file(std::wstring filename);

void on_device_select(int device_id);
void on_player_pause_toggle();
void on_player_stop();
void on_view_length_change(float value);
void on_unbuffered_switch();
void on_overlap_removal_switch_action(bool with_increment);
void on_overlap_removal_switch();
void on_playback_seek_to(float value);
