#pragma once

#include <atomic>
#include <memory>
#include <string>

class compressed_midi_event_source;

// Shared with source-dialog lifecycle and video rendering.
extern std::atomic<bool> compressed_player_preparing;
extern std::atomic<bool> compressed_player_cancel;
extern std::atomic<bool> compressed_player_source_selected;

void compressed_player_status(const std::string& message);
void select_regular_player_source();
std::shared_ptr<compressed_midi_event_source> current_compressed_player_source();
std::wstring current_compressed_player_filename();
bool restart_selected_compressed_source();
void open_player_file(std::wstring filename);

void on_player_source_open();
void on_compressed_preparation_cancel();
