#pragma once
#include <functional>
#include <string>
#include <vector>

namespace safc::imgui_ui
{
// UI-thread callbacks bind the native owner window without exposing platform
// handles or process globals to panels and services. Empty paths mean Cancel.
struct native_dialogs
{
    std::function<std::wstring()> open_midi;
    std::function<std::wstring()> open_any;
    std::function<std::wstring()> open_bank;
    std::function<std::wstring(const std::wstring&)> save_midi;
    std::function<std::wstring(const std::wstring&)> save_video;
    std::function<std::wstring(const std::wstring&)> save_data;
    std::function<std::vector<std::wstring>()> add_midis;
};
}
