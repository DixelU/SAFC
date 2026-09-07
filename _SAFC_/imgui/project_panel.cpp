#include "project_panel.h"
#include "analysis_panel.h"
#include "folded_theme.h"
#include "widgets.h"
#include <imgui.h>
#include <array>
#include <cfloat>

namespace safc::imgui_ui
{
void draw_processing_flags(std::uint32_t& flags)
{
    constexpr std::pair<const char*, std::uint32_t> fields[] = {
        {"Remove empty tracks", remove_empty_tracks}, {"Remove intermediate files", remove_remnants},
        {"All instruments to piano", all_instruments_to_piano}, {"Ignore tempos", ignore_tempos},
        {"Ignore pitch bends", ignore_pitches}, {"Ignore notes", ignore_notes},
        {"Keep only tempo, notes and pitch", ignore_all_but_tempos_notes_and_pitch},
        {"Enable event allowlist", enable_important_filter}};
    for (const auto& [label, flag] : fields) ImGui::CheckboxFlags(label, &flags, flag);
    if (flags & enable_important_filter)
    {
        ImGui::Indent();
        ImGui::CheckboxFlags("Allow notes", &flags, imp_filter_allow_notes);
        ImGui::CheckboxFlags("Allow pitch", &flags, imp_filter_allow_pitch);
        ImGui::CheckboxFlags("Allow tempo", &flags, imp_filter_allow_tempo);
        ImGui::CheckboxFlags("Allow program changes", &flags, imp_filter_allow_progc);
        ImGui::CheckboxFlags("Allow other events", &flags, imp_filter_allow_other);
        ImGui::Unindent();
    }
}

project_panel::project_panel(project_session& project, analysis_panel& analysis, native_dialogs dialogs)
    : project_(project), analysis_(analysis), dialogs_(std::move(dialogs)) {}

void project_panel::properties(file_settings& f)
{
    ImGui::SeparatorText("Selected MIDI");
    ImGui::TextWrapped("%s", f.appearance_filename.c_str());
    ImGui::TextDisabled("%llu bytes | %u tracks | original PPQN %u", f.filesize, f.old_track_number, f.old_ppqn);
    if (ImGui::Button("Play") && play) play(f.filename);
    ImGui::SameLine();
    if (ImGui::Button("Edit") && edit) edit(f.filename);
    ImGui::SameLine();
    if (ImGui::Button("Analyze / collect time map"))
    {
        const auto id = focused_;
        analysis_.open_file(f.filename, f.new_ppqn, f.allow_legacy_rsb_meta_interaction,
            [this, id](single_midi_info_collector::time_graph map)
            { if (auto* current = project_.find(id)) current->time_map = std::move(map); });
        if (show_analysis) show_analysis();
    }
    if (ImGui::BeginTable("File timing", 2, ImGuiTableFlags_SizingStretchSame))
    {
        auto scalar = [](const char* label, ImGuiDataType type, void* value)
        {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
            ImGui::TableNextColumn(); ImGui::PushID(label); ImGui::SetNextItemWidth(-1);
            const bool changed = ImGui::InputScalar("##value", type, value); ImGui::PopID(); return changed;
        };
        if (scalar("PPQN", ImGuiDataType_U16, &f.new_ppqn)) f.ppqn_manually_set = true;
        scalar("Tempo (0 = keep)", ImGuiDataType_Double, &f.new_tempo);
        scalar("Offset ticks", ImGuiDataType_S64, &f.offset_ticks);
        scalar("Selection start", ImGuiDataType_S64, &f.selection_start);
        scalar("Length (-1 = all)", ImGuiDataType_S64, &f.selection_length);
        scalar("Processing group", ImGuiDataType_S16, &f.group_id);
        ImGui::EndTable();
    }
    ImGui::Checkbox("Apply offset after PPQN scaling", &f.apply_offset_after);
    if (!(f.bool_settings & remove_remnants))
    {
        std::array<char, 512> suffix{};
        std::copy_n(f.file_name_postfix.data(), std::min(f.file_name_postfix.size(), suffix.size() - 1), suffix.data());
        if (ImGui::InputText("Intermediate suffix", suffix.data(), suffix.size()))
        {
            f.file_name_postfix = suffix.data();
            f.w_file_name_postfix = std::filesystem::u8path(f.file_name_postfix).wstring();
        }
    }
    if (ImGui::CollapsingHeader("Events and track processing"))
    {
        draw_processing_flags(f.bool_settings);
        ImGui::Separator();
        ImGui::Checkbox("Split tracks by channel", &f.channels_split);
        ImGui::Checkbox("Collapse tracks", &f.collapse_midi);
        ImGui::Checkbox("In-place merge", &f.inplace_merge_enabled);
        ImGui::Checkbox("Running-status compression", &f.rsb_compression);
        ImGui::Checkbox("Legacy running-status meta interaction", &f.allow_legacy_rsb_meta_interaction);
        ImGui::Checkbox("Allow SysEx", &f.allow_sysex);
        ImGui::Checkbox("Enable zero-velocity notes", &f.enable_zero_velocity);
    }
    ImGui::SeparatorText("Transform maps");
    if (ImGui::Button("Cut / transpose")) { key_mapped_ = focused_; key_open_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("Velocity")) { volume_mapped_ = focused_; volume_open_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("Pitch bend")) { pitch_mapped_ = focused_; pitch_open_ = true; }
    ImGui::TextDisabled("Maps: keys %s | velocity %s | pitch %s | time %s",
        f.key_map ? "on" : "off", f.volume_map ? "on" : "off", f.pitch_bend_map ? "on" : "off", f.time_map.empty() ? "empty" : "collected");
    if (ImGui::Button("Remove this file's maps"))
    { f.key_map.reset(); f.volume_map.reset(); f.pitch_bend_map.reset(); f.time_map.clear(); }
    if (ImGui::Button("Copy processing settings to selected files"))
    {
        for (auto id : selected_) if (id != focused_) if (auto* other = project_.find(id))
        {
            auto copy = f;
            copy.filename = other->filename; copy.postprocessed_file_name = other->postprocessed_file_name;
            copy.appearance_filename = other->appearance_filename; copy.appearance_path = other->appearance_path;
            copy.filesize = other->filesize; copy.old_ppqn = other->old_ppqn; copy.old_track_number = other->old_track_number;
            copy.group_id = other->group_id; copy.time_map.clear();
            if (f.key_map) copy.key_map = std::make_shared<cut_and_transpose>(*f.key_map);
            if (f.volume_map) copy.volume_map = std::make_shared<mapping_panel::volume_curve>(*f.volume_map);
            if (f.pitch_bend_map) copy.pitch_bend_map = std::make_shared<mapping_panel::pitch_curve>(*f.pitch_bend_map);
            *other = std::move(copy);
        }
    }
}

void project_panel::draw(bool* open)
{
    project_.poll();
    if (!project_.find(focused_) && !project_.data.files.empty())
    { focused_ = project_.id_at(0); selected_.insert(focused_); }
    const bool busy = project_.loading() || project_.merging();
    ImGui::SetNextWindowPos({24, 86}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({1010, 700}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({690, 420}, {FLT_MAX, FLT_MAX});
    if (begin_folded_window("SAFC project", open))
    {
        ImGui::BeginDisabled(busy);
        try
        {
            if (ImGui::Button("Add MIDIs...") && dialogs_.add_midis) project_.add_files(dialogs_.add_midis());
            ImGui::SameLine();
            ImGui::BeginDisabled(selected_.empty());
            if (ImGui::Button("Remove selected"))
            { project_.remove({selected_.begin(), selected_.end()}); selected_.clear(); }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Select all")) for (size_t i = 0; i < project_.data.files.size(); ++i) selected_.insert(project_.id_at(i));
            ImGui::SameLine();
            if (ImGui::Button("Remove all")) ImGui::OpenPopup("Remove all MIDIs?");
            if (ImGui::BeginPopupModal("Remove all MIDIs?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Remove every MIDI and its processing settings from this project?");
                if (ImGui::Button("Remove"))
                {
                    std::vector<std::uint64_t> ids;
                    for (size_t i = 0; i < project_.data.files.size(); ++i) ids.push_back(project_.id_at(i));
                    project_.remove(ids); selected_.clear(); ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        catch (const std::exception& error) { notice_ = error.what(); }
        ImGui::EndDisabled();
        const float body_height = std::max(160.f, ImGui::GetContentRegionAvail().y - 170.f);
        if (ImGui::BeginTable("Project columns", 2, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch, 0.52f);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.48f);
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            if (ImGui::BeginTable("MIDI files", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, {0, body_height}))
            {
                ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("PPQN", ImGuiTableColumnFlags_WidthFixed, 48);
                ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, 48);
                ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 48);
                ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>(project_.data.files.size()));
                while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    auto& f = project_.data.files[i]; const auto id = project_.id_at(i);
                    ImGui::PushID(static_cast<int>(id)); ImGui::TableNextRow(); ImGui::TableNextColumn();
                    if (literal_selectable("##file", f.appearance_filename, selected_.contains(id), ImGuiSelectableFlags_SpanAllColumns))
                    {
                        if (!ImGui::GetIO().KeyCtrl) selected_.clear();
                        if (selected_.contains(id)) selected_.erase(id); else selected_.insert(id);
                        focused_ = id;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.appearance_path.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%u", f.new_ppqn);
                    ImGui::TableNextColumn(); ImGui::Text("%u", f.old_track_number);
                    ImGui::TableNextColumn(); ImGui::Text("%d", f.group_id); ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TableNextColumn(); ImGui::BeginChild("Properties", {0, body_height});
            ImGui::BeginDisabled(busy);
            if (auto* f = project_.find(focused_))
            {
                ImGui::PushID(static_cast<int>(focused_));
                properties(*f);
                ImGui::PopID();
            }
            else ImGui::TextWrapped("Select a MIDI to edit processing settings, maps, and timing.");
            ImGui::EndDisabled(); ImGui::EndChild(); ImGui::EndTable();
        }
        ImGui::BeginDisabled(busy);
        ImGui::SeparatorText("Global overrides");
        ImGui::SetNextItemWidth(80); ImGui::InputInt("PPQN", &global_ppq_, 0); ImGui::SameLine();
        if (ImGui::Button("Apply##PPQ")) { if (global_ppq_ > 0 && global_ppq_ <= 65535) project_.data.set_global_ppqn(static_cast<std::uint16_t>(global_ppq_), true); }
        ImGui::SameLine(); ImGui::SetNextItemWidth(95); ImGui::InputInt("Offset", &global_offset_, 0); ImGui::SameLine();
        if (ImGui::Button("Apply##Offset")) project_.data.set_global_offset(global_offset_);
        ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputFloat("Tempo", &global_tempo_, 0, 0, "%.2f"); ImGui::SameLine();
        if (ImGui::Button("Apply##Tempo") && std::isfinite(global_tempo_) && global_tempo_ >= 0) project_.data.set_global_tempo(global_tempo_);
        if (ImGui::Button("Auto PPQN")) { project_.data.global_ppqn = 0; project_.data.set_global_ppqn(); }
        ImGui::SameLine();
        if (ImGui::Button("Balance processing groups"))
        { const auto save = project_.data.save_path; project_.data.resolve_subdivision_problem_group_id_assign(); project_.data.save_path = save; }
        ImGui::SameLine();
        if (ImGui::Button("Remove all transform maps")) for (auto& f : project_.data.files)
        { f.key_map.reset(); f.volume_map.reset(); f.pitch_bend_map.reset(); f.time_map.clear(); }
        try
        {
            if (ImGui::Button("Save as...") && dialogs_.save_midi)
                if (auto path = dialogs_.save_midi(project_.data.save_path); !path.empty()) project_.data.save_path = std::move(path);
            ImGui::SameLine();
            ImGui::BeginDisabled(project_.data.files.empty());
            if (ImGui::Button("Start merging"))
            {
                if (project_.data.save_path.empty() && dialogs_.save_midi) project_.data.save_path = dialogs_.save_midi(L"merged.mid");
                if (!project_.data.save_path.empty()) ImGui::OpenPopup("Write merged MIDI?");
            }
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Write merged MIDI?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                const auto path = std::filesystem::path(project_.data.save_path).u8string();
                ImGui::TextUnformatted("Merge the project into this file (replace it if it exists)?");
                ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
                if (ImGui::Button("Merge")) { project_.start_merge(); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        catch (const std::exception& error) { notice_ = error.what(); }
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::TextDisabled("%zu MIDIs | output PPQN %u", project_.data.files.size(), project_.data.global_ppqn);
        const auto message = project_.message();
        if (project_.loading()) ImGui::TextUnformatted("Checking MIDI files...");
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
        if (!notice_.empty()) ImGui::TextWrapped("%s", notice_.c_str());
        const auto p = project_.progress();
        if (!p.stage.empty())
        {
            ImGui::TextWrapped("%s | %.1fs", p.stage.c_str(), p.seconds);
            if (!p.error.empty()) ImGui::TextWrapped("%s", p.error.c_str());
            if (p.busy)
            {
                ImGui::SameLine(); if (ImGui::Button(p.cancelling ? "Cancelling..." : "Cancel merge")) project_.cancel_merge();
                ImGui::ProgressBar(p.fraction, {-1, 0});
                for (const auto& item : p.files) ImGui::TextWrapped("%s: %s (%.0f%%)", item.file.c_str(), item.message.c_str(), item.fraction * 100);
            }
        }
    }
    end_folded_window();
    if (auto* f = project_.find(key_mapped_); f && key_open_) mappings_.draw_key_map(f->appearance_filename.c_str(), f->key_map, &key_open_);
    if (auto* f = project_.find(volume_mapped_); f && volume_open_) mappings_.draw_volume_map(f->appearance_filename.c_str(), f->volume_map, &volume_open_);
    if (auto* f = project_.find(pitch_mapped_); f && pitch_open_) mappings_.draw_pitch_map(f->appearance_filename.c_str(), f->pitch_bend_map, &pitch_open_);
}
}
