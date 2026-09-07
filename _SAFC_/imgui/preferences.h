#pragma once
#include <cstdint>
#include <string>
#include "../SAFC_InnerModules/bool_settings.h"
#include "../SAFC_InnerModules/syncore_output.h"
#include "../SAFC_InnerModules/simple_player_video_export.h"

namespace safc::imgui_ui
{
struct application_preferences
{
    std::uint32_t processing_flags = _BoolSettings::remove_remnants | _BoolSettings::remove_empty_tracks | _BoolSettings::all_instruments_to_piano;
    std::uint16_t processing_threads = 1;
    bool split_channels = false, collapse_tracks = false, apply_offset_after = true;
    bool inplace_merge = false, rsb_compression = false, allow_sysex = false;
    int background = 0;
    float ui_scale = 1.f;
    std::wstring midi_device, sound_bank;
    syncore_preferences synth;
    simple_player_video_settings video;
};

class preferences_store
{
public:
    application_preferences load() const;
    void save(const application_preferences&) const;
};
}
