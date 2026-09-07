#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <polyline_converter.h>
#include "../SAFC_InnerModules/cut_and_transpose.h"

namespace safc::imgui_ui
{
// All edits and clipboard data belong to the UI thread. No file or widget
// pointer survives a draw, so changing/removing the selected MIDI is safe.
class mapping_panel
{
public:
    using volume_curve = dixelu::polyline_converter<std::uint8_t, std::uint8_t>;
    using pitch_curve = dixelu::polyline_converter<std::uint16_t, std::uint16_t>;

    void draw_key_map(const char* file_label, std::shared_ptr<::cut_and_transpose>& map, bool* open);
    void draw_volume_map(const char* file_label, std::shared_ptr<volume_curve>& map, bool* open);
    void draw_pitch_map(const char* file_label, std::shared_ptr<pitch_curve>& map, bool* open);

private:
    struct curve_state
    {
        const void* owner = nullptr;
        int mode = 0;
        int x = 0, y = 0;
        int first_x = -1, first_y = 0;
        float degree = 1.f;
        bool extended = false;
        bool symmetric = true;
    };
    template<class Curve>
    void draw_curve(const char* title, const char* label, std::shared_ptr<Curve>& map,
        bool* open, curve_state& state, std::optional<Curve>& clipboard, int maximum);

    std::optional<::cut_and_transpose> key_clipboard_;
    std::optional<volume_curve> volume_clipboard_;
    std::optional<pitch_curve> pitch_clipboard_;
    curve_state volume_, pitch_;
    bool extended_keys_ = false;
    int cut_anchor_ = -1;
};
}
