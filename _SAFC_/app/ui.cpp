#include "app_state.h"
#include "ui.h"
#include "update.h"
#include "file_actions.h"
#include "file_properties.h"
#include "settings.h"
#include "merger.h"
#include "player_controls.h"
#include "syncore_settings.h"
#include "playback_source.h"
#include "editor.h"
#include "window_layout.h"

void init(bool reinitialise_font)
{
	if (reinitialise_font)
		lfont_symbols_info::initialise_font(default_font_name, true);

	g_data.detected_threads =
		std::max(
			std::min((std::uint16_t)(
				(std::uint16_t)std::max(
					std::thread::hardware_concurrency(),
					1u
				)
				- 1
				),
				(std::uint16_t)(ceil(get_available_memory() / 2048))
			), (std::uint16_t)1
		);

	global_window_handler = std::make_shared<windows_handler>();

	const auto& [maj, min, ver, build] = g_version_tuple;

	constexpr unsigned BACKGROUND = 0x070E16AF;
	constexpr unsigned BACKGROUND_OPQ = 0x070E16DF;
	constexpr unsigned BORDER = 0xFFFFFF7F;
	constexpr unsigned HEADER = 0x285685CF;

	/*selectable_properted_list* SPL = new selectable_properted_list(bs_list_black_small, NULL, props_and_sets::open_file_properties, -50, 172, 300, 12, 65, 30);
	moveable_window* window = new MoveableResizeableWindow(Mainwindow_name.str(), system_white, -200, 200, 400, 400, 0x3F3F3FAF, 0x7F7F7F7F, 0, [SPL](float dH, float dW, float NewHeight, float NewWidth) {
		constexpr float TopMargin = 200 - 172;
		constexpr float BottomMargin = 12;
		float SPLNewHight = (NewHeight - TopMargin - BottomMargin);
		float SPLNewWidth = SPL->width + dW;
		SPL->safe_resize(SPLNewHight, SPLNewWidth);
		});
	((MoveableResizeableWindow*)window)->assign_min_dimensions(300, 300);
	((MoveableResizeableWindow*)window)->assign_pinned_activities({
		"ADD_Butt", "REM_Butt", "REM_ALL_Butt", "GLOBAL_PPQN_Butt", "GLOBAL_OFFSET_Butt", "GLOBAL_TEMPO_Butt", "DELETE_ALL_VM", "DELETE_ALL_CAT", "DELETE_ALL_PITCHES",
		"DELETE_ALL_MODULES", "settings", "SAVE_AS", "START"
		}, MoveableResizeableWindow::PinSide::right);
	((MoveableResizeableWindow*)window)->assign_pinned_activities({ "settings", "SAVE_AS", "START" }, MoveableResizeableWindow::PinSide::bottom);*/
	moveable_window* window = new moveable_fui_window(std::format("SAFC v{}.{}.{}.{}", maj, min, ver, build), system_white, -200, 197.5f, 400, 397.5f, 300, 2.5f, 100, 100, 5, BACKGROUND, HEADER, BORDER);

	button* button_buff;
	(*window)["List"] = new selectable_properted_list(bs_list_black_small, NULL, props_and_sets::open_file_properties, -45, 172, 295, 12, 64, 30);;

	(*window)["ADD_Butt"] = new button("Add MIDIs", system_white, on_add, 150, 167.5, 75, 12, 1, 0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["REM_Butt"] = new button("Remove selected", system_white, on_rem, 150, 155, 75, 12, 1, 0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["REM_ALL_Butt"] = new button("Remove all", system_white, on_rem_all, 150, 142.5, 75, 12, 1, 0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0xF7F7F7FF, &system_white, "May cause lag");

	(*window)["OPEN_TOOLS"] = new button("Tools...", system_black, []()
	{
		global_window_handler->enable_window("TOOLS");
	}, 150, 117.5, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, " ");

	(*window)["GLOBAL_PPQN_Butt"] = new button("Global PPQN", system_white, on_global_ppqn, 150, 92.5, 75, 12, 1, 0xFF3F00AF, 0xFFFFFFFF, 0xFF3F00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["GLOBAL_OFFSET_Butt"] = new button("Global offset", system_white, on_global_offset, 150, 80, 75, 12, 1, 0xFF7F00AF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["GLOBAL_TEMPO_Butt"] = new button("Global tempo", system_white, on_global_tempo, 150, 67.5, 75, 12, 1, 0xFFAF00AF, 0xFFFFFFFF, 0xFFAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*window)["DELETE_ALL_VM"] = new button("Remove vol. maps", system_white, on_rem_vol_maps, 150, 42.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//0xFF007FAF
	(*window)["DELETE_ALL_CAT"] = new button("Remove C&Ts", system_white, on_rem_cats, 150, 30, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["DELETE_ALL_MODULES"] = new button("Remove modules", system_white, on_rem_all_modules, 150, 17.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*window)["APP_SETTINGS"] = new button("Settings...", system_white, settings::on_settings, 150, -140, 75, 12, 1,
		0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["SAVE_AS"] = new button("Save as...", system_white, on_save_to, 150, -152.5, 75, 12, 1,
		0x3FAF00AF, 0xFFFFFFFF, 0x3FAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["START"] = button_buff = new button("Start merging", system_white, on_start, 150, -177.5, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//177.5

	(*global_window_handler)["MAIN"] = window;

	window = new moveable_fui_window("MIDI utilities", system_white, 65, 140, 130, 70, 90, 2.5f, 10, 10, 3, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["OPEN_PLAYER"] = new button(
		"On-drive MIDI Player", system_black, []()
	{
		global_window_handler->disable_window("TOOLS");
		if (!compressed_player_preparing.load(std::memory_order_acquire))
			compressed_player_status(
				"Choose or drag&drop a MIDI or XZ/ZIP/7z archive");
		global_window_handler->enable_window("ARCHIVE_SOURCE");
	},
		130, 105, 100, 12, 1,
		0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF,
		nullptr, " ");
	(*window)["OPEN_MIDI_EDITOR"] = new button("Piano roll editor", system_black, []()
	{
		global_window_handler->main_window_id = "MIDI_EDITOR";
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("MIDI_EDITOR");
	}, 130, 92.5f, 100, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, " ");

	(*global_window_handler)["TOOLS"] = window;

	window = new moveable_fui_window("Props. and sets.", system_white, -100, 100, 200, 225, 100, 2.5f, 75, 50, 3, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["FileName"] = new text_box("_", system_white, 0, 88.5 - moveable_window::window_header_size, 6, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 0, _Align::left, text_box::VerticalOverflow::cut);
	(*window)["PPQN"] = new input_field(" ", -90 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 25, system_white, props_and_sets::PPQN, 0x007FFFFF, &system_white, "PPQN is lesser than 65536.", 5, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["TEMPO"] = new input_field(" ", -50 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 45, system_white, props_and_sets::TEMPO, 0x007FFFFF, &system_white, "Specific tempo override field", 8, _Align::center, _Align::left, input_field::Type::FP_PositiveNumbers);
	(*window)["OFFSET"] = new input_field(" ", 20 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 55, system_white, props_and_sets::OFFSET, 0x007FFFFF, &system_white, "Offset from begining in ticks", 10, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*window)["APPLY_OFFSET_AFTER"] = new checkbox(12.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::center, "Apply offset after PPQ change");

	(*window)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*window)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*window)["OTHER_CHECKBOXES"] = new button("Other settings", system_white, on_other_settings, -37.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Other MIDI processing settings");

	(*window)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*window)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Enable RSB compression");

	(*window)["COLLAPSE_MIDI"] = new checkbox(97.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse all tracks of a MIDI into one");

	(*window)["BOOL_APPLY_TO_ALL_MIDIS"] = button_buff = new button("A2A", system_white, props_and_sets::on_apply_bs2a, 80 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Sets \"bool settings\" to all midis");
	button_buff->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Enables/Disables inplace merge");

	(*window)["GROUPID"] = new input_field(" ", 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, props_and_sets::PPQN, 0x007FFFFF, &system_white, "Group id...", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["MIDIINFO"] = button_buff = new button("Collect info", system_white, props_and_sets::initialize_collecting, 20, 15 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Collects additional info about the midi");
	button_buff->tip->safe_move(-20, 0);

	(*window)["APPLY"] = new button("Apply", system_white, props_and_sets::on_apply_settings, 87.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 30, 10, 1, 0x7FAFFF3F, 0xFFFFFFFF, 0xFFAF7FFF, 0xFFAF7F3F, 0xFFAF7FFF, nullptr, " ");

	(*window)["CUT_AND_TRANSPOSE"] = (button_buff = new button("Cut & Transpose...", system_white, props_and_sets::cut_and_transpose::on_cat, 45 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 85, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Cut and Transpose tool"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);
	(*window)["PITCH_MAP"] = (button_buff = new button("Pitch map ...", system_white, props_and_sets::on_pitch_map, -37.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 70, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, &system_white, "Allows to transform pitches"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);
	(*window)["VOLUME_MAP"] = (button_buff = new button("Volume map ...", system_white, props_and_sets::volume_map::on_vol_map, -37.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Allows to transform volumes of notes"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["SELECT_START"] = new input_field(" ", -37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection start", 13, _Align::center, _Align::right, input_field::Type::NaturalNumbers);
	(*window)["SELECT_LENGTH"] = new input_field(" ", 37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection length", 14, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*window)["CONSTANT_PROPS"] = new text_box("_Props text example_", system_white, 0, -55 - moveable_window::window_header_size, 80 - moveable_window::window_header_size, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 1);

	(*global_window_handler)["SMPAS"] = window;//Selected midi properties and settings

	window = new moveable_fui_window("Other settings.", system_white, -75, 35, 150, 65 + moveable_window::window_header_size, 100, 2.5f, 17.5f, 17.5f, 3, BACKGROUND_OPQ, HEADER, BORDER);

	checkbox* checkbox_buff = nullptr;

	(*window)["BOOL_PIANO_ONLY"] = new checkbox(-65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instruments to piano");
	(*window)["BOOL_IGN_TEMPO"] = new checkbox(-50, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove tempo events");
	(*window)["BOOL_IGN_PITCH"] = new checkbox(-35, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove pitch events");
	(*window)["BOOL_IGN_NOTES"] = new checkbox(-20, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Remove notes");
	(*window)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-5, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore other events");

	(*window)["LEGACY_META_RSB_BEHAVIOR"] = checkbox_buff = new checkbox(10, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, false, &system_white, _Align::center, "En. legacy RSB/Meta behavior");
	checkbox_buff->tip->safe_change_position_argumented(_Align::left, -70, 15 - moveable_window::window_header_size);
	(*window)["ALLOW_SYSEX"] = new checkbox(25, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Allow sysex events");

	(*window)["IMP_FLT_ENABLE"] = new checkbox(65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F1F7F, 0x5FFF007F, 1, 0, &system_white, _Align::right, "Enable important event filter");

	(*window)["0VERLAY"] = new text_box("", system_white, -10, 2.5f - moveable_window::window_header_size, 23, 120, 0, 0xFFFFFF1F, 0x007FFF7F, 1);

	(*window)["IMP_FLT_NOTES"] = new checkbox(-60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Important filter: piano");
	(*window)["IMP_FLT_TEMPO"] = new checkbox(-45, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: tempo");
	(*window)["IMP_FLT_PITCH"] = new checkbox(-30, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: pitch");
	(*window)["IMP_FLT_PROGC"] = checkbox_buff = new checkbox(-15, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: program change");
	checkbox_buff->tip->safe_change_position_argumented(_Align::left, -70, -5 - moveable_window::window_header_size);
	(*window)["IMP_FLT_OTHER"] = new checkbox(0, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: other");

	(*window)["ENABLE_ZERO_VELOCITY"] = new checkbox(60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0x00001F3F, 0x00FF00FF, 1, 0, &system_white, _Align::right, "\"Enable\" zero velocity notes");

	(*window)["APPLY"] = new button("Apply", system_white, props_and_sets::on_apply_settings, 70 - moveable_window::window_header_size, -20 - moveable_window::window_header_size, 30, 10, 1, 0x3F7FFF3F, 0xFFFFFFEF, 0x7FFF3FFF, 0x7FFF3F1F, 0x7FFF3FFF, nullptr, " ");

	(*global_window_handler)["OTHER_SETS"] = window; // Other settings

	window = new moveable_fui_window("Cut and Transpose.", system_white, -200, 50, 400, 100, 300, 2.5f, 15, 15, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["CAT_ITSELF"] = new cut_and_transpose_piano(0, 20 - moveable_window::window_header_size, 1, 10, nullptr);
	(*window)["CAT_SET_DEFAULT"] = new button("reset", system_white, props_and_sets::cut_and_transpose::on_reset, -150, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_+128"] = new button("0-127 -> 128-255", system_white, props_and_sets::cut_and_transpose::on_0_127_to_128_255, -85, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_CDT128"] = new button("Cut down to 128", system_white, props_and_sets::cut_and_transpose::on_cdt128, 0, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_COPY"] = new button("Copy", system_white, props_and_sets::cut_and_transpose::on_copy, 65, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_PASTE"] = new button("Paste", system_white, props_and_sets::cut_and_transpose::on_paste, 110, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_DELETE"] = new button("Delete", system_white, props_and_sets::cut_and_transpose::on_delete, 155, -10 - moveable_window::window_header_size, 40, 10, 1, 0xFF00FF3F, 0xFF00FFFF, 0xFFFFFFFF, 0xFF003F3F, 0xFF003FFF, nullptr, " ");

	(*global_window_handler)["CAT"] = window;

	window = new moveable_fui_window("Volume map.", system_white, -150, 150, 300, 350, 200, 2.5f, 100, 100, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["VM_PLC"] = new volume_graph(0, 0 - moveable_window::window_header_size, 300 - moveable_window::window_header_size * 2, 300 - moveable_window::window_header_size * 2, std::make_shared<dixelu::polyline_converter<std::uint8_t, std::uint8_t>>());///todo: interface
	(*window)["VM_SSBDIIF"] = button_buff = new button("Shape alike x^y", system_white, props_and_sets::volume_map::on_degree_shape, -110 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 80, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Where y is from frame bellow");///Set shape by degree in input field;
	button_buff->tip->safe_change_position_argumented(_Align::left, -150 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_DEGREE"] = new input_field("1", -140 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, nullptr, " ", 4, _Align::center, _Align::center, input_field::Type::FP_PositiveNumbers);
	(*window)["VM_ERASE"] = button_buff = new button("Erase points", system_white, props_and_sets::volume_map::on_erase, -35 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, nullptr, "_");
	(*window)["VM_DELETE"] = new button("Delete map", system_white, props_and_sets::volume_map::on_delete, 30 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFAFAF3F, 0xFFAFAFFF, 0xFFEFEFFF, 0xFF7F3F7F, 0xFF1F1FFF, nullptr, "_");
	(*window)["VM_SIMP"] = button_buff = new button("Simplify map", system_white, props_and_sets::volume_map::on_simplify, -70 - moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Reduces amount of \"repeating\" points");
	button_buff->tip->safe_change_position_argumented(_Align::left, -100 - moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_TRACE"] = button_buff = new button("Trace map", system_white, props_and_sets::volume_map::on_trace, -35 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, &system_white, "Puts every point onto map");
	button_buff->tip->safe_change_position_argumented(_Align::left, -65 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_SETMODE"] = button_buff = new button("Single", system_white, props_and_sets::volume_map::on_set_mode_change, 30 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 40, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, nullptr, "_");

	(*global_window_handler)["VM"] = window;

	window = new moveable_fui_window("App settings", system_white, -100, 110, 200, 230, 125, 2.5f, 50, 50, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);

	(*window)["AS_BCKGID"] = new input_field(std::to_string(settings::background_id), -35, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Background id", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["AS_GLOBALSETTINGS"] = new text_box("Global settings for new MIDIs", system_white, 0, 85 - moveable_window::window_header_size, 50, 200, 12, 0x007FFF1F, 0x007FFF7F, 1, _Align::center);
	(*window)["AS_APPLY"] = button_buff = new button("Apply", system_white, settings::on_set_apply, 85 - moveable_window::window_header_size, -87.5 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "_");
	(*window)["AS_EN_FONT"] = button_buff = new button((is_fonted) ? "Disable fonts" : "Enable fonts", system_white, settings::change_is_fonted_var, 72.5 - moveable_window::window_header_size, -67.5 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, " ");

	auto angle_input = new input_field(std::to_string(dumb_rotation_angle), -87.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Rotation angle", 6, _Align::center, _Align::left, input_field::Type::FP_Any);
	angle_input->disable(); // hide // legacy
	(*window)["AS_ROT_ANGLE"] = angle_input;
	(*window)["AS_FONT_SIZE"] = new wheel_variable_changer(settings::apply_fs_wheel, -37.5, -82.5, lfont_symbols_info::font_size, 1, system_white, "Font size", "Delta", wheel_variable_changer::Type::addition);
	(*window)["AS_FONT_P"] = new wheel_variable_changer(settings::apply_rel_wheel, -37.5, -22.5, font_height_to_width, 0.01, system_white, "Font rel.", "Delta", wheel_variable_changer::Type::addition);
	(*window)["AS_FONT_NAME"] = new input_field(default_font_name, 52.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 100, legacy_white, &default_font_name, 0x007FFFFF, &system_white, "Font name", 32, _Align::center, _Align::left, input_field::Type::text);

	(*window)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*window)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*window)["BOOL_PIANO_ONLY"] = new checkbox(-67.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instuments to piano");
	(*window)["BOOL_IGN_TEMPO"] = new checkbox(-52.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Ignore tempo events");
	(*window)["BOOL_IGN_PITCH"] = new checkbox(-37.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore pitch bending events");
	(*window)["BOOL_IGN_NOTES"] = new checkbox(-22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore note events");
	(*window)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore everything except specified");
	(*window)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*window)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Enable RSB compression");

	(*window)["ALLOW_SYSEX"] = new checkbox(-97.5 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::left, "Allow sysex events");

	(*window)["BOOL_APPLY_TO_ALL_MIDIS"] = button_buff = new button("A2A", system_white, settings::apply_to_all, 80 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "The same as A2A in MIDI's props.");
	button_buff->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Enable/disable inplace merge");

	(*window)["COLLAPSE_MIDI"] = new checkbox(72.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse tracks of a MIDI into one");
	(*window)["APPLY_OFFSET_AFTER"] = new checkbox(57.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Apply offset after PPQ change");

	(*window)["AS_THREADS_COUNT"] = new input_field(std::to_string(g_data.detected_threads), 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Threads count", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["AUTOUPDATECHECK"] = new checkbox(-97.5 + moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, check_autoupdates, &system_white, _Align::left, "Check for updates automatically");
	if (simple_player::syncore_available())
	{
		(*window)["AS_SYNCORE"] = new button(
			"SYNCore...", system_white, on_syncore_setup_open,
			72.5 - moveable_window::window_header_size,
			-47.5 - moveable_window::window_header_size,
			65, 10, 1, 0x7F3FFF3F, 0x7F3FFFFF,
			0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr,
			"Sound bank, audio buffering, phase, gain, and limiter settings");
	}
	/*(*window)["FEEDBACK"] = button_buff = new button("F/B", system_white, settings::feedback_open, 50 - moveable_window::window_header_size, -87.5 - moveable_window::window_header_size, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "_");*/

	(*global_window_handler)["APP_SETTINGS"] = window;

	if (simple_player::syncore_available())
	{
		window = new moveable_fui_window("SYNCore setup", system_white,
			-130, 130 + moveable_window::window_header_size,
			260, 245, 140, 2.5f, 55, 45, 2.5f,
			BACKGROUND_OPQ, HEADER, BORDER);

		(*window)["BANK"] = new text_box(
			"Bank: built-in sine (choose SF2/SFZ below)", legacy_white,
			0, 115, 12, 230, 7, 0, 0, 0,
			_Align::left, text_box::VerticalOverflow::cut);
		(*window)["CHOOSE_BANK"] = new button(
			"Choose SF2/SFZ...", system_white, on_syncore_bank_select,
			-57.5, 94, 105, 10, 1, 0x7F3FFF3F, 0x7F3FFFFF,
			0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr,
			"Choose a SoundFont 2 or SFZ bank and select SYNCore output");
		(*window)["BUILTIN_BANK"] = new button(
			"Built-in sine", system_white, on_syncore_use_builtin_bank,
			67.5, 94, 80, 10, 1, 0x007FFF3F, 0x007FFFFF,
			0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr,
			"Clear the bank path and use SYNCore's test sine instrument");
		(*window)["NOTE"] = new text_box(
			"Changes apply to the next playback | 0 threads = auto",
			system_white, 0, 72, 10, 230, 7, 0, 0, 0, _Align::center);

		auto add_syncore_field = [&](const char* key, const char* label,
			const std::string& value, float y, bool right_column,
			input_field::Type type, std::uint8_t max_chars)
		{
			const float label_x = right_column ? 44.0f : -86.0f;
			const float input_x = right_column ? 100.0f : -30.0f;
			(*window)[std::string(key) + "_LABEL"] = new text_box(
				label, system_white, label_x, y, 10, 68, 7,
				0, 0, 0, _Align::right, text_box::VerticalOverflow::cut);
			(*window)[key] = new input_field(
				value, input_x, y, 10, 40, system_white, nullptr,
				0x007FFFFF, nullptr, " ", max_chars,
				_Align::center, _Align::left, type);
		};

		add_syncore_field("SAMPLE_RATE", "Sample rate", "48000", 48, false,
			input_field::Type::NaturalNumbers, 6);
		add_syncore_field("BUFFER_FRAMES", "Buffer frames", "4096", 48, true,
			input_field::Type::NaturalNumbers, 7);
		add_syncore_field("MAX_COHORTS", "Cohort ceiling", "4096", 28, false,
			input_field::Type::NaturalNumbers, 7);
		add_syncore_field("RENDER_THREADS", "Render threads", "0", 28, true,
			input_field::Type::NaturalNumbers, 2);
		add_syncore_field("GAIN_DB", "Gain (dB)", "-12", 8, false,
			input_field::Type::FP_Any, 7);

		(*window)["PHASE_MODE"] = new button(
			"Phase: Coherent", system_white, on_syncore_phase_cycle,
			-22.5, -16, 175, 10, 1, 0x7F3FFF3F, 0x7F3FFFFF,
			0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr,
			"Cycle the SYNCore phase policy; coherent is the deterministic default");
		(*window)["LIMITER_LABEL"] = new text_box(
			"Limiter", system_white, 62, 8, 10, 68, 7,
			0, 0, 0, _Align::right);
		(*window)["LIMITER"] = new checkbox(
			112, 8, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F,
			1, true, &system_white, _Align::right,
			"Enable SYNCore's -1 dB sample-peak limiter");
		(*window)["STATUS"] = new text_box(
			"Status: Stopped", system_white, 0, -48, 30, 240, 7,
			0xFFFFFF0A, 0x007FFF5F, 1, _Align::center,
			text_box::VerticalOverflow::cut);
		(*window)["APPLY"] = new button(
			"Apply and save", system_white, on_syncore_preferences_apply,
			0, -78, 100, 10, 1, 0x007FFF3F, 0x007FFFFF,
			0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr,
			"Persist these settings and restart SYNCore if it is open");

		(*global_window_handler)["SYNCORE_SETTINGS"] = window;
	}

	initialize_player_video_render_window(BACKGROUND_OPQ, HEADER, BORDER);

	window = new moveable_window("SMRP Container", system_white, -300, 300, 600, 600, 0x000000CF, 0xFFFFFF7F);

	(*window)["TIMER"] = new input_field("0 s", 0, 195, 10, 50, system_white, nullptr, 0, &system_white, "Timer", 12, _Align::center, _Align::center, input_field::Type::text);

	(*global_window_handler)["SMRP_CONTAINER"] = window;

	window = new moveable_fui_window("MIDI Info Collector", system_white, -150, 200, 300, 400, 200, 1.25f, 100, 100, 5, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["FLL"] = new text_box("--File log line--", system_white, 0, -moveable_window::window_header_size + 185, 15, 285, 10, 0, 0, 0, _Align::left);
	(*window)["FEL"] = new text_box("--File error line--", system_red, 0, -moveable_window::window_header_size + 175, 15, 285, 10, 0, 0, 0, _Align::left);

	(*window)["TEMPO_GRAPH"] = new Graphing<single_midi_info_collector::tempo_graph>(
		0, -moveable_window::window_header_size + 145, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false);
	(*window)["POLY_GRAPH"] = new Graphing<single_midi_info_collector::polyphony_graph>(
		0, -moveable_window::window_header_size + 95, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false);
	(*window)["NPS_GRAPH"] = new Graphing<single_midi_info_collector::notes_per_second_graph>(
		0, -moveable_window::window_header_size + 45, 285, 50, (1. / 20000.), true, 0x00FF9F7F, 0xFFFFFFFF, 0xFFAF00FF, 0xFFFFFFFF, 0x5F5F5F7F, nullptr, system_white, false);

	(*window)["PG_SWITCH"] = new button("Enable graph B", system_white, props_and_sets::SMIC::enable_pg, 37.5, 10 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Polyphony graph");
	(*window)["TG_SWITCH"] = new button("Enable graph A", system_white, props_and_sets::SMIC::enable_tg, -37.5, 10 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Tempo graph");
	(*window)["ALL_EXP"] = new button("Export all", system_white, props_and_sets::SMIC::export_all, 110, 10 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["TG_EXP"] = new button("Export Tempo", system_white, props_and_sets::SMIC::export_tg, -110, 10 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["LD_TIME_MAP"] = new button("Copy time map", system_white, props_and_sets::SMIC::load_time_map, 45, -10 - moveable_window::window_header_size, 65, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr);
	(*window)["HUMANREADIBLE"] = button_buff = new button(".csv", system_white, props_and_sets::SMIC::switch_personal_use, 105, -10 - moveable_window::window_header_size, 45, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Switches output format for `export all`");
	button_buff->tip->safe_change_position_argumented(_Align::right, button_buff->x_pos + button_buff->width * 0.5, button_buff->tip->cy_pos);
	(*window)["TOTAL_INFO"] = new text_box("----", system_white, 0, -150, 35, 285, 10, 0, 0, 0, _Align::left);
	(*window)["INT_MIN"] = new input_field("0", -132.5, -10 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Minutes", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INT_SEC"] = new input_field("0", -107.5, -10 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Seconds", 2, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INT_MSC"] = new input_field("000", -80, -10 - moveable_window::window_header_size, 10, 25, system_white, nullptr, 0x007FFFFF, &system_white, "Milliseconds", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INTEGRATE_TICKS"] = new button("Integrate ticks", system_white, props_and_sets::SMIC::integrate_time, -27.5, -10 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the closest tick to that time.");
	(*window)["INT_TIC"] = new input_field("0", -105, -30 - moveable_window::window_header_size, 10, 75, system_white, nullptr, 0x007FFFFF, &system_white, "Ticks", 17, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INTEGRATE_TIME"] = new button("Integrate time", system_white, props_and_sets::SMIC::differentiate_ticks, -27.5, -30 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the time of that tick.");
	(*window)["DELIM"] = new input_field(";", 137.5, -10 - moveable_window::window_header_size, 10, 7.5, system_white, &(props_and_sets::csv_delim), 0x007FFFFF, &system_white, "Delimiter", 1, _Align::center, _Align::right, input_field::Type::text);
	(*window)["ANSWER"] = new text_box("----", system_white, -66.25, -80, 25, 152.5, 10, 0, 0, 0, _Align::center, text_box::VerticalOverflow::recalibrate);

	(*global_window_handler)["SMIC"] = window;

	window = new moveable_fui_window("Simple MIDI player", system_white, /*-200, 197.5, 400, 397.5, 150, 2.5f, 75, 75, 5*/
		-200, 175 + moveable_window::window_header_size, 400, 375, 150, 2.5, 65, 65, 2.5, BACKGROUND_OPQ, HEADER, BORDER);
	window->on_close = on_simplayer_close;

	(*window)["TEXT"] = new text_box("TIME", legacy_white, 0, 130 + moveable_window::window_header_size, 50, 175, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
	(*window)["PAUSE"] = new button("\202", legacy_white, on_player_pause_toggle, -190, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["STOP"] = new button("\201", legacy_white, on_player_stop, -175, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	auto player_view = new player_viewer(0, -20);
	(*window)["VIEW_LEN_SLIDER"] = new slider(slider::Orientation::horizontal, -130, 180 - moveable_window::window_header_size, 65, 14, 23, log2f(player_view->data->scroll_window_us), on_view_length_change, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF, 0x007FFFFF, 0x808080FF, 10, 4);
	(*window)["BUFFERING_SWITCH"] = new button(
		player_view->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered",
		system_white,
		on_unbuffered_switch,
		155, 180 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["OVERLAP_SWITCH"] = new button(
		"R/t OR",
		system_white,
		on_overlap_removal_switch,
		155, 150 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	auto playback_seek_slider = new slider(slider::Orientation::horizontal, 0,
		130 - moveable_window::window_header_size, 375, 0, 1, 0,
		on_playback_seek_to, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF,
		0x007FFFFF, 0x808080FF, 10, 4);
	// Restarting the parser for every mouse-move event causes dense files to
	// spend the entire drag cancelling seeks. Commit the final slider value once.
	playback_seek_slider->fire_on_release = true;
	(*window)["SEEK_TO"] = playback_seek_slider;

	(*window)["RENDER_VIDEO"] = new button("Render", system_white,
		open_player_video_render_settings, 135, 165 - moveable_window::window_header_size,
		40, 10, 1, 0x7F3FFF3F, 0x7F3FFFFF, 0xFFFFFFFF,
		0x7F3FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["MAXIMISE"] = new button("Maximise", system_white,
		switch_maximise, 175, 165 - moveable_window::window_header_size,
		40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF,
		0x007FFFFF, 0xFFFFFFFF, nullptr);

	(*window)["VIEW"] = player_view;

	// Device selection label and list
	auto device_list_selector = new selectable_properted_list(
		bs_list_black_small,
		on_device_select,
		nullptr,  // No properties callback
		-145, 135 + moveable_window::window_header_size,  // Position: left side, below viewport
		100,  // width
		12,   // Space between items
		20,   // Max chars per line
		2     // Max visible lines
	);
	device_list_selector->arrow_stick_height = 2.5;

	(*window)["DEVICE_LIST"] = device_list_selector;

	(*global_window_handler)["SIMPLAYER"] = window;
	update_device_list();

	// ========================================================================
	// Unified MIDI / nested-archive source dialog. Playback is handed to
	// SIMPLAYER so there is only one set of controls and one visualiser.
	// ========================================================================
	window = new moveable_fui_window("Open MIDI or archive", system_white,
		-140, 55 + moveable_window::window_header_size,
		280, 105, 180, 2.5, 30, 30, 2.5,
		BACKGROUND_OPQ, HEADER, BORDER);
	window->on_close = []()
	{
		if (compressed_player_preparing.load(std::memory_order_acquire))
			compressed_player_cancel.store(true, std::memory_order_release);
	};

	(*window)["TEXT"] = new text_box(
		"Choose or drop a MIDI or XZ/ZIP/7z archive; nested layers are supported",
		legacy_white, 0, 38 - moveable_window::window_header_size,
		38, 250, 10, 0xFFFFFF1A, 0, 0,
		_Align(center | top), text_box::VerticalOverflow::recalibrate);
	(*window)["OPEN"] = new button(
		"Browse...", system_white, on_player_source_open,
		-45, -5 - moveable_window::window_header_size,
		120, 12, 1, 0x7F3FFF7F, 0xFFFFFFFF, 0x7F3FFFFF,
		0xFFFFFFFF, 0x7F3FFFFF, nullptr,
		"Open a MIDI, XZ/ZIP/7z, or a nested archive containing one MIDI");
	(*window)["CANCEL"] = new button(
		"Cancel", system_white, on_compressed_preparation_cancel,
		85, -5 - moveable_window::window_header_size,
		60, 12, 1, 0x5F5F5F7F, 0xFFFFFFFF, 0x7F7F7FFF,
		0xFFFFFFFF, 0x7F7F7FFF, nullptr,
		"Cancel preparation, or close this dialog");
	(*global_window_handler)["ARCHIVE_SOURCE"] = window;

	// ========================================================================
	// MIDI Editor Window
	// ========================================================================
	window = new moveable_fui_window("MIDI Piano Roll Editor", system_white,
		-200, 197.5f, 400, 397.5f, 300, 2.5f, 100, 100, 5, BACKGROUND_OPQ, HEADER, BORDER);

	// Editor viewer (piano roll visualization) - main area, left of the button column
	auto editor_view = new midi_editor_viewer(-45, -30, editor.get());
	editor_view->data.width = 300;
	editor_view->data.height = 300;
	editor_view->on_track_changed = update_editor_status_text;
	editor_view->on_status = editor_flash_status;
	editor_view->on_status_restore = update_editor_status_text;
	editor_view->on_draw_state_changed = update_channel_indicator;
	editor_view->on_open_chopper = on_editor_open_chopper;
	editor_view->on_open_flip = on_editor_open_flip;
	editor_view->on_open_claw = on_editor_open_claw;
	editor_view->on_open_lfo = on_editor_open_lfo;
	editor_view->on_save = on_editor_save_file;
	editor_view->on_play_from_view = on_editor_play_from_view;
	editor_view->note_audition = [](std::uint8_t key, std::uint8_t velocity, std::uint8_t channel, bool on)
	{
		if (!player)
			return;
		if (on && !player->ensure_output(saved_midi_device_name))
			return;
		player->preview_note(channel, key, velocity, on);
	};
	editor_view->playback_seconds = []() -> double
	{
		if (!player || !editor_playback_active.load(std::memory_order_acquire) || !player->is_playing())
			return -1.0;
		return double(player->get_position_us()) / 1000000.0;
	};
	(*window)["VIEW"] = editor_view;

	// Status text (single line above the viewer)
	(*window)["TEXT"] = new text_box("Load a MIDI file to begin editing", legacy_white, -45, 165, 12, 200, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);

	// File operation buttons (right side, aligned with MAIN window pattern)
	(*window)["LOAD_FILE"] = new button("Load MIDI", system_black, on_editor_load_file, 150, 167.5, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, "Load MIDI file for editing");
	(*window)["SAVE_FILE"] = new button("Save MIDI", system_black, on_editor_save_file, 150, 155, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, "Save edited MIDI file");
	(*window)["TOOL_CHOP"] = new button("Chop", system_white, on_editor_open_chopper, 130, 137.5, 36, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Chopper (Alt+U)");
	(*window)["TOOL_FLIP"] = new button("Flip", system_white, on_editor_open_flip, 170, 137.5, 36, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Flip score (Alt+Y)");
	(*window)["TOOL_CLAW"] = new button("Claw", system_white, on_editor_open_claw, 130, 125, 36, 10, 1, 0x7F3FFF3F, 0x7F3FFFFF, 0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr, "Claw machine (Alt+W)");
	(*window)["TOOL_LFO"] = new button("LFO", system_white, on_editor_open_lfo, 170, 125, 36, 10, 1, 0xFFAF003F, 0xFFAF00FF, 0xFFFFFFFF, 0xFFAF00FF, 0xFFFFFFFF, nullptr, "LFO tool (Alt+O)");

	// Channel selector: which channel newly drawn notes land on. Clicking a
	// note picks its channel up too; a bright border marks the current one.
	for (int channel = 0; channel < 16; ++channel)
	{
		const float chan_x = -190.f + 15.f * channel;
		(*window)["CH" + std::to_string(channel)] = new button(
			std::to_string(channel + 1), system_white,
			[channel]() { on_editor_channel_select(channel); },
			chan_x, 140, 14, 10, 1,
			editor_channel_button_color(0, channel),
			0xFFFFFF7F,
			0xFFFFFFFF,
			editor_channel_button_color(0, channel, true),
			0xFFFFFFFF,
			nullptr, "Channel for new notes");
	}

	// Playback button
	(*window)["PLAY"] = new button("Play", system_black, on_editor_play, 150, 92.5, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, "Play / stop current MIDI");
	(*window)["PLAY_FROM"] = new button("Play from view", system_white, on_editor_play_from_view, 150, 80, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Play from the visible start tick");
	// Maximise button
	(*window)["MAXIMISE"] = new button("Maximise", system_white, switch_midieditor_maximise, 150, 67.5, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Expand editor to full window");

	// Velocity lane visibility
	(*window)["VEL_LANE"] = new button("Hide Lane", system_white, on_editor_toggle_lane, 150, 55, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Show/hide the bottom lane (drag its divider to resize)");

	(*window)["LANE_VEL"] = new button("Vel", system_white, []() { on_editor_lane_mode(midi_editor_viewer::lane_mode::note_velocity); }, -180, 126.5, 30, 10, 2, 0x007FFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Edit note velocity");
	(*window)["LANE_PITCH"] = new button("Pitch", system_white, []() { on_editor_lane_mode(midi_editor_viewer::lane_mode::pitch_bend); }, -145, 126.5, 30, 10, 1, 0x7F3FFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF, nullptr, "Edit channel pitch bend");
	(*window)["LANE_PAN"] = new button("Pan", system_white, []() { on_editor_lane_mode(midi_editor_viewer::lane_mode::pan); }, -110, 126.5, 30, 10, 1, 0xFFAF003F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAF00FF, 0xFFFFFFFF, nullptr, "Edit channel pan CC10");
	(*window)["LANE_CCVOL"] = new button("CCVol", system_white, []() { on_editor_lane_mode(midi_editor_viewer::lane_mode::channel_volume); }, -75, 126.5, 30, 10, 1, 0x00AF7F3F, 0xFFFFFFFF, 0xFFFFFFFF, 0x00AF7FFF, 0xFFFFFFFF, nullptr, "Edit channel volume CC7");
	(*window)["LANE_TEMPO"] = new button("Tempo", system_white, []() { on_editor_lane_mode(midi_editor_viewer::lane_mode::tempo); }, -40, 126.5, 30, 10, 1, 0xFFAF003F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAF00FF, 0xFFFFFFFF, nullptr, "Edit tempo map; Ctrl+wheel zooms, Shift+wheel pans, Ctrl+Shift+wheel resets range");

	auto editor_track_list = new selectable_properted_list(
		bs_list_black_small,
		on_editor_track_list_select,
		nullptr,
		150,
		37.5,
		75,
		12,
		18,
		5);
	editor_track_list->arrow_stick_height = 2.5;
	(*window)["TRACK_LIST"] = editor_track_list;
	(*window)["RENAME_TRACK"] = new button("Rename track", system_white, on_editor_rename_track, 150, -30, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Rename active track");

	// Track navigation (right-clicking a gray note also switches track)
	(*window)["TRACK_NEXT"] = new button("Next track", system_white, on_editor_track_next, 150, -87.5, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Next track (or right-click a gray note)");
	(*window)["TRACK_PREV"] = new button("Prev track", system_white, on_editor_track_prev, 150, -100, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Previous track");

	// Snap grid for note drawing (Del/Ctrl+C/Ctrl+V/Ctrl+B work via keyboard)
	(*window)["SNAP"] = new button("Snap: 1/16", system_white, on_editor_snap_cycle, 150, -115, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Cycle the drawing snap grid");

	// Edit operation buttons (bottom area)
	(*window)["UNDO"] = new button("Undo", system_white, on_editor_undo, 150, -135, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Undo last edit (Ctrl+Z)");
	(*window)["REDO"] = new button("Redo", system_white, on_editor_redo, 150, -147.5, 75, 12, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "Redo undone edit (Ctrl+Y)");

	// Back to main window button
	(*window)["BACK_TO_MAIN"] = new button("Back", system_white, []()
	{
		if (midieditor_maximised)
			switch_midieditor_maximise();
		global_window_handler->main_window_id = "MAIN";
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("MAIN");
	}, 150, -182.5, 75, 12, 1, 0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, "Return to main window");

	(*global_window_handler)["MIDI_EDITOR"] = window;
	update_channel_indicator();
	update_editor_track_list();

	initialize_midi_editor_tool_windows(editor_flash_status,
		[](const std::string& message) { throw_alert_warning(std::string(message)); },
		BACKGROUND_OPQ, HEADER, BORDER);

	window = new moveable_fui_window("Feedback/Support? O.o", system_white, -100, 100 + moveable_window::window_header_size, 200, 200 + moveable_window::window_header_size, 100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["EDITBOX"] = new edit_box(" ", &system_white, 0, 0, 150, 150, 10, 0, ~0U ^ 0b11100000, 1);

	(*global_window_handler)["SUPPORT"] = window;

	global_window_handler->enable_window("MAIN");
	//global_window_handler->enable_window("SIMPLAYER");
	//global_window_handler->enable_window("V1WT");
	//global_window_handler->enable_window("COMPILEW"); // todo: someday fix the damn editbox...
	//global_window_handler->enable_window("SMIC");
	//global_window_handler->enable_window("OR");
	//global_window_handler->enable_window("SMRP_CONTAINER");
	//global_window_handler->enable_window("APP_SETTINGS");
	//global_window_handler->enable_window("CAT");
	//global_window_handler->enable_window("SMPAS");//Debug line
	//global_window_handler->enable_window("PROMPT");////DEBUUUUG
	//global_window_handler->enable_window("OTHER_SETS");

	on_overlap_removal_switch_action(false);

	DragAcceptFiles(hWnd, true);
	OleInitialize(nullptr);
	global_drag_and_drop_handler.drop_override = try_open_drop_in_player;

	std::cout << "Registering Drag&Drop: " << (RegisterDragDrop(hWnd, &global_drag_and_drop_handler)) << std::endl;

	safc_version_check();
}
