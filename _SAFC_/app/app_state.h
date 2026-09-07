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
#include "project_model.h"

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
