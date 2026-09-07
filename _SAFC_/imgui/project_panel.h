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
    std::uint64_t focused_ = 0, key_mapped_ = 0, volume_mapped_ = 0, pitch_mapped_ = 0;
    bool key_open_ = false, volume_open_ = false, pitch_open_ = false;
    int global_ppq_ = 960, global_offset_ = 0;
    float global_tempo_ = 0;
    std::string notice_;
    void properties(file_settings& file);
};
}
