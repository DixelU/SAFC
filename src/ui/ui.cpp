#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

#include <imgui.h>
#include <log_c/log.h>

#include "ui.hpp"

#include "../core/single_midi_processor_2.h"
#include "../core/midi_collection_merger.h"

#include "../ui/ofd.h"

static ImGuiSelectionBasicStorage midi_selection;
static ImGuiMultiSelectIO* ms_io;

static int midi_settings_modes = 0;
static char it_ppq[256] = "1920";
static char it_offset[380] = "0";
static char it_tempo[380] = "120";

static bool temp = false;

void ThrowAlert_Error(std::string&& AlertText)
{
	safc_globals::global_alert_queue.push_back({ std::move(AlertText), "Error", safc_globals::alert_queue_item::alert_type::error });
}

void ThrowAlert_Warning(std::string&& AlertText)
{
	safc_globals::global_alert_queue.push_back({ std::move(AlertText), "Warning", safc_globals::alert_queue_item::alert_type::warning });
}

void ThrowAlert_Info(std::string&& AlertText)
{
	safc_globals::global_alert_queue.push_back({ std::move(AlertText), "Info", safc_globals::alert_queue_item::alert_type::info });
}

void AddFiles(const std::vector<std_unicode_string>& filenames) 
{
	for (auto& file : filenames)
	{
		midi_file_meta meta;
		meta.set(std::move(file));

		log_info("File: %s", meta.visible_path.c_str());

		safc_globals::global_midi_list.emplace_back(std::move(meta));
	}
}

// Ohh god that's weird
std::pair<midi_collection_threaded_merger::proc_data_ptr, midi_collection_threaded_merger::message_buffer_ptr> SMRP;
void SetSMRP(std::pair< midi_collection_threaded_merger::proc_data_ptr, midi_collection_threaded_merger::message_buffer_ptr>& smrp)
{
	std::lock_guard<std::recursive_mutex> locker(safc_globals::global_lock);
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
				
				log_info("add midi");
			}
			
			if(ImGui::MenuItem("Remove MIDIs"))
			{
				std::vector<int> selected_indices;
				for(int i = 0; i < safc_globals::global_midi_list.size(); ++i)
				{
					if(midi_selection.Contains((ImGuiID)i))
						selected_indices.push_back(i);
				}
				
				// Erase in reverse order to avoid shifting indices
				std::sort(selected_indices.rbegin(), selected_indices.rend());
				for(int idx : selected_indices)
				{
					safc_globals::global_midi_list.erase(safc_globals::global_midi_list.begin() + idx);
				}
				
				midi_selection.Clear();
			}
			
			// todo: automatic save filename generation
			if(ImGui::MenuItem("Save & Merge"))
			{
				std_unicode_string save_to = SaveFileDlg(to_cchar_t("Save file"));

				if(!save_to.empty())
				{
					safc_globals::save_to_file.set(std::move(save_to));
					log_info("Save file: '%s'", safc_globals::save_to_file.visible_path.c_str()); // todo: log visible filename only
				}
				else
				{
					// todo replace with log_info
					ThrowAlert_Error("Save canceled");
				}
			}
			
			ImGui::Separator();
			
			if(ImGui::MenuItem("Clear MIDI List"))
			{
				safc_globals::global_midi_list.clear();
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
		clipper.Begin(safc_globals::global_midi_list.size());
		if(ms_io->RangeSrcItem != -1)
			clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem); // Ensure RangeSrc item is not clipped.
		
		while(clipper.Step())
		{
			for(int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++)
			{
				ImGui::PushID(n);
				bool item_is_selected = midi_selection.Contains((ImGuiID)n);
				ImGui::SetNextItemSelectionUserData(n);
				ImGui::Selectable(safc_globals::global_midi_list[n].visible_name.c_str(), item_is_selected);
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
		ImGui::Checkbox("Remove Empty Tracks", &safc_globals::cb_rm_empty_tracks);
		ImGui::Checkbox("Remove Merge Remnants", &safc_globals::cb_rm_merge_remnants);
		ImGui::Checkbox("All Instruments to Piano", &safc_globals::cb_ignore_all_instruments_2_piano);
		ImGui::Checkbox("Ignore Tempo Events", &safc_globals::cb_ignore_tempo_events);
		ImGui::Checkbox("Ignore Pitch bend Events", &safc_globals::cb_ignore_pitchbend_events);
		ImGui::Checkbox("Ignore Note Events", &safc_globals::cb_ignore_note_events);
		ImGui::Checkbox("Ignore Except Everything Specified", &safc_globals::cb_ignore_except_everythig_spec);
		ImGui::Checkbox("Multichannel Split", &safc_globals::cb_multichannel_split);
		ImGui::Checkbox("RSB Compression", &safc_globals::cb_rsb_compression);
		ImGui::Checkbox("Inplace Merge", &safc_globals::cb_implace_merge);
		ImGui::Checkbox("Apply Offset After PPQ Change", &safc_globals::cb_apply_offset_after_ppq_change);
		ImGui::Checkbox("Collapse Tracks into One", &safc_globals::cb_collapse_trk_into_1);
		ImGui::Checkbox("Allow SysEx Events", &safc_globals::cb_allow_sysex_events);
		
		ImGui::End();
	}

	if (!safc_globals::global_alert_queue.empty())
	{
		auto& alert = safc_globals::global_alert_queue.front();
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
			alert.type == safc_globals::alert_queue_item::alert_type::error ? ImVec4(0.5f, 0.2f, 0.2f, 1.0f) :
			alert.type == safc_globals::alert_queue_item::alert_type::warning ? ImVec4(0.5f, 0.5f, 0.2f, 1.0f) :
			ImVec4(0.2f, 0.5f, 0.5f, 1.0f)
		);

		ImGui::OpenPopup(alert.header.c_str());
		
		if (ImGui::BeginPopupModal(alert.header.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped("%s", alert.message.c_str());
			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				safc_globals::global_alert_queue.pop_front();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor();
	}
	
	std::lock_guard<std::recursive_mutex> locker(safc_globals::global_lock);
	
	// Causes segfault involving atomic_bool loading
	//if(SMRP.second->processing)
	//	ImGui::OpenPopup("Procesing Progress");
	
	if(ImGui::BeginPopupModal("Procesing Progress", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		// HELP ME...
		// IDFK What I'm doing
		//SetSMRP(GlobalMCTM->currently_processed[ID]);

		float progress = 0;
		std::string proc;
		ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), proc.c_str());
		ImGui::EndPopup();
	}
}