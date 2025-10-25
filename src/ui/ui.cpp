#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <format>

#include <imgui.h>
#include <log_c/log.h>

#include "ui.hpp"

#include "../core/single_midi_processor_2.h"
#include "../core/midi_collection_merger.h"
#include "../core/background_worker.h"

#include "../ui/ofd.h"

static ImGuiSelectionBasicStorage midi_selection;
static ImGuiMultiSelectIO* ms_io;

static int midi_settings_modes = 0;
static char it_ppq[256] = "1920";
static char it_offset[380] = "0";
static char it_tempo[380] = "120";

static bool temp = false;

static std::shared_ptr<midi_collection_threaded_merger> current_merger;

void ThrowAlert_Error(std::string&& AlertText)
{
	safc_globals::alert_queue.push_back({ std::move(AlertText), "Error", safc_globals::alert_queue_item::alert_type::error });
}

void ThrowAlert_Warning(std::string&& AlertText)
{
	safc_globals::alert_queue.push_back({ std::move(AlertText), "Warning", safc_globals::alert_queue_item::alert_type::warning });
}

void ThrowAlert_Info(std::string&& AlertText)
{
	safc_globals::alert_queue.push_back({ std::move(AlertText), "Info", safc_globals::alert_queue_item::alert_type::info });
}

void AddFiles(const std::vector<std_unicode_string>& filenames) 
{
	std::unique_lock lock(safc_globals::global_lock);

	for (auto& file : filenames)
	{
		midi_file_meta meta;
		meta.set(file);

		log_info("File: %s", meta.visible_path.c_str());

		safc_globals::midi_list.emplace_back(std::move(meta));
	}
}

void RenderMainWindow()
{
	std::unique_lock lock(safc_globals::global_lock);

	ImGui::SetNextWindowSizeConstraints(ImVec2(700, 700), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_MenuBar);

	if(ImGui::BeginMenuBar())
	{
		if(ImGui::BeginMenu("File"))
		{
			if(ImGui::MenuItem("Add MIDIs"))
			{
				worker_singleton<struct file_meta_scanner>::instance().push(
					[](){ AddFiles(OpenFileDlg(to_cchar_t("Add MIDI Files"))); });
				
				log_info("add midi");
			}
			
			if(ImGui::MenuItem("Remove MIDIs"))
			{
				std::vector<int> selected_indices;
				for (int i = 0; i < safc_globals::midi_list.size(); ++i)
				{
					if (midi_selection.Contains((ImGuiID)i))
						selected_indices.push_back(i);
				}

				worker_singleton<struct file_meta_scanner>::instance().push(
					[selected_indices = std::move(selected_indices)]() mutable 
					{
						// Erase in reverse order to avoid shifting indices
						std::sort(selected_indices.rbegin(), selected_indices.rend());
						for (int idx : selected_indices)
							safc_globals::midi_list.erase(safc_globals::midi_list.begin() + idx);

						// call thread id reassign procedure here
					});
				
				midi_selection.Clear();
			}
			
			// todo: automatic save filename generation
			if(ImGui::MenuItem("Save & Merge"))
			{
				std_unicode_string save_to = SaveFileDlg(to_cchar_t("Save file"));

				if(!save_to.empty())
				{
					safc_globals::save_file = std::move(save_to);
					safc_globals::visible_save_file = 
						std::string(
							safc_globals::save_file.begin(),
							safc_globals::save_file.end()); // wstring to string conversion for windows

					log_info("Save file: '%s'", safc_globals::visible_save_file.c_str()); // todo: log visible filename only

					std::vector<midi_file_meta::processing_data_ptr_t> data;
					for (auto & el: safc_globals::midi_list)
						data.push_back(el.processing_data);

					current_merger = 
						std::make_shared<midi_collection_threaded_merger>(
							data,
							safc_globals::global_ppq,
							safc_globals::save_file,
							false /* if headless -> set to true */);

					worker_singleton<struct merge_watcher>::instance().push([local_merger = current_merger]
					{
						local_merger->start_processing_midis();

						while (local_merger->check_smrp_processing_and_start_next_step())
							std::this_thread::sleep_for(std::chrono::milliseconds(100));

						while (!local_merger->check_ri_merge())
							std::this_thread::sleep_for(std::chrono::milliseconds(66));
					});
				}
				else
				{
					log_trace("Save canceled");
				}
			}
			
			ImGui::Separator();
			
			if(ImGui::MenuItem("Clear MIDI List"))
			{
				safc_globals::midi_list.clear();
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
		clipper.Begin(safc_globals::midi_list.size());
		if(ms_io->RangeSrcItem != -1)
			clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem); // Ensure RangeSrc item is not clipped.
		
		while(clipper.Step())
		{
			for(int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++)
			{
				ImGui::PushID(n);
				bool item_is_selected = midi_selection.Contains((ImGuiID)n);
				ImGui::SetNextItemSelectionUserData(n);
				ImGui::Selectable(safc_globals::midi_list[n].visible_name.c_str(), item_is_selected);
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

	if (!safc_globals::alert_queue.empty())
	{
		auto& alert = safc_globals::alert_queue.front();
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
				safc_globals::alert_queue.pop_front();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor();
	}

	if (current_merger == nullptr)
		return;

	ImGui::OpenPopup("Procesing Progress");
	
	if(ImGui::BeginPopupModal("Procesing Progress", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		for (auto& single_thread_data : current_merger->currently_processing)
		{
			auto thread_data_copy = single_thread_data;
			if (!thread_data_copy.first || !thread_data_copy.second)
			{
				ImGui::Text("Starting thread ...");
				ImGui::Separator();
				continue;
			}

			if (thread_data_copy.second->finished)
			{
				ImGui::Separator();
				continue;
			}

			auto progress = float(thread_data_copy.second->last_input_position) / thread_data_copy.first->settings.details.initial_filesize;
			auto progress_bar_string = std::format("{:.3}%", progress * 100);
			auto last_text = thread_data_copy.second->log->get_last();
			auto last_warning = thread_data_copy.second->warning->get_last();
			auto last_error = thread_data_copy.second->error->get_last();

			ImGui::Text("%s", thread_data_copy.first->visible_filename.c_str());
			ImGui::SameLine();
			ImGui::ProgressBar(0.5f, {}, progress_bar_string.c_str());
			ImGui::Text("%s", last_text.c_str());
			if (!last_warning.empty())
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%s", last_warning.c_str());
			if (!last_error.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", last_error.c_str());
			ImGui::Separator();
		}
	}
}