#define NOMINMAX
#include "preferences.h"
#include <type_traits>
#include "../WinRegWrappers.h"
#include <algorithm>
#include <cmath>
#include <thread>

namespace safc::imgui_ui
{
application_preferences preferences_store::load() const
{
    application_preferences p;
    p.processing_threads = static_cast<std::uint16_t>(std::clamp(std::thread::hardware_concurrency(), 1u, 64u));
    WinReg::RegKey key;
    try { key.Open(HKEY_CURRENT_USER, L"Software\\SAFC\\", KEY_READ); }
    catch (...) { return p; }
    auto number = [&](const wchar_t* name, auto& value)
    {
        try { value = static_cast<std::remove_reference_t<decltype(value)>>(key.GetDwordValue(name)); }
        catch (...) {}
    };
    auto text = [&](const wchar_t* name, std::wstring& value)
    { try { value = key.GetStringValue(name); } catch (...) {} };
    number(L"DEFAULT_BOOL_SETTINGS", p.processing_flags);
    number(L"AS_THREADS_COUNT", p.processing_threads);
    number(L"SPLIT_TRACKS", p.split_channels);
    number(L"COLLAPSE_MIDI", p.collapse_tracks);
    number(L"APPLY_OFFSET_AFTER", p.apply_offset_after);
    number(L"AS_INPLACE_FLAG", p.inplace_merge);
    number(L"RSB_COMPRESS", p.rsb_compression);
    number(L"ALLOW_SYSEX", p.allow_sysex);
    number(L"AS_BCKGID", p.background);
    text(L"MIDI_DEVICE_NAME", p.midi_device);
    text(L"SYNCORE_BANK_PATH", p.sound_bank);
    number(L"SYNCORE_SAMPLE_RATE", p.synth.sample_rate);
    number(L"SYNCORE_BUFFER_FRAMES", p.synth.buffer_frames);
    number(L"SYNCORE_MAX_COHORTS", p.synth.maximum_cohorts);
    number(L"SYNCORE_RENDER_THREADS", p.synth.render_threads);
    number(L"SYNCORE_PHASE_MODE", p.synth.phase_mode);
    number(L"SYNCORE_LIMITER", p.synth.limiter_enabled);
    number(L"PLAYER_RENDER_WIDTH", p.video.width);
    number(L"PLAYER_RENDER_HEIGHT", p.video.height);
    number(L"PLAYER_RENDER_FPS", p.video.fps);
    number(L"PLAYER_RENDER_VIDEO_KBPS", p.video.video_bitrate_kbps);
    number(L"PLAYER_RENDER_AUDIO_KBPS", p.video.audio_bitrate_kbps);
    number(L"PLAYER_RENDER_AUDIO_RATE", p.video.audio_sample_rate);
    try { p.video.tail_seconds = std::stod(key.GetStringValue(L"PLAYER_RENDER_TAIL_SECONDS")); } catch (...) {}
    try { p.video.visible_seconds = std::stod(key.GetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS")); } catch (...) {}
    try { p.synth.output_gain_db = std::stod(key.GetStringValue(L"SYNCORE_GAIN_DB")); } catch (...) {}
    try { p.ui_scale = std::stof(key.GetStringValue(L"IMGUI_UI_SCALE")); } catch (...) {}
    p.processing_threads = std::clamp<std::uint16_t>(p.processing_threads, 1, 256);
    p.synth.sample_rate = std::clamp(p.synth.sample_rate, 8000u, 192000u);
    p.synth.buffer_frames = std::clamp(p.synth.buffer_frames, 256u, 1048576u);
    p.synth.maximum_cohorts = std::clamp(p.synth.maximum_cohorts, 1u, 1048576u);
    p.synth.render_threads = std::min(p.synth.render_threads, 64u);
    if (static_cast<unsigned>(p.synth.phase_mode) > 4) p.synth.phase_mode = syncore_phase_mode::coherent;
    p.synth.output_gain_db = std::isfinite(p.synth.output_gain_db) ? std::clamp(p.synth.output_gain_db, -60., 12.) : -12.;
    p.ui_scale = std::isfinite(p.ui_scale) ? std::clamp(p.ui_scale, .75f, 2.f) : 1.f;
    return p;
}

void preferences_store::save(const application_preferences& p) const
{
    WinReg::RegKey key;
    key.Create(HKEY_CURRENT_USER, L"Software\\SAFC\\");
    key.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", p.processing_flags);
    key.SetDwordValue(L"AS_THREADS_COUNT", p.processing_threads);
    key.SetDwordValue(L"SPLIT_TRACKS", p.split_channels);
    key.SetDwordValue(L"COLLAPSE_MIDI", p.collapse_tracks);
    key.SetDwordValue(L"APPLY_OFFSET_AFTER", p.apply_offset_after);
    key.SetDwordValue(L"AS_INPLACE_FLAG", p.inplace_merge);
    key.SetDwordValue(L"RSB_COMPRESS", p.rsb_compression);
    key.SetDwordValue(L"ALLOW_SYSEX", p.allow_sysex);
    key.SetDwordValue(L"AS_BCKGID", p.background);
    key.SetStringValue(L"IMGUI_UI_SCALE", std::to_wstring(p.ui_scale));
    key.SetStringValue(L"MIDI_DEVICE_NAME", p.midi_device);
    key.SetStringValue(L"SYNCORE_BANK_PATH", p.sound_bank);
    key.SetDwordValue(L"SYNCORE_SAMPLE_RATE", p.synth.sample_rate);
    key.SetDwordValue(L"SYNCORE_BUFFER_FRAMES", p.synth.buffer_frames);
    key.SetDwordValue(L"SYNCORE_MAX_COHORTS", p.synth.maximum_cohorts);
    key.SetDwordValue(L"SYNCORE_RENDER_THREADS", p.synth.render_threads);
    key.SetDwordValue(L"SYNCORE_PHASE_MODE", static_cast<std::uint32_t>(p.synth.phase_mode));
    key.SetDwordValue(L"SYNCORE_LIMITER", p.synth.limiter_enabled);
    key.SetStringValue(L"SYNCORE_GAIN_DB", std::to_wstring(p.synth.output_gain_db));
    key.SetDwordValue(L"PLAYER_RENDER_WIDTH", p.video.width);
    key.SetDwordValue(L"PLAYER_RENDER_HEIGHT", p.video.height);
    key.SetDwordValue(L"PLAYER_RENDER_FPS", p.video.fps);
    key.SetDwordValue(L"PLAYER_RENDER_VIDEO_KBPS", p.video.video_bitrate_kbps);
    key.SetDwordValue(L"PLAYER_RENDER_AUDIO_KBPS", p.video.audio_bitrate_kbps);
    key.SetDwordValue(L"PLAYER_RENDER_AUDIO_RATE", p.video.audio_sample_rate);
    key.SetStringValue(L"PLAYER_RENDER_TAIL_SECONDS", std::to_wstring(p.video.tail_seconds));
    key.SetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS", std::to_wstring(p.video.visible_seconds));
}
}
