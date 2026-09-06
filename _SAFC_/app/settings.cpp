#include "app_state.h"
#include "settings.h"

namespace settings
{
std::int32_t background_id = 0;
WinReg::RegKey regestry_access;

void on_settings()
{
	global_window_handler->enable_window("APP_SETTINGS");//g_data.detected_threads

	auto app_settings_window = (*global_window_handler)["APP_SETTINGS"];
	((input_field*)(*app_settings_window)["AS_BCKGID"])->update_input_string(std::to_string(background_id));
	((input_field*)(*app_settings_window)["AS_ROT_ANGLE"])->update_input_string(std::to_string(dumb_rotation_angle));
	((input_field*)(*app_settings_window)["AS_THREADS_COUNT"])->update_input_string(std::to_string(g_data.detected_threads));

	((checkbox*)((*app_settings_window)["BOOL_REM_TRCKS"]))->state = default_bool_settings & _BoolSettings::remove_empty_tracks;
	((checkbox*)((*app_settings_window)["BOOL_REM_REM"]))->state = default_bool_settings & _BoolSettings::remove_remnants;
	((checkbox*)((*app_settings_window)["BOOL_PIANO_ONLY"]))->state = default_bool_settings & _BoolSettings::all_instruments_to_piano;
	((checkbox*)((*app_settings_window)["BOOL_IGN_TEMPO"]))->state = default_bool_settings & _BoolSettings::ignore_tempos;
	((checkbox*)((*app_settings_window)["BOOL_IGN_PITCH"]))->state = default_bool_settings & _BoolSettings::ignore_pitches;
	((checkbox*)((*app_settings_window)["BOOL_IGN_NOTES"]))->state = default_bool_settings & _BoolSettings::ignore_notes;
	((checkbox*)((*app_settings_window)["BOOL_IGN_ALL_EX_TPS"]))->state = default_bool_settings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

	((checkbox*)((*app_settings_window)["SPLIT_TRACKS"]))->state = g_data.channels_split;
	((checkbox*)((*app_settings_window)["RSB_COMPRESS"]))->state = g_data.rsb_compression;
	((checkbox*)((*app_settings_window)["COLLAPSE_MIDI"]))->state = g_data.collapse_midi;
	((checkbox*)((*app_settings_window)["APPLY_OFFSET_AFTER"]))->state = g_data.apply_offset_after;

	((checkbox*)((*app_settings_window)["INPLACE_MERGE"]))->state = g_data.inplace_merge_flag;
	((checkbox*)((*app_settings_window)["AUTOUPDATECHECK"]))->state = check_autoupdates;
}

void on_set_apply()
{
	bool registry_opened = false;
	try
	{
		settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
		registry_opened = true;
	}
	catch (...)
	{
		std::cout << "RK opening failed\n";
	}

	auto pptr = (*global_window_handler)["APP_SETTINGS"];
	std::string input_field_string;

	input_field_string = ((input_field*)(*pptr)["AS_BCKGID"])->get_current_input("0");
	std::cout << "AS_BCKGID " << input_field_string << std::endl;
	if (input_field_string.size())
	{
		background_id = std::stoi(input_field_string);
		if (registry_opened) TRY_CATCH(regestry_access.SetDwordValue(L"AS_BCKGID", background_id);, "Failed on setting AS_BCKGID")
	}
	std::cout << background_id << std::endl;

	input_field_string = ((input_field*)(*pptr)["AS_ROT_ANGLE"])->get_current_input("0");
	std::cout << "ROT_ANGLE " << input_field_string << std::endl;
	if (input_field_string.size() && !is_fonted)
		dumb_rotation_angle = stof(input_field_string);
	std::cout << dumb_rotation_angle << std::endl;

	input_field_string = ((input_field*)(*pptr)["AS_THREADS_COUNT"])->get_current_input(std::to_string(g_data.detected_threads));
	std::cout << "AS_THREADS_COUNT " << input_field_string << std::endl;
	if (input_field_string.size())
	{
		g_data.detected_threads = stoi(input_field_string);
		g_data.resolve_subdivision_problem_group_id_assign();
		if (registry_opened) TRY_CATCH(regestry_access.SetDwordValue(L"AS_THREADS_COUNT", g_data.detected_threads);, "Failed on setting AS_THREADS_COUNT")
	}
	std::cout << g_data.detected_threads << std::endl;

	default_bool_settings = (default_bool_settings & (~_BoolSettings::remove_empty_tracks)) | (_BoolSettings::remove_empty_tracks * (!!((checkbox*)(*pptr)["BOOL_REM_TRCKS"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::remove_remnants)) | (_BoolSettings::remove_remnants * (!!((checkbox*)(*pptr)["BOOL_REM_REM"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::all_instruments_to_piano)) | (_BoolSettings::all_instruments_to_piano * (!!((checkbox*)(*pptr)["BOOL_PIANO_ONLY"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_tempos)) | (_BoolSettings::ignore_tempos * (!!((checkbox*)(*pptr)["BOOL_IGN_TEMPO"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_pitches)) | (_BoolSettings::ignore_pitches * (!!((checkbox*)(*pptr)["BOOL_IGN_PITCH"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_notes)) | (_BoolSettings::ignore_notes * (!!((checkbox*)(*pptr)["BOOL_IGN_NOTES"])->state));
	default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_all_but_tempos_notes_and_pitch)) | (_BoolSettings::ignore_all_but_tempos_notes_and_pitch * (!!((checkbox*)(*pptr)["BOOL_IGN_ALL_EX_TPS"])->state));

	check_autoupdates = ((checkbox*)(*pptr)["AUTOUPDATECHECK"])->state;

	g_data.channels_split = ((checkbox*)((*pptr)["SPLIT_TRACKS"]))->state;
	g_data.rsb_compression = ((checkbox*)((*pptr)["RSB_COMPRESS"]))->state;

	g_data.collapse_midi = ((checkbox*)((*pptr)["COLLAPSE_MIDI"]))->state;
	g_data.apply_offset_after = ((checkbox*)((*pptr)["APPLY_OFFSET_AFTER"]))->state;

	if (registry_opened)
	{
		TRY_CATCH(regestry_access.SetDwordValue(L"AUTOUPDATECHECK", check_autoupdates); , "Failed on setting AUTOUPDATECHECK")
			TRY_CATCH(regestry_access.SetDwordValue(L"SPLIT_TRACKS", g_data.channels_split);, "Failed on setting SPLIT_TRACKS")
			TRY_CATCH(regestry_access.SetDwordValue(L"COLLAPSE_MIDI", g_data.collapse_midi); , "Failed on setting COLLAPSE_MIDI")
			TRY_CATCH(regestry_access.SetDwordValue(L"APPLY_OFFSET_AFTER", g_data.collapse_midi); , "Failed on setting APPLY_OFFSET_AFTER")
			//TRY_CATCH(regestry_access.SetDwordValue(L"RSB_COMPRESS", check_autoupdates);, "Failed on setting RSB_COMPRESS")
			TRY_CATCH(regestry_access.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", default_bool_settings); , "Failed on setting DEFAULT_BOOL_SETTINGS")
			TRY_CATCH(regestry_access.SetDwordValue(L"FONTSIZE_POST1P4", lfont_symbols_info::font_size);, "Failed on setting FONTSIZE_POST1P4")
			TRY_CATCH(regestry_access.SetDwordValue(L"FLOAT_FONTHTW_POST1P4", *(std::uint32_t*)(&font_height_to_width));, "Failed on setting FLOAT_FONTHTW_POST1P4")
	}

	g_data.inplace_merge_flag = (((checkbox*)(*pptr)["INPLACE_MERGE"])->state);
	if (registry_opened)
		TRY_CATCH(regestry_access.SetDwordValue(L"AS_INPLACE_FLAG", g_data.inplace_merge_flag); , "Failed on setting AS_INPLACE_FLAG")

		((input_field*)(*pptr)["AS_FONT_NAME"])->put_into_source();
	std::wstring ws(default_font_name.begin(), default_font_name.end());
	if (registry_opened)
		TRY_CATCH(regestry_access.SetStringValue(L"COLLAPSEDFONTNAME_POST1P4", ws);, "Failed on setting COLLAPSEDFONTNAME_POST1P4")

		settings::regestry_access.Close();
}

void change_is_fonted_var()
{
	is_fonted = !is_fonted;

	set_is_fonted_var(is_fonted);
	exit(0);
}

void apply_to_all()
{
	on_set_apply();

	for (auto& settings : g_data.files)
	{
		settings.bool_settings = default_bool_settings;
		settings.inplace_merge_enabled = g_data.inplace_merge_flag && !g_data.channels_split;
		settings.channels_split = g_data.channels_split;
		settings.rsb_compression = g_data.rsb_compression;
		settings.collapse_midi = g_data.collapse_midi;
		settings.apply_offset_after = g_data.apply_offset_after;
	}
}

void apply_fs_wheel(double new_val)
{
	lfont_symbols_info::font_size = new_val;
	lfont_symbols_info::initialise_font(default_font_name);
}

void apply_rel_wheel(double new_val)
{
	font_height_to_width = new_val;
	lfont_symbols_info::initialise_font(default_font_name);
}

void feedback_open()
{
	global_window_handler->enable_window("SUPPORT");
}
}

void restore_reg_settings()
{
	bool reg_open = false;
	try
	{
		settings::regestry_access.Create(HKEY_CURRENT_USER, default_reg_path);
	}
	catch (...) { std::cout << "Exception thrown while creating registry key\n"; }
	try
	{
		settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
		reg_open = true;
	}
	catch (...) { std::cout << "Exception thrown while opening RK\n"; }

	if (!reg_open)
		return;

	try
	{
		settings::background_id = settings::regestry_access.GetDwordValue(L"AS_BCKGID");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_BCKGID from registry\n"; }
	try
	{
		check_autoupdates = settings::regestry_access.GetDwordValue(L"AUTOUPDATECHECK");
	}
	catch (...) { std::cout << "Exception thrown while restoring AUTOUPDATECHECK from registry\n"; }
	try
	{
		is_fonted = settings::regestry_access.GetDwordValue(L"FONTS_ENABLED_POST1P4");
	}
	catch (...) { std::cout << "Exception thrown while restoring FONTS_ENABLED from registry\n"; }
	try
	{
		g_data.channels_split = settings::regestry_access.GetDwordValue(L"SPLIT_TRACKS");
	}
	catch (...) { std::cout << "Exception thrown while restoring SPLIT_TRACKS from registry\n"; }
	try
	{
		g_data.collapse_midi = settings::regestry_access.GetDwordValue(L"COLLAPSE_MIDI");
	}
	catch (...) { std::cout << "Exception thrown while restoring COLLAPSE_MIDI from registry\n"; }
	try
	{
		g_data.apply_offset_after = settings::regestry_access.GetDwordValue(L"APPLY_OFFSET_AFTER");
	}
	catch (...) { std::cout << "Exception thrown while restoring APPLY_OFFSET_AFTER from registry\n"; }
	try
	{
		g_data.detected_threads = settings::regestry_access.GetDwordValue(L"AS_THREADS_COUNT");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_THREADS_COUNT from registry\n"; }
	try
	{
		default_bool_settings = settings::regestry_access.GetDwordValue(L"DEFAULT_BOOL_SETTINGS");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_INPLACE_FLAG from registry\n"; }
	try
	{
		g_data.inplace_merge_flag = settings::regestry_access.GetDwordValue(L"AS_INPLACE_FLAG");
	}
	catch (...) { std::cout << "Exception thrown while restoring INPLACE_MERGE from registry\n"; }
	try
	{
		std::wstring ws = settings::regestry_access.GetStringValue(L"COLLAPSEDFONTNAME_POST1P4");//COLLAPSEDFONTNAME
		default_font_name.resize(ws.size());
		std::transform(ws.begin(), ws.end(), default_font_name.begin(), [](wchar_t c) { return static_cast<char>(c); });
	}
	catch (...) { std::cout << "Exception thrown while restoring COLLAPSEDFONTNAME_POST1P4 from registry\n"; }
	try
	{
		lfont_symbols_info::font_size = settings::regestry_access.GetDwordValue(L"FONTSIZE_POST1P4");
	}
	catch (...) { std::cout << "Exception thrown while restoring FONTSIZE from registry\n"; }
	try
	{
		std::uint32_t B = settings::regestry_access.GetDwordValue(L"FLOAT_FONTHTW_POST1P4");
		font_height_to_width = *(float*)&B;
	}
	catch (...) { std::cout << "Exception thrown while restoring FLOAT_FONTHTW from registry\n"; }
	try
	{
		saved_midi_device_name = settings::regestry_access.GetStringValue(L"MIDI_DEVICE_NAME");
	}
	catch (...) { std::cout << "Exception thrown while restoring MIDI_DEVICE_NAME from registry\n"; }
	try
	{
		saved_syncore_bank_path = settings::regestry_access.GetStringValue(L"SYNCORE_BANK_PATH");
	}
	catch (...) { std::cout << "No saved SYNCore bank path\n"; }
	try
	{
		const auto value = settings::regestry_access.GetDwordValue(L"SYNCORE_SAMPLE_RATE");
		if (value >= 8000 && value <= 192000)
			saved_syncore_preferences.sample_rate = value;
	}
	catch (...) {}
	try
	{
		const auto value = settings::regestry_access.GetDwordValue(L"SYNCORE_BUFFER_FRAMES");
		if (value >= 256 && value <= 1048576)
			saved_syncore_preferences.buffer_frames = value;
	}
	catch (...) {}
	try
	{
		const auto value = settings::regestry_access.GetDwordValue(L"SYNCORE_MAX_COHORTS");
		if (value >= 1 && value <= 1048576)
			saved_syncore_preferences.maximum_cohorts = value;
	}
	catch (...) {}
	try
	{
		const auto value = settings::regestry_access.GetDwordValue(L"SYNCORE_RENDER_THREADS");
		if (value <= 64)
			saved_syncore_preferences.render_threads = value;
	}
	catch (...) {}
	try
	{
		const auto value = settings::regestry_access.GetDwordValue(L"SYNCORE_PHASE_MODE");
		if (value <= static_cast<std::uint32_t>(syncore_phase_mode::independent_bins))
			saved_syncore_preferences.phase_mode = static_cast<syncore_phase_mode>(value);
	}
	catch (...) {}
	try
	{
		const auto value = std::stod(settings::regestry_access.GetStringValue(L"SYNCORE_GAIN_DB"));
		if (value >= -60.0 && value <= 12.0)
			saved_syncore_preferences.output_gain_db = value;
	}
	catch (...) {}
	try
	{
		saved_syncore_preferences.limiter_enabled =
			settings::regestry_access.GetDwordValue(L"SYNCORE_LIMITER") != 0;
	}
	catch (...) {}
	load_player_video_render_settings();
	if (player)
	{
		player->set_syncore_bank_path(saved_syncore_bank_path);
		player->set_syncore_preferences(saved_syncore_preferences);
	}

	settings::regestry_access.Close();
}

void on_other_settings()
{
	global_window_handler->enable_window("OTHER_SETS");
}

