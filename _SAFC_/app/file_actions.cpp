#include "app_state.h"
#include "file_actions.h"
#include "dialogs.h"

void add_files(const std::vector<std::wstring>& filenames)
{
	if (global_window_handler)
		global_window_handler->disable_all_windows();

	for (int i = 0; i < filenames.size(); i++)
	{
		if (filenames[i].empty())
			continue;

		g_data.files.push_back(file_settings(filenames[i]));

		auto& lastFile = g_data.files.back();
		if (lastFile.is_midi)
		{
			if (global_window_handler)
				_WH_t<selectable_properted_list>("MAIN", "List")->safe_push_back_new_string(lastFile.appearance_filename);

			std::uint32_t counter = 0;

			lastFile.new_tempo = g_data.global_new_tempo;
			lastFile.offset_ticks = g_data.global_offset;
			lastFile.inplace_merge_enabled = g_data.inplace_merge_flag;
			lastFile.channels_split = g_data.channels_split;
			lastFile.rsb_compression = g_data.rsb_compression;
			lastFile.collapse_midi = g_data.collapse_midi;
			lastFile.apply_offset_after = g_data.apply_offset_after;

			for (int q = 0; q < g_data.files.size(); q++)
			{
				if (g_data[q].filename == lastFile.filename)
				{
					g_data[q].file_name_postfix = std::to_string(counter) + "_.mid";
					g_data[q].w_file_name_postfix = std::to_wstring(counter) + L"_.mid";
					counter++;
				}
			}
		}
		else
		{
			g_data.files.pop_back();
		}
	}

	g_data.set_global_ppqn();
	g_data.resolve_subdivision_problem_group_id_assign();
}

void on_add()
{
	if (gui_stop_requested())
		return;
	auto filenames = multiple_open_file_dialog(L"Select midi files");
	if (gui_stop_requested())
		return;

	worker_singleton<struct midi_file_list>::instance().push(
		[filenames = std::move(filenames)](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		add_files(filenames);
	});
}

void on_rem()
{
	worker_singleton<struct midi_file_list>::instance().push([]()
	{
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");
		for (auto id = ptr->selected_id.rbegin(); id != ptr->selected_id.rend(); ++id)
			g_data.remove_by_id(*id);

		ptr->remove_selected();

		global_window_handler->disable_all_windows();

		g_data.set_global_ppqn();
		g_data.resolve_subdivision_problem_group_id_assign();
	});
}

void on_rem_all()
{
	worker_singleton<struct midi_file_list>::instance().push([]()
	{
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");

		global_window_handler->disable_all_windows();

		while (g_data.files.size())
		{
			g_data.remove_by_id(0);
			ptr->safe_remove_string_by_id(0);
		}
	});
}

void on_submit_global_ppqn()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint16_t PPQN = (input_field_data.size()) ? stoi(input_field_data) : g_data.global_ppqn;

	g_data.set_global_ppqn(PPQN, true);
	global_window_handler->disable_window("PROMPT");

	//props_and_sets::open_file_properties(props_and_sets::current_id);
}

void on_global_ppqn()
{
	global_window_handler->throw_prompt(
		"New value will be assigned to every MIDI\n(in settings)",
		"Global PPQN",
		on_submit_global_ppqn,
		_Align::center,
		input_field::Type::NaturalNumbers,
		std::to_string(g_data.global_ppqn),
		5);
}

void on_submit_global_offset()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint32_t offset = (input_field_data.size()) ? std::stoi(input_field_data) : g_data.global_offset;

	g_data.set_global_offset(offset);
	global_window_handler->disable_window("PROMPT");
}

void on_global_offset()
{
	global_window_handler->throw_prompt(
		"Sets new global offset",
		"Global Offset",
		on_submit_global_offset,
		_Align::center,
		input_field::Type::WholeNumbers,
		std::to_string(g_data.global_offset),
		10);
}

void on_submit_global_tempo()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	float tempo = (input_field_data.size()) ? std::stof(input_field_data) : g_data.global_new_tempo;

	g_data.set_global_tempo(tempo);
	global_window_handler->disable_window("PROMPT");
}

void on_global_tempo()
{
	global_window_handler->throw_prompt("Sets specific tempo value to every MIDI\n(in settings)", "Global S. Tempo\0", on_submit_global_tempo, _Align::center, input_field::Type::FP_PositiveNumbers, std::to_string(g_data.global_new_tempo), 8);
}

void on_resolve()
{
	g_data.resolve_subdivision_problem_group_id_assign();
}

void on_rem_vol_maps()
{
	((volume_graph*)(*((*global_window_handler)["VM"]))["VM_PLC"])->plc_bb = nullptr;
	global_window_handler->disable_window("VM");

	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].volume_map = nullptr;
}

void on_rem_cats()
{
	global_window_handler->disable_window("CAT");
	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].key_map = nullptr;
}

void on_rem_pitch_maps()
{
	//global_window_handler->disable_window("CAT");
	throw_alert_warning("Currently pitch maps can not be created and/or deleted :D");
	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].pitch_bend_map = nullptr;
}

void on_rem_all_modules()
{
	on_rem_vol_maps();
	on_rem_cats();
}

