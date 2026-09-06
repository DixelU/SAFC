#pragma once

#define NOMINMAX

#include <algorithm>
#include <cstdlib>
#include <io.h>
#include <tuple>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <vector>
#include <filesystem>
#include <deque>
#include <fstream>
#include <string>
#include <iterator>
#include <map>
#include <thread>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <boost/algorithm/string.hpp>

#include <WinSock2.h>
#include <urlmon.h>


#include "../WinRegWrappers.h"

#include "../JSON/JSON.h"

#include "../btree/btree_map.h"
#include "../SAFGUIF/fonted_manip.h"
#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/include_all.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"
#include "../SAFCGUIF_Local/SAFGUIF_L.h"
#include "../SAFCGUIF_Local/simple_player_viewer.h"
#include "../SAFCGUIF_Local/midi_editor_viewer.h"
#include "../SAFCGUIF_Local/midi_editor_tools_ui.h"
#include "../SAFCGUIF_Local/player_video_render_ui.h"

#include "../SAFC_InnerModules/single_midi_processor_2.h"
#include "../SAFC_InnerModules/bool_settings.h"
#include "../consts.h"

#include "app_workers.h"

// Shared application data. Definitions live in app_state.cpp.
extern button_settings* bs_list_black_small;
extern std::uint32_t default_bool_settings;
extern syncore_preferences saved_syncore_preferences;
extern syncore_preferences syncore_preferences_draft;
struct file_settings
{////per file settings
	std::wstring filename;
	std::wstring postprocessed_file_name, w_file_name_postfix;
	std::string appearance_filename, appearance_path, file_name_postfix;

	double new_tempo = 0;

	std::uint16_t new_ppqn = 0, old_ppqn = 0, old_track_number = 0, merge_multiplier = 0;
	std::int16_t group_id = 0;

	std::uint64_t filesize = 0;
	std::int64_t selection_start = 0, selection_length = -1;
	std::uint32_t bool_settings = default_bool_settings;
	std::int64_t offset_ticks = 0;

	bool
		is_midi = false,
		inplace_merge_enabled = false,
		allow_legacy_rsb_meta_interaction = false,
		rsb_compression = false,
		channels_split = true,
		collapse_midi = false,
		allow_sysex = false,
		enable_zero_velocity = false,
		apply_offset_after = true,
		ppqn_manually_set = false;

	std::shared_ptr<cut_and_transpose> key_map;
	std::shared_ptr<dixelu::polyline_converter<std::uint8_t, std::uint8_t>> volume_map;
	std::shared_ptr<dixelu::polyline_converter<std::uint16_t, std::uint16_t>> pitch_bend_map;

	single_midi_info_collector::time_graph time_map;

	file_settings(const std::wstring& filename);

	void switch_bool_setting(std::uint32_t smp_bool_setting);

	void set_bool_setting(std::uint32_t smp_bool_setting, bool new_state);

	std::shared_ptr<single_midi_processor_2::processing_data> build_smrp_processing_data();
};

struct safc_data
{
	////overall settings and storing perfile settings....
	std::vector<file_settings> files;
	std::wstring save_path;
	std::uint16_t global_ppqn;
	std::int32_t global_offset;

	float global_new_tempo;
	bool incremental_ppqn;
	bool inplace_merge_flag;
	bool channels_split;
	bool rsb_compression;
	bool collapse_midi;
	bool apply_offset_after;
	bool is_cli_mode;

	std::uint16_t detected_threads;

	safc_data();

	void resolve_subdivision_problem_group_id_assign(std::uint16_t threads_count = 0);

	void set_global_ppqn(std::uint16_t new_ppqn = 0, bool force_global_ppqn_override = false);

	void set_global_offset(std::int32_t offset);

	void set_global_tempo(float new_tempo);

	void remove_by_id(std::uint32_t id);

	std::shared_ptr<midi_collection_threaded_merger> mctm_constructor();

	file_settings& operator[](std::int32_t id);
};


extern safc_data g_data;
extern std::shared_ptr<midi_collection_threaded_merger> global_mctm;
extern std::atomic_bool application_shutting_down;

bool gui_stop_requested(std::stop_token stop_token = {}) noexcept;
size_t get_available_memory();
void throw_alert_error(std::string&& text);
void throw_alert_warning(std::string&& text);
handleable_ui_part* _WH(const char* window, const char* element);
template<typename ui_part_type>
ui_part_type* _WH_t(const char* window, const char* element) requires std::is_base_of_v<handleable_ui_part, ui_part_type>
{
	return dynamic_cast<ui_part_type*>(_WH(window, element));
}
