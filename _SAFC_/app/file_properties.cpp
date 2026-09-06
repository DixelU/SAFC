#include "app_state.h"
#include "file_properties.h"

namespace props_and_sets
{
std::string* PPQN = new std::string(""), * OFFSET = new std::string(""), * TEMPO = new std::string("");
int current_id = -1, cat_id = -1, vm_id = -1, pm_id = -1;
bool human_readible = true; // tf is this
single_midi_info_collector* smic_ptr = nullptr;
std::string csv_delim = ";";

void open_file_properties(int id)
{
	if (!(id < g_data.files.size() && id >= 0))
	{
		current_id = -1;
		return;
	}

	current_id = id;
	auto settings_window_ptr = (*global_window_handler)["SMPAS"];
	auto other_settings_window_ptr = (*global_window_handler)["OTHER_SETS"];

	((text_box*)((*settings_window_ptr)["FileName"]))->safe_string_replace("..." + g_data[id].appearance_filename);
	((input_field*)((*settings_window_ptr)["PPQN"]))->update_input_string();
	((input_field*)((*settings_window_ptr)["PPQN"]))->safe_string_replace(std::to_string((g_data[id].new_ppqn) ? g_data[id].new_ppqn : g_data[id].old_ppqn));
	((input_field*)((*settings_window_ptr)["TEMPO"]))->safe_string_replace(std::to_string(g_data[id].new_tempo));
	((input_field*)((*settings_window_ptr)["OFFSET"]))->safe_string_replace(std::to_string(g_data[id].offset_ticks));
	((input_field*)((*settings_window_ptr)["GROUPID"]))->safe_string_replace(std::to_string(g_data[id].group_id));

	((input_field*)((*settings_window_ptr)["SELECT_START"]))->safe_string_replace(std::to_string(g_data[id].selection_start));
	((input_field*)((*settings_window_ptr)["SELECT_LENGTH"]))->safe_string_replace(std::to_string(g_data[id].selection_length));

	((checkbox*)((*settings_window_ptr)["BOOL_REM_TRCKS"]))->state = g_data[id].bool_settings & _BoolSettings::remove_empty_tracks;
	((checkbox*)((*settings_window_ptr)["BOOL_REM_REM"]))->state = g_data[id].bool_settings & _BoolSettings::remove_remnants;

	((checkbox*)((*other_settings_window_ptr)["BOOL_PIANO_ONLY"]))->state = g_data[id].bool_settings & _BoolSettings::all_instruments_to_piano;
	((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_TEMPO"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_tempos;
	((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_PITCH"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_pitches;
	((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_NOTES"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_notes;
	((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_ALL_EX_TPS"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_ENABLE"]))->state = g_data[id].bool_settings & _BoolSettings::enable_important_filter;
	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_NOTES"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_notes;
	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_TEMPO"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_tempo;
	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_PITCH"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_pitch;
	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_PROGC"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_progc;
	((checkbox*)((*other_settings_window_ptr)["IMP_FLT_OTHER"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_other;

	((checkbox*)((*other_settings_window_ptr)["LEGACY_META_RSB_BEHAVIOR"]))->state = g_data[id].allow_legacy_rsb_meta_interaction;
	((checkbox*)((*other_settings_window_ptr)["ALLOW_SYSEX"]))->state = g_data[id].allow_sysex;
	((checkbox*)((*other_settings_window_ptr)["ENABLE_ZERO_VELOCITY"]))->state = g_data[id].enable_zero_velocity;

	((checkbox*)((*settings_window_ptr)["SPLIT_TRACKS"]))->state = g_data[id].channels_split;
	((checkbox*)((*settings_window_ptr)["RSB_COMPRESS"]))->state = g_data[id].rsb_compression;

	((checkbox*)((*settings_window_ptr)["INPLACE_MERGE"]))->state = g_data[id].inplace_merge_enabled;

	((checkbox*)((*settings_window_ptr)["COLLAPSE_MIDI"]))->state = g_data[id].collapse_midi;
	((checkbox*)((*settings_window_ptr)["APPLY_OFFSET_AFTER"]))->state = g_data[id].apply_offset_after;

	((text_box*)((*settings_window_ptr)["CONSTANT_PROPS"]))->safe_string_replace(
		"File size: " + std::to_string(g_data[id].filesize) + "b\n" +
		"Old PPQN: " + std::to_string(g_data[id].old_ppqn) + "\n" +
		"Track number (header info): " + std::to_string(g_data[id].old_track_number) + "\n" +
		"\"Remnant\" file postfix: " + g_data[id].file_name_postfix + "\n" +
		"Time map status: " + ((g_data[id].time_map.empty()) ? "Empty" : "Present")
	);

	global_window_handler->enable_window("SMPAS");
}

void on_apply_settings()
{
	if (current_id < 0 && current_id >= g_data.files.size())
	{
		throw_alert_warning("You cannot apply current settings to file with id " + std::to_string(current_id));
		return;
	}

	std::int32_t string_value;
	std::string current_string = "";
	auto settings_window = (*global_window_handler)["SMPAS"];
	auto other_settings_window = (*global_window_handler)["OTHER_SETS"];

	current_string = ((input_field*)(*settings_window)["PPQN"])->get_current_input("0");
	if (current_string.size())
	{
		string_value = std::stoi(current_string);
		if (string_value)
		{
			g_data[current_id].new_ppqn = string_value;
			g_data[current_id].ppqn_manually_set = true;
		}
		else
		{
			g_data[current_id].new_ppqn = g_data.global_ppqn;
			g_data[current_id].ppqn_manually_set = false;
		}
	}

	current_string = ((input_field*)(*settings_window)["TEMPO"])->get_current_input("0");
	if (current_string.size())
	{
		float F = stof(current_string);
		g_data[current_id].new_tempo = F;
	}

	current_string = ((input_field*)(*settings_window)["OFFSET"])->get_current_input("0");
	if (current_string.size())
	{
		string_value = stoll(current_string);
		g_data[current_id].offset_ticks = string_value;
	}

	current_string = ((input_field*)(*settings_window)["GROUPID"])->get_current_input("0");
	if (current_string.size())
	{
		string_value = stoi(current_string);
		if (string_value != g_data[current_id].group_id)
		{
			g_data[current_id].group_id = string_value;
			throw_alert_warning("Manual group_id editing might cause significant drop of processing perfomance!");
		}
	}

	current_string = ((input_field*)(*settings_window)["SELECT_START"])->get_current_input("0");
	if (current_string.size())
	{
		string_value = stoll(current_string);
		g_data[current_id].selection_start = string_value;
	}

	current_string = ((input_field*)(*settings_window)["SELECT_LENGTH"])->get_current_input("-1");
	if (current_string.size())
	{
		string_value = stoll(current_string);
		g_data[current_id].selection_length = string_value;
	}

	g_data[current_id].allow_legacy_rsb_meta_interaction = (((checkbox*)(*other_settings_window)["LEGACY_META_RSB_BEHAVIOR"])->state);

	if (g_data[current_id].allow_legacy_rsb_meta_interaction)
		std::cout << "WARNING: Legacy way of treating running status events can also allow major corruptions of midi structure!" << std::endl;

	g_data[current_id].set_bool_setting(_BoolSettings::remove_empty_tracks, (((checkbox*)(*settings_window)["BOOL_REM_TRCKS"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::remove_remnants, (((checkbox*)(*settings_window)["BOOL_REM_REM"])->state));

	g_data[current_id].set_bool_setting(_BoolSettings::all_instruments_to_piano, (((checkbox*)(*other_settings_window)["BOOL_PIANO_ONLY"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::ignore_tempos, (((checkbox*)(*other_settings_window)["BOOL_IGN_TEMPO"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::ignore_pitches, (((checkbox*)(*other_settings_window)["BOOL_IGN_PITCH"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::ignore_notes, (((checkbox*)(*other_settings_window)["BOOL_IGN_NOTES"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, (((checkbox*)(*other_settings_window)["BOOL_IGN_ALL_EX_TPS"])->state));

	g_data[current_id].set_bool_setting(_BoolSettings::enable_important_filter, (((checkbox*)(*other_settings_window)["IMP_FLT_ENABLE"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_notes, (((checkbox*)(*other_settings_window)["IMP_FLT_NOTES"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_tempo, (((checkbox*)(*other_settings_window)["IMP_FLT_TEMPO"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_pitch, (((checkbox*)(*other_settings_window)["IMP_FLT_PITCH"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_progc, (((checkbox*)(*other_settings_window)["IMP_FLT_PROGC"])->state));
	g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_other, (((checkbox*)(*other_settings_window)["IMP_FLT_OTHER"])->state));

	g_data[current_id].enable_zero_velocity = (((checkbox*)(*other_settings_window)["ENABLE_ZERO_VELOCITY"])->state);
	g_data[current_id].allow_sysex = (((checkbox*)(*other_settings_window)["ALLOW_SYSEX"])->state);

	g_data[current_id].rsb_compression = ((checkbox*)(*settings_window)["RSB_COMPRESS"])->state;
	g_data[current_id].channels_split = ((checkbox*)(*settings_window)["SPLIT_TRACKS"])->state;
	g_data[current_id].collapse_midi = ((checkbox*)(*settings_window)["COLLAPSE_MIDI"])->state;
	g_data[current_id].apply_offset_after = ((checkbox*)(*settings_window)["APPLY_OFFSET_AFTER"])->state;

	auto& inplaceMergeState = ((checkbox*)(*settings_window)["INPLACE_MERGE"])->state;

	inplaceMergeState &= !g_data[current_id].rsb_compression;
	g_data[current_id].inplace_merge_enabled = inplaceMergeState;
}

void on_apply_bs2a()
{
	if (current_id < 0 && current_id >= g_data.files.size())
	{
		throw_alert_warning("You cannot apply current settings to file with id " + std::to_string(current_id));
		return;
	}

	on_apply_settings();

	for (auto& settings : g_data.files)
	{
		default_bool_settings = settings.bool_settings = g_data[current_id].bool_settings;
		g_data.inplace_merge_flag = settings.inplace_merge_enabled = g_data[current_id].inplace_merge_enabled;
		settings.allow_legacy_rsb_meta_interaction = g_data[current_id].allow_legacy_rsb_meta_interaction;
		settings.collapse_midi = g_data[current_id].collapse_midi;
		settings.apply_offset_after = g_data[current_id].apply_offset_after;
		settings.allow_sysex = g_data[current_id].allow_sysex;
		settings.channels_split = g_data[current_id].channels_split;
		settings.rsb_compression = g_data[current_id].rsb_compression;
	}
}

namespace cut_and_transpose
{
std::uint8_t cut_max = 0, cut_min = 0;
std::int16_t transpose_value = 0;

void on_cat()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	if (!g_data[current_id].key_map)
		g_data[current_id].key_map = std::make_shared<::cut_and_transpose>(0, 127, 0);

	tool_ptr->piano_transform = g_data[current_id].key_map;
	tool_ptr->update_info();

	global_window_handler->enable_window("CAT");
}

void on_reset()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	tool_ptr->piano_transform->max_val = 255;
	tool_ptr->piano_transform->min_val = 0;
	tool_ptr->piano_transform->transpose_val = 0;

	tool_ptr->update_info();
}

void on_cdt128()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	tool_ptr->piano_transform->max_val = 127;
	tool_ptr->piano_transform->min_val = 0;
	tool_ptr->piano_transform->transpose_val = 0;

	tool_ptr->update_info();
}

void on_0_127_to_128_255()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	tool_ptr->piano_transform->max_val = 127;
	tool_ptr->piano_transform->min_val = 0;
	tool_ptr->piano_transform->transpose_val = 128;

	tool_ptr->update_info();
}

void on_copy()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	cut_max = tool_ptr->piano_transform->max_val;
	cut_min = tool_ptr->piano_transform->min_val;
	transpose_value = tool_ptr->piano_transform->transpose_val;
}

void on_paste()
{
	auto window = (*global_window_handler)["CAT"];
	auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

	tool_ptr->piano_transform->max_val = cut_max;
	tool_ptr->piano_transform->min_val = cut_min;
	tool_ptr->piano_transform->transpose_val = transpose_value;

	tool_ptr->update_info();
}

void on_delete()
{
	auto window = (*global_window_handler)["CAT"];
	((cut_and_transpose_piano*)((*window)["CAT_ITSELF"]))->piano_transform = nullptr;
	global_window_handler->disable_window("CAT");

	g_data[current_id].key_map = nullptr;
}
}

namespace volume_map
{
void on_vol_map()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	((button*)(*window)["VM_SETMODE"])->safe_string_replace("Single");

	auto degree_input = ((input_field*)(*window)["VM_DEGREE"]);
	degree_input->update_input_string("1");
	degree_input->current_string.clear();

	tool_ptr->active_setting = 0;
	tool_ptr->hovered = 0;
	tool_ptr->re_put_mode = 0;

	if (!g_data[current_id].volume_map)
		g_data[current_id].volume_map = std::make_shared<dixelu::polyline_converter<std::uint8_t, std::uint8_t>>();
	tool_ptr->plc_bb = g_data[current_id].volume_map;

	global_window_handler->enable_window("VM");
}

void on_degree_shape()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	if (tool_ptr->plc_bb)
	{
		auto degree_input = ((input_field*)(*window)["VM_DEGREE"]);
		float degree = std::stof(degree_input->get_current_input("0"));

		tool_ptr->plc_bb->clear();
		tool_ptr->plc_bb->insert(127, 127);

		for (int i = 0; i < 128; i++)
			tool_ptr->plc_bb->insert(i, std::ceil(std::pow(i / 127., degree) * 127.));
	}
	else
		throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
}

void on_simplify()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	if (tool_ptr->plc_bb)
		tool_ptr->make_map_more_simple();
	else
		throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
}

void on_trace()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	if (tool_ptr->plc_bb)
	{
		if (tool_ptr->plc_bb->empty())
			return;

		std::uint8_t values_array[256]{};

		for (int i = 0; i < 255; i++)
			values_array[i] = tool_ptr->plc_bb->evaluate_as<std::uint8_t>(
				static_cast<std::uint8_t>(i), dixelu::polyline_extrapolation::linear)
			.value_or(static_cast<std::uint8_t>(i));

		for (int i = 0; i < 255; i++)
			tool_ptr->plc_bb->insert(static_cast<std::uint8_t>(i), values_array[i]);
	}
	else
		throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
}

void on_set_mode_change()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	if (tool_ptr->plc_bb)
	{
		tool_ptr->re_put_mode = !tool_ptr->re_put_mode;
		((button*)(*window)["VM_SETMODE"])->safe_string_replace(((tool_ptr->re_put_mode) ? "Double" : "Single"));
	}
	else
		throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
}

void on_erase()
{
	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

	if (tool_ptr->plc_bb)
		tool_ptr->plc_bb->clear();
	else
		throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
}

void on_delete()
{
	if (g_data[current_id].volume_map)
		g_data[current_id].volume_map = nullptr;

	auto window = (*global_window_handler)["VM"];
	auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);
	tool_ptr->plc_bb = nullptr;

	global_window_handler->disable_window("VM");
}
}

void on_pitch_map()
{
	// throw_alert_error("Having hard time thinking of how to implement it...\nNot available... yet..."); // will be never availible
}
}

