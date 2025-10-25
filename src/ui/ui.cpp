#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

#include <imgui.h>
#include <log_c/log.h>

#include "ui.hpp"

#include "../core/SMRP2.h"
#include "../core/MCTM.h"

#include "../ui/ofd.h"

static ImGuiSelectionBasicStorage midi_selection;
static ImGuiMultiSelectIO* ms_io;

static int midi_settings_modes = 0;
static char it_ppq[256] = "1920";
static char it_offset[380] = "0";
static char it_tempo[380] = "120";

static bool temp = false;

// Ohh god that's weird
std::pair<midi_collection_threaded_merger::proc_data_ptr, midi_collection_threaded_merger::message_buffer_ptr> SMRP;
void SetSMRP(std::pair< midi_collection_threaded_merger::proc_data_ptr, midi_collection_threaded_merger::message_buffer_ptr>& smrp)
{
	std::lock_guard<std::recursive_mutex> locker(Lock);
	SMRP = smrp;
}

void RenderMainWindow()
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(700, 700), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_MenuBar);
	if(ImGui::BeginMenuBar())
	{
		if(ImGui::BeginMenu("File"))
		{
			if(ImGui::MenuItem("Add MIDIs"))
			{
				auto midis = OpenFileDlg(to_cchar_t("Add MIDI Files"));
				
				for (auto& file : midis)
				{
					midi_file_meta meta;
					meta.set(std::move(file));

					log_info("File: %s", meta.visible_path.c_str());

					global_midi_list.emplace_back(std::move(meta));
				}
				
				log_info("add midi");
			}
			
			if(ImGui::MenuItem("Remove MIDIs"))
			{
				std::vector<int> selected_indices;
				for(int i = 0; i < global_midi_list.size(); ++i)
				{
					if(midi_selection.Contains((ImGuiID)i))
						selected_indices.push_back(i);
				}
				
				// Erase in reverse order to avoid shifting indices
				std::sort(selected_indices.rbegin(), selected_indices.rend());
				for(int idx : selected_indices)
				{
					global_midi_list.erase(global_midi_list.begin() + idx);
				}
				
				midi_selection.Clear();
			}
			
			if(ImGui::MenuItem("Save & Merge"))
			{
				std_unicode_string save_to = SaveFileDlg(to_cchar_t("Save file"));

				if(!save_to.empty())
				{
					save_to_file.set(std::move(save_to));
					log_info("Save file: '%s'", save_to_file.visible_path.c_str()); // todo: log visible filename only
				}
				else
				{
					log_info("Save canceled");
				}
			}
			
			ImGui::Separator();
			
			if(ImGui::MenuItem("Clear MIDI List"))
			{
				global_midi_list.clear();
			}
			ImGui::EndMenu();
		}
		
		if(ImGui::BeginMenu("Edit"))
		{
			if(ImGui::MenuItem("MIDI Settings"))
			{
				midi_settings_window = true;
				log_info("midi settings");
			}
			
			if(ImGui::MenuItem("temp"))
			{
				temp = true;
			}
			ImGui::EndMenu();   
		}
		ImGui::EndMenuBar();
	}
	
	const int ITEMS_COUNT = 30;
	if(ImGui::BeginChild("##MIDI_LS", ImVec2(0, 0), ImGuiChildFlags_FrameStyle))
	{
		ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
		ms_io = ImGui::BeginMultiSelect(flags, midi_selection.Size, ITEMS_COUNT);
		midi_selection.ApplyRequests(ms_io);
		
		ImGuiListClipper clipper;
		clipper.Begin(global_midi_list.size());
		if(ms_io->RangeSrcItem != -1)
			clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem); // Ensure RangeSrc item is not clipped.
		
		while(clipper.Step())
		{
			for(int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++)
			{
				ImGui::PushID(n);
				bool item_is_selected = midi_selection.Contains((ImGuiID)n);
				ImGui::SetNextItemSelectionUserData(n);
				ImGui::Selectable(global_midi_list[n].visible_name.c_str(), item_is_selected);
				ImGui::PopID();
			}
		}
		
		ms_io = ImGui::EndMultiSelect();
		midi_selection.ApplyRequests(ms_io);
		ImGui::EndChild();
	}
	ImGui::End();
	
	if(midi_settings_window)
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2(500, 500), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin("MIDI Settings", &midi_settings_window, 0);
		ImGui::RadioButton("Individually", &midi_settings_modes, 0);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if(ImGui::BeginItemTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
			ImGui::Text("MIDI Settings only apply for the selected ones");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::RadioButton("Globally", &midi_settings_modes, 1);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if(ImGui::BeginItemTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
			ImGui::Text("MIDI Settings apply globally to all midis in the list");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::Separator();
		ImGui::InputText("PPQN", it_ppq, IM_ARRAYSIZE(it_ppq), ImGuiInputTextFlags_CharsDecimal);
		ImGui::InputText("Tempo", it_tempo, IM_ARRAYSIZE(it_tempo), ImGuiInputTextFlags_CharsDecimal);
		ImGui::InputText("Offset", it_offset, IM_ARRAYSIZE(it_offset), ImGuiInputTextFlags_CharsDecimal);
		ImGui::Checkbox("Remove Empty Tracks", &cb_rm_empty_tracks);
		ImGui::Checkbox("Remove Merge Remnants", &cb_rm_merge_remnants);
		ImGui::Checkbox("All Instruments to Piano", &cb_all_instruments_2_piano);
		ImGui::Checkbox("Ignore Tempo Events", &cb_ignore_tempo_events);
		ImGui::Checkbox("Ignore Pitch bend Events", &cb_ignore_pitchbend_events);
		ImGui::Checkbox("Ignore Note Events", &cb_ignore_note_events);
		ImGui::Checkbox("Ignore Except Everything Specified", &cb_ignore_except_everythig_spec);
		ImGui::Checkbox("Multichannel Split", &cb_multichannel_split);
		ImGui::Checkbox("RSB Compression", &cb_rsb_compression);
		ImGui::Checkbox("Inplace Merge", &cb_implace_merge);
		ImGui::Checkbox("Apply Offset After PPQ Change", &cb_apply_offset_after_ppq_change);
		ImGui::Checkbox("Collapse Tracks into One", &cb_collapse_trk_into_1);
		ImGui::Checkbox("Allow SysEx Events", &cb_allow_sysex_events);
		
		ImGui::End();
	}
	
	std::lock_guard<std::recursive_mutex> locker(Lock);
	
	// Causes segfault involving atomic_bool loading
	//if(SMRP.second->processing)
	//	ImGui::OpenPopup("Procesing Progress");
	
	if(ImGui::BeginPopupModal("Procesing Progress", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		// HELP ME...
		// IDFK What I'm doing
		//SetSMRP(GlobalMCTM->currently_processed[ID]);
		static float progress = 0.0f, progress_dir = 1.0f;
		progress += progress_dir * 0.4f * ImGui::GetIO().DeltaTime;
		float progress_saturated = IM_CLAMP(progress, 0.0f, 1.0f);
		char buf[32];
		sprintf(buf, "%d/%d", (int)(progress_saturated * 1753), 1753);
		ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), buf);
		ImGui::EndPopup();
	}
}