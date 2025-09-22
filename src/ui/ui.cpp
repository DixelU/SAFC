#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

#include <imgui.h>
#include <log_c/log.h>

#include "ui.hpp"

#include "../core/SMRP2.h"
#include "../core/MCTM.h"

static ImGuiSelectionBasicStorage midi_selection;
static ImGuiMultiSelectIO* ms_io;

static int midi_settings_modes = 0;
static char it_ppq[256] = "1920";
static char it_offset[380] = "0";
static char it_tempo[380] = "120";

static bool temp = false;

// Ohh god that's weird

std::pair<MIDICollectionThreadedMerger::proc_data_ptr, MIDICollectionThreadedMerger::message_buffer_ptr> SMRP;
		
void SetSMRP(std::pair< MIDICollectionThreadedMerger::proc_data_ptr, MIDICollectionThreadedMerger::message_buffer_ptr>& smrp)
{
	std::lock_guard<std::recursive_mutex> locker(Lock);
	SMRP = smrp;
}


// Stolen from NVi-PFA :madman:
std::string FilenameOnly(const std::string& path) 
{
	size_t slash = path.find_last_of("/\\");
	return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

void RenderMainWindow()
{	 
	ImGui::SetNextWindowSizeConstraints(ImVec2(700, 700), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::Begin("SAFC Window", nullptr, ImGuiWindowFlags_MenuBar);
	if(ImGui::BeginMenuBar())
	{
		if(ImGui::BeginMenu("File"))
		{
			if(ImGui::MenuItem("Add MIDIs"))
			{
				std::string midi_filename = sfd_open_dialog(&midi_ofd_opts);
				
				if(!midi_filename.empty())
				{
				  log_info("Got file: '%s'", midi_filename.c_str());
				  midi_list.emplace_back(midi_filename);
				}
				else
				{
				  log_info("Open canceled");
				}
				log_info("add midi");
			}
			
			if(ImGui::MenuItem("Remove MIDIs"))
			{
				std::vector<int> selected_indices;
				for(int i = 0; i < midi_list.size(); ++i)
				{
					if(midi_selection.Contains((ImGuiID)i))
						selected_indices.push_back(i);
				}
				
				// Erase in reverse order to avoid shifting indices
				std::sort(selected_indices.rbegin(), selected_indices.rend());
				for(int idx : selected_indices)
				{
					midi_list.erase(midi_list.begin() + idx);
				}
				
				midi_selection.Clear();
			}
			
			if(ImGui::MenuItem("Save & Merge"))
			{
				const char *filename = sfd_save_dialog(&midi_sfd_opts);
				
				if(filename)
				{
				  log_info("save file: '%s'", filename);
				}
				else
				{
				  log_info("Open canceled");
				}
			}
			
			ImGui::Separator();
			
			if(ImGui::MenuItem("Clear MIDI List"))
			{
				midi_list.clear();
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
		clipper.Begin(midi_list.size());
		if(ms_io->RangeSrcItem != -1)
			clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem); // Ensure RangeSrc item is not clipped.
		
		while(clipper.Step())
		{
			for(int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++)
			{
				ImGui::PushID(n);
				char label[500];
				snprintf(label, sizeof(label), "%s", FilenameOnly(midi_list[n]).c_str());
				bool item_is_selected = midi_selection.Contains((ImGuiID)n);
				ImGui::SetNextItemSelectionUserData(n);
				ImGui::Selectable(label, item_is_selected);
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