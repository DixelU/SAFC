#include "project_panel.h"
#include "analysis_panel.h"
#include "folded_theme.h"
#include "widgets.h"
#include <imgui.h>
#include <array>
#include <cfloat>
#include <cmath>

namespace safc::imgui_ui
{
void draw_processing_flags(std::uint32_t& flags)
{
	constexpr std::pair<const char*, std::uint32_t> fields[] = {{"Remove empty tracks", remove_empty_tracks},
		{"Remove intermediate files", remove_remnants}, {"All instruments to piano", all_instruments_to_piano},
		{"Ignore tempos", ignore_tempos}, {"Ignore pitch bends", ignore_pitches}, {"Ignore notes", ignore_notes},
		{"Keep only tempo, notes and pitch", ignore_all_but_tempos_notes_and_pitch},
		{"Enable event allowlist", enable_important_filter}};
	for (const auto& [label, flag] : fields)
		ImGui::CheckboxFlags(label, &flags, flag);
	// The allowlist checkbox is the last field.
	ImGui::SetItemTooltip("Drop whole tracks that contain none of the event types allowed below.\n"
						  "Events inside the tracks that are kept are not filtered.");
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
	: project_(project), analysis_(analysis), dialogs_(std::move(dialogs))
{
}

bool project_panel::action_button(const char* label)
{
	// A new action replaces the notice left by an earlier one.
	if (!ImGui::Button(label))
		return false;
	notice_.clear();
	return true;
}

std::size_t project_panel::copy_processing_settings(const file_settings& file)
{
	std::size_t copied = 0;
	for (const auto id : selected_)
	{
		if (id == focused_)
			continue;
		auto* other = project_.find(id);
		if (!other)
			continue;

		// Keep each destination's identity and clone mutable maps for independent editing.
		auto copy = file;
		copy.filename = other->filename;
		copy.postprocessed_file_name = other->postprocessed_file_name;
		copy.appearance_filename = other->appearance_filename;
		copy.appearance_path = other->appearance_path;
		copy.filesize = other->filesize;
		copy.old_ppqn = other->old_ppqn;
		copy.old_track_number = other->old_track_number;
		copy.group_id = other->group_id;
		copy.time_map.clear();
		if (file.key_map)
			copy.key_map = std::make_shared<cut_and_transpose>(*file.key_map);
		if (file.volume_map)
			copy.volume_map = std::make_shared<mapping_panel::volume_curve>(*file.volume_map);
		if (file.pitch_bend_map)
			copy.pitch_bend_map = std::make_shared<mapping_panel::pitch_curve>(*file.pitch_bend_map);
		*other = std::move(copy);
		++copied;
	}
	return copied;
}

void project_panel::properties(file_settings& file)
{
	ImGui::SeparatorText("Selected MIDI");
	ImGui::TextWrapped("%s", file.appearance_filename.c_str());
	ImGui::TextDisabled(
		"%llu bytes | %u tracks | original PPQN %u", file.filesize, file.old_track_number, file.old_ppqn);
	if (ImGui::Button("Play") && play)
		play(file.filename);
	ImGui::SameLine();
	if (ImGui::Button("Edit") && edit)
		edit(file.filename);
	ImGui::SameLine();
	// Opening another MIDI in the analysis panel would cancel its running export.
	ImGui::BeginDisabled(analysis_.export_busy());
	const bool analyze = ImGui::Button("Analyze / collect time map");
	ImGui::EndDisabled();
	if (analysis_.export_busy())
		ImGui::SetItemTooltip("Wait for the analysis export to finish.");
	if (analyze)
	{
		const auto id = focused_;
		analysis_.open_file(file.filename, file.new_ppqn, file.allow_legacy_rsb_meta_interaction,
			[this, id](single_midi_info_collector::time_graph map)
		{
			if (auto* current = project_.find(id))
				current->time_map = std::move(map);
		});
		if (show_analysis)
			show_analysis();
	}
	if (ImGui::BeginTable("File timing", 2, ImGuiTableFlags_SizingStretchSame))
	{
		auto scalar = [](const char* label, ImGuiDataType type, void* value, const char* format = nullptr)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::PushID(label);
			ImGui::SetNextItemWidth(-1);
			const bool changed = ImGui::InputScalar("##value", type, value, nullptr, nullptr, format);
			ImGui::PopID();
			return changed;
		};
		if (scalar("PPQN", ImGuiDataType_U16, &file.new_ppqn))
			file.ppqn_manually_set = true;
		scalar("Tempo BPM (0 = keep)", ImGuiDataType_Double, &file.new_tempo, "%.3f");
		ImGui::SetItemTooltip("Replace every tempo event with this tempo. Use 0 to keep the original tempos;\n"
							  "values above 0 and up to 3 BPM are rejected when merging.");
		scalar("Offset ticks", ImGuiDataType_S64, &file.offset_ticks);
		scalar("Selection start", ImGuiDataType_S64, &file.selection_start);
		scalar("Length (-1 = all)", ImGuiDataType_S64, &file.selection_length);
		scalar("Processing group", ImGuiDataType_S16, &file.group_id);
		ImGui::SetItemTooltip("Files in the same group are processed one after another on one thread;\n"
							  "different groups run in parallel. Added files get the least loaded groups.\n"
							  "Balance processing groups redistributes every file by size.");
		ImGui::EndTable();
	}
	ImGui::Checkbox("Apply offset after PPQN scaling", &file.apply_offset_after);
	ImGui::SetItemTooltip("On: the offset is counted in output ticks, after PPQN conversion.\n"
						  "Off: it is counted in this file's original ticks and scaled with them.");
	if (!(file.bool_settings & remove_remnants))
	{
		std::array<char, 512> suffix{};
		std::copy_n(
			file.file_name_postfix.data(), std::min(file.file_name_postfix.size(), suffix.size() - 1), suffix.data());
		if (ImGui::InputText("Intermediate suffix", suffix.data(), suffix.size()))
		{
			file.file_name_postfix = suffix.data();
			const std::u8string utf8_suffix(file.file_name_postfix.begin(), file.file_name_postfix.end());
			file.w_file_name_postfix = std::filesystem::path(utf8_suffix).wstring();
		}
	}
	if (ImGui::CollapsingHeader("Events and track processing"))
	{
		draw_processing_flags(file.bool_settings);
		ImGui::Separator();
		ImGui::Checkbox("Split tracks by channel", &file.channels_split);
		ImGui::SetItemTooltip("Split tracks that use several MIDI channels into one track per channel.");
		ImGui::Checkbox("Collapse tracks", &file.collapse_midi);
		ImGui::SetItemTooltip("Merge all tracks of this MIDI into one track\n"
							  "(one track per channel when Split tracks by channel is on).");
		// Processing turns in-place merge off for compressed files, so show the effective state.
		bool inplace = file.inplace_merge_enabled && !file.rsb_compression;
		ImGui::BeginDisabled(file.rsb_compression);
		if (ImGui::Checkbox("In-place merge", &inplace))
			file.inplace_merge_enabled = inplace;
		ImGui::EndDisabled();
		if (file.rsb_compression)
			ImGui::SetItemTooltip("Unavailable while running-status compression is on for this file.");
		else
			ImGui::SetItemTooltip("Combine track 1 of every in-place MIDI into one output track, then track 2, and so\n"
								  "on, keeping the output track count low. Slower than the regular merge.");
		ImGui::Checkbox("Running-status compression", &file.rsb_compression);
		ImGui::SetItemTooltip("Write note-offs as zero-velocity note-ons so running status can omit repeated\n"
							  "status bytes, making the output smaller. Disables in-place merge for this file.");
		ImGui::Checkbox("Legacy running-status meta interaction", &file.allow_legacy_rsb_meta_interaction);
		ImGui::SetItemTooltip("Keep the running-status byte across meta events in the input instead of resetting it.\n"
							  "Not standard-conforming; only for very old MIDIs that rely on it.");
		ImGui::Checkbox("Allow SysEx", &file.allow_sysex);
		ImGui::SetItemTooltip("Keep system exclusive events. They are dropped by default because many players\n"
							  "handle them poorly.");
		ImGui::Checkbox("Enable zero-velocity notes", &file.enable_zero_velocity);
	}
	ImGui::SeparatorText("Transform maps");
	if (ImGui::Button("Cut / transpose"))
	{
		key_mapped_ = focused_;
		key_open_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Velocity"))
	{
		volume_mapped_ = focused_;
		volume_open_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Pitch bend"))
	{
		pitch_mapped_ = focused_;
		pitch_open_ = true;
	}
	ImGui::TextDisabled("Maps: keys %s | velocity %s | pitch %s | time %s", file.key_map ? "on" : "off",
		file.volume_map ? "on" : "off", file.pitch_bend_map ? "on" : "off",
		file.time_map.empty() ? "empty" : "collected");
	if (action_button("Remove this file's maps"))
	{
		file.key_map.reset();
		file.volume_map.reset();
		file.pitch_bend_map.reset();
		file.time_map.clear();
	}
	ImGui::BeginDisabled(selected_.size() - selected_.contains(focused_) == 0);
	if (action_button("Copy processing settings to selected files"))
	{
		const auto copied = copy_processing_settings(file);
		notice_ = "Copied processing settings to " + std::to_string(copied) + (copied == 1 ? " file." : " files.");
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(selected_.size() - selected_.contains(focused_) == 0
			? "Select more files with Ctrl-click or Shift-click to copy this file's settings to them."
			: "Copy this file's processing settings and maps to the other selected files.");
}

void project_panel::select(std::size_t index, std::uint64_t id)
{
	const auto& io = ImGui::GetIO();
	selection_cleared_ = false;
	if (io.KeyShift && project_.find(anchor_))
	{
		// Shift selects the list range from the anchor; Ctrl adds it to the selection.
		std::size_t anchor = 0;
		while (project_.id_at(anchor) != anchor_)
			++anchor;
		if (!io.KeyCtrl)
			selected_.clear();
		for (auto i = std::min(anchor, index); i <= std::max(anchor, index); ++i)
			selected_.insert(project_.id_at(i));
		focused_ = id;
		return;
	}
	if (!io.KeyCtrl)
		selected_.clear();
	anchor_ = id;
	if (!selected_.contains(id))
	{
		selected_.insert(id);
		focused_ = id;
		return;
	}
	selected_.erase(id);
	// Keep the properties on a file that is still selected.
	if (focused_ == id)
	{
		focused_ = selected_.empty() ? 0 : *selected_.begin();
		selection_cleared_ = selected_.empty();
	}
}

void project_panel::draw_file_list(float body_height)
{
	const float numeric_column_width = scaled(48.f);
	const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if (!ImGui::BeginTable("MIDI files", 4, flags, {0, body_height}))
		return;
	ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("PPQN", ImGuiTableColumnFlags_WidthFixed, numeric_column_width);
	ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, numeric_column_width);
	ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, numeric_column_width);
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableHeadersRow();
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(project_.data.files.size()));
	while (clipper.Step())
	{
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
		{
			auto& f = project_.data.files[i];
			const auto id = project_.id_at(i);
			ImGui::PushID(static_cast<int>(id));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (literal_selectable(
					"##file", f.appearance_filename, selected_.contains(id), ImGuiSelectableFlags_SpanAllColumns))
				select(static_cast<std::size_t>(i), id);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", f.appearance_path.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%u", f.new_ppqn);
			ImGui::TableNextColumn();
			ImGui::Text("%u", f.old_track_number);
			ImGui::TableNextColumn();
			ImGui::Text("%d", f.group_id);
			ImGui::PopID();
		}
	}
	ImGui::EndTable();
}

void project_panel::draw_columns(bool busy, float body_height)
{
	if (!ImGui::BeginTable("Project columns", 2, ImGuiTableFlags_Resizable))
		return;

	ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch, 0.52f);
	ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.48f);
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	draw_file_list(body_height);
	ImGui::TableNextColumn();
	ImGui::BeginChild("Properties", {0, body_height});
	ImGui::BeginDisabled(busy);
	if (auto* file = project_.find(focused_))
	{
		ImGui::PushID(static_cast<int>(focused_));
		properties(*file);
		ImGui::PopID();
	}
	else
		ImGui::TextWrapped("Select a MIDI to edit processing settings, maps, and timing.");
	ImGui::EndDisabled();
	ImGui::EndChild();
	ImGui::EndTable();
}

void project_panel::draw_file_actions(bool busy)
{
	ImGui::BeginDisabled(busy);
	try
	{
		if (action_button("Add MIDIs...") && dialogs_.add_midis)
			project_.add_files(dialogs_.add_midis());
		ImGui::SameLine();
		ImGui::BeginDisabled(selected_.empty());
		if (action_button("Remove selected"))
		{
			project_.remove({selected_.begin(), selected_.end()});
			selected_.clear();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (action_button("Select all"))
		{
			for (size_t i = 0; i < project_.data.files.size(); ++i)
				selected_.insert(project_.id_at(i));
			selection_cleared_ = false;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(project_.data.files.empty());
		if (action_button("Remove all"))
			ImGui::OpenPopup("Remove all MIDIs?");
		ImGui::EndDisabled();
		if (ImGui::BeginPopupModal("Remove all MIDIs?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Remove every MIDI and its processing settings from this project?");
			if (ImGui::Button("Remove"))
			{
				std::vector<std::uint64_t> ids;
				for (size_t i = 0; i < project_.data.files.size(); ++i)
					ids.push_back(project_.id_at(i));
				project_.remove(ids);
				selected_.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	catch (const std::exception& error)
	{
		notice_ = error.what();
	}
	ImGui::EndDisabled();
}

void project_panel::draw_global_overrides()
{
	auto& data = project_.data;
	// Each field shows the applied project value until the user types a different one.
	const auto mirror = [](auto& field, auto applied, bool& edited)
	{
		using field_type = std::remove_reference_t<decltype(field)>;
		if (!edited || field == static_cast<field_type>(applied))
		{
			field = static_cast<field_type>(applied);
			edited = false;
		}
	};
	mirror(global_ppq_, data.global_ppqn, ppq_edited_);
	mirror(global_offset_, data.global_offset, offset_edited_);
	mirror(global_tempo_, data.global_new_tempo, tempo_edited_);
	// Enter in a field applies it like its Apply button.
	const auto entered = []
	{
		return ImGui::IsItemDeactivatedAfterEdit() &&
			(ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
	};

	ImGui::SeparatorText("Global overrides");
	ImGui::SetNextItemWidth(scaled(80));
	ppq_edited_ |= ImGui::InputInt("PPQN", &global_ppq_, 0);
	bool apply = entered();
	ImGui::SameLine();
	if (action_button("Apply##PPQ") || apply)
	{
		notice_.clear();
		if (global_ppq_ > 0 && global_ppq_ <= 65535)
		{
			data.set_global_ppqn(static_cast<std::uint16_t>(global_ppq_), true);
			ppq_edited_ = false;
		}
		else
			notice_ = "Global PPQN must be 1..65535.";
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(scaled(95));
	offset_edited_ |= ImGui::InputInt("Offset", &global_offset_, 0);
	apply = entered();
	ImGui::SameLine();
	if (action_button("Apply##Offset") || apply)
	{
		notice_.clear();
		data.set_global_offset(global_offset_);
		offset_edited_ = false;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(scaled(80));
	tempo_edited_ |= ImGui::InputFloat("Tempo BPM", &global_tempo_, 0, 0, "%.3f");
	ImGui::SetItemTooltip("0 keeps each file's tempos; otherwise above 3 BPM.");
	apply = entered();
	ImGui::SameLine();
	if (action_button("Apply##Tempo") || apply)
	{
		notice_.clear();
		// Processing ignores tempos of 3 BPM or less, so they cannot be applied.
		if (std::isfinite(global_tempo_) && (global_tempo_ == 0 || (global_tempo_ > 3 && global_tempo_ <= 60000000)))
		{
			data.set_global_tempo(global_tempo_);
			tempo_edited_ = false;
		}
		else
			notice_ = "Global tempo must be 0 (keep the original) or above 3 BPM.";
	}
	if (ppq_edited_ || offset_edited_ || tempo_edited_)
		ImGui::TextDisabled("Edited global values are not applied yet; press Enter or Apply.");
	if (action_button("Auto PPQN"))
	{
		data.forced_ppqn = false;
		data.global_ppqn = 0;
		data.set_global_ppqn();
		ppq_edited_ = false;
	}
	ImGui::SetItemTooltip("Use the largest PPQN of the files, also when files are added later.");
	ImGui::SameLine();
	if (action_button("Balance processing groups"))
	{
		const auto save = data.save_path;
		data.resolve_subdivision_problem_group_id_assign();
		data.save_path = save;
	}
	ImGui::SetItemTooltip("Redistribute every file across processing groups by file size.");
	ImGui::SameLine();
	if (action_button("Remove all transform maps"))
		for (auto& f : data.files)
		{
			f.key_map.reset();
			f.volume_map.reset();
			f.pitch_bend_map.reset();
			f.time_map.clear();
		}
}

void project_panel::draw_merge_controls()
{
	try
	{
		if (action_button("Save as...") && dialogs_.save_midi)
			if (auto path = dialogs_.save_midi(project_.data.save_path); !path.empty())
				project_.data.save_path = std::move(path);
		ImGui::SameLine();
		{
			const auto path = std::filesystem::path(project_.data.save_path).u8string();
			const auto* text = reinterpret_cast<const char*>(path.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Output: %s", path.empty() ? "chosen when merging starts" : text);
			ImGui::PopStyleColor();
			if (!path.empty())
				ImGui::SetItemTooltip("%s", text);
		}
		ImGui::BeginDisabled(project_.data.files.empty());
		if (action_button("Start merging"))
		{
			if (project_.data.save_path.empty() && dialogs_.save_midi)
				project_.data.save_path = dialogs_.save_midi(L"merged.mid");
			if (!project_.data.save_path.empty())
				ImGui::OpenPopup("Write merged MIDI?");
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopupModal("Write merged MIDI?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const auto path = std::filesystem::path(project_.data.save_path).u8string();
			ImGui::TextUnformatted("Merge the project into this file (replace it if it exists)?");
			ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
			if (ImGui::Button("Merge"))
			{
				notice_.clear();
				project_.start_merge();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	catch (const std::exception& error)
	{
		notice_ = error.what();
	}
}

void project_panel::draw_status()
{
	ImGui::SameLine();
	ImGui::TextDisabled("%zu MIDIs | output PPQN %u", project_.data.files.size(), project_.data.global_ppqn);
	const auto message = project_.message();
	if (project_.loading())
		ImGui::TextUnformatted("Checking MIDI files...");
	if (const auto queued = project_.queued())
		ImGui::Text("%zu MIDI(s) queued; they are added when the current job finishes.", queued);
	if (!message.empty())
		ImGui::TextWrapped("%s", message.c_str());
	if (!notice_.empty())
		ImGui::TextWrapped("%s", notice_.c_str());
	const auto p = project_.progress();
	if (!p.stage.empty())
	{
		ImGui::TextWrapped("%s | %.1fs", p.stage.c_str(), p.seconds);
		if (!p.error.empty())
			ImGui::TextWrapped("%s", p.error.c_str());
		if (p.busy)
		{
			ImGui::SameLine();
			if (ImGui::Button(p.cancelling ? "Cancelling..." : "Cancel merge"))
				project_.cancel_merge();
			// A negative fraction marks a phase without measured progress.
			if (p.fraction < 0)
				ImGui::ProgressBar(-float(ImGui::GetTime()), {-1, 0});
			else
				ImGui::ProgressBar(p.fraction, {-1, 0});
			for (const auto& item : p.files)
				ImGui::TextWrapped("%s: %s (%.0f%%)", item.file.c_str(), item.message.c_str(), item.fraction * 100);
		}
	}
}

void project_panel::draw(bool* open)
{
	project_.poll();
	if (project_.data.files.empty())
		selection_cleared_ = false;
	if (!project_.find(focused_) && !project_.data.files.empty() && !selection_cleared_)
	{
		focused_ = project_.id_at(0);
		selected_.insert(focused_);
	}
	const bool busy = project_.loading() || project_.merging();
	ImGui::SetNextWindowPos({scaled(24), scaled(86)}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({scaled(1010), scaled(700)}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints({scaled(690), scaled(420)}, {FLT_MAX, FLT_MAX});
	if (begin_folded_window("SAFC project", open))
	{
		draw_file_actions(busy);
		// Leave room for the controls below the columns, as measured on the previous frame.
		const float top = ImGui::GetCursorPosY();
		const float footer = footer_height_ < 0 ? scaled(170.f) : footer_height_;
		const float body_height = std::max(scaled(160.f), ImGui::GetContentRegionAvail().y - footer);
		draw_columns(busy, body_height);
		ImGui::BeginDisabled(busy);
		draw_global_overrides();
		draw_merge_controls();
		ImGui::EndDisabled();
		draw_status();
		footer_height_ = ImGui::GetCursorPosY() - top - body_height;
	}
	end_folded_window();
	if (auto* f = project_.find(key_mapped_); f && key_open_)
		mappings_.draw_key_map(f->appearance_filename.c_str(), f->key_map, &key_open_);
	if (auto* f = project_.find(volume_mapped_); f && volume_open_)
		mappings_.draw_volume_map(f->appearance_filename.c_str(), f->volume_map, &volume_open_);
	if (auto* f = project_.find(pitch_mapped_); f && pitch_open_)
		mappings_.draw_pitch_map(f->appearance_filename.c_str(), f->pitch_bend_map, &pitch_open_);
}
}
