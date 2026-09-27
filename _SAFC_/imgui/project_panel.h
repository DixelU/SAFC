#pragma once
#include "project_session.h"
#include "platform_dialogs.h"
#include "mapping_panel.h"
#include <functional>
#include <unordered_set>

namespace safc::imgui_ui
{
class analysis_panel;
void draw_processing_flags(std::uint32_t& flags);
class project_panel
{
public:
	project_panel(project_session& project, analysis_panel& analysis, native_dialogs dialogs);
	void draw(bool* open);
	std::function<void(std::wstring)> play, edit;
	std::function<void()> show_analysis;

private:
	project_session& project_;
	analysis_panel& analysis_;
	native_dialogs dialogs_;
	mapping_panel mappings_;
	std::unordered_set<std::uint64_t> selected_;
	// anchor_ is the last plain or Ctrl-clicked file, where Shift-click ranges start.
	std::uint64_t focused_ = 0, anchor_ = 0, key_mapped_ = 0, volume_mapped_ = 0, pitch_mapped_ = 0;
	bool key_open_ = false, volume_open_ = false, pitch_open_ = false;
	// Set when Ctrl-click deselects every file, so no file is focused automatically.
	bool selection_cleared_ = false;
	// Global fields mirror the project until edited; *_edited_ marks a value not applied yet.
	int global_ppq_ = 0, global_offset_ = 0;
	float global_tempo_ = 0;
	bool ppq_edited_ = false, offset_edited_ = false, tempo_edited_ = false;
	// Height below the file columns, measured on the previous frame (negative before the first).
	float footer_height_ = -1.f;
	std::string notice_;

	bool action_button(const char* label);
	void properties(file_settings& file);
	std::size_t copy_processing_settings(const file_settings& file);
	void select(std::size_t index, std::uint64_t id);
	void draw_file_actions(bool busy);
	void draw_file_list(float body_height);
	void draw_columns(bool busy, float body_height);
	void draw_global_overrides();
	void draw_merge_controls();
	void draw_status();
};
}
