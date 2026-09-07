#include "mapping_panel.h"
#include "folded_theme.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace safc::imgui_ui
{
namespace
{
constexpr ImU32 accent = IM_COL32(53, 165, 246, 255);
constexpr ImU32 curve_color = IM_COL32(189, 146, 244, 255);

bool black_key(int key)
{
    switch (key % 12) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; }
}

std::string key_name(int key)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[key % 12]) + std::to_string(key / 12 - 1);
}

void draw_key_strip(ImDrawList& draw, ImVec2 origin, ImVec2 size,
    const ::cut_and_transpose& map, int bank, bool output)
{
    const ImVec2 far(origin.x + size.x, origin.y + size.y);
    draw.AddRectFilled(origin, far, IM_COL32(6, 19, 30, 255));
    draw.PushClipRect(origin, far, true);
    for (int key = 0; key < 256; ++key)
    {
        // The output piano slides over the fixed input coordinate system:
        // output key k sits directly above source key k - transpose.
        const int source = output ? key - map.transpose_val : key;
        const float x = origin.x + size.x * (source - bank * 128) / 128.f;
        const float next = x + size.x / 128.f;
        if (next <= origin.x || x >= far.x) continue;
        const bool included = source >= 0 && source <= 255
            && map.process(static_cast<std::uint8_t>(source)).has_value();
        const ImU32 white = included ? IM_COL32(207, 225, 233, 255) : IM_COL32(61, 74, 83, 255);
        const ImU32 black = included ? IM_COL32(16, 33, 48, 255) : IM_COL32(23, 32, 42, 255);
        draw.AddRectFilled(ImVec2(x, origin.y), ImVec2(next, far.y), white);
        if (black_key(key))
        {
            const float top = output ? origin.y + size.y * .45f : origin.y;
            const float bottom = output ? far.y : origin.y + size.y * .55f;
            draw.AddRectFilled(ImVec2(x, top), ImVec2(next, bottom), black);
        }
        if (included)
        {
            const float y = output ? far.y - 4.f : origin.y;
            draw.AddRectFilled(ImVec2(x, y), ImVec2(next, y + 4.f), accent);
        }
    }
    // Draw dividers and labels after the fills so later keys cannot cover the
    // octave number, and fractional key widths keep consistent separators.
    for (int key = 0; key <= 256; ++key)
    {
        const int source = output ? key - map.transpose_val : key;
        const float x = origin.x + size.x * (source - bank * 128) / 128.f;
        if (x < origin.x || x > far.x) continue;
        draw.AddLine(ImVec2(x, origin.y), ImVec2(x, far.y), IM_COL32(6, 19, 30, 200));
    }
    if (size.x > 650) for (int key = 0; key < 256; key += 12)
    {
        const int source = output ? key - map.transpose_val : key;
        const float x = origin.x + size.x * (source - bank * 128) / 128.f;
        if (x < origin.x || x >= far.x) continue;
        draw.AddText(ImVec2(x + 1, output ? origin.y + 4 : far.y - ImGui::GetTextLineHeight() - 4),
            IM_COL32(53, 112, 151, 255), key_name(key).c_str());
    }
    draw.PopClipRect();
    draw.AddRect(origin, far, IM_COL32(53, 112, 151, 255));
}

template<class Curve>
void identity(Curve& curve, int maximum)
{
    curve.clear();
    curve.insert(0, 0);
    curve.insert(static_cast<typename Curve::key_type>(maximum),
        static_cast<typename Curve::value_type>(maximum));
}

// Remove only collinear interior vertices. Keeping the endpoints preserves
// both the interpolation and the established linear extrapolation contract.
template<class Curve>
void simplify(Curve& curve)
{
    using point = typename Curve::point_type;
    std::vector<point> keep;
    for (const auto& p : curve.points())
    {
        while (keep.size() >= 2)
        {
            const auto& a = keep[keep.size() - 2];
            const auto& b = keep.back();
            const auto cross = (std::int64_t(b.first) - a.first) * (std::int64_t(p.second) - a.second)
                - (std::int64_t(b.second) - a.second) * (std::int64_t(p.first) - a.first);
            if (cross != 0) break;
            keep.pop_back();
        }
        keep.push_back(p);
    }
    curve = Curve(keep);
}
}

void mapping_panel::draw_key_map(const char* label, std::shared_ptr<::cut_and_transpose>& map, bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(860, 540), ImGuiCond_FirstUseEver);
    if (!begin_folded_window("Cut & transpose", open)) { end_folded_window(); return; }
    ImGui::TextUnformatted(label);
    bool enabled = bool(map);
    if (ImGui::Checkbox("Enable key map", &enabled))
    {
        if (enabled) map = std::make_shared<::cut_and_transpose>(0, 127, 0);
        else map.reset();
    }
    if (!map) { ImGui::TextDisabled("Notes keep their original keys."); end_folded_window(); return; }

    if (ImGui::Button("Reset (0..255)")) *map = ::cut_and_transpose(0, 255, 0);
    ImGui::SameLine();
    if (ImGui::Button("Keep 0..127")) *map = ::cut_and_transpose(0, 127, 0);
    ImGui::SameLine();
    if (ImGui::Button("0..127 -> 128..255")) *map = ::cut_and_transpose(0, 127, 128);
    ImGui::SameLine();
    if (ImGui::Button("Copy")) key_clipboard_ = *map;
    ImGui::SameLine();
    ImGui::BeginDisabled(!key_clipboard_);
    if (ImGui::Button("Paste")) *map = *key_clipboard_;
    ImGui::EndDisabled();

    int minimum = map->min_val, maximum = map->max_val, shift = map->transpose_val;
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderInt("First input key", &minimum, 0, 255)) map->min_val = static_cast<std::uint8_t>(minimum);
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderInt("Last input key", &maximum, 0, 255)) map->max_val = static_cast<std::uint8_t>(maximum);
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderInt("Transpose (semitones)", &shift, -255, 255)) map->transpose_val = static_cast<std::int16_t>(shift);
    ImGui::Checkbox("Show extended 128..255 key bank", &extended_keys_);
    ImGui::TextDisabled("Drag across an input keyboard to set the cut range. A/D: first key, Q/E: last key, W/S: transpose.");
    const int banks = extended_keys_ ? 2 : 1;
    for (int bank = 0; bank < banks; ++bank)
    {
        ImGui::PushID(bank);
        ImGui::Text("Output (%+d semitones)", int(map->transpose_val));
        const ImVec2 size(std::max(200.f, ImGui::GetContentRegionAvail().x), 66.f);
        const ImVec2 output_origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        draw_key_strip(*ImGui::GetWindowDrawList(), output_origin, size, *map, bank, true);
        ImGui::Text("Input %d..%d", bank * 128, bank * 128 + 127);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("key_keyboard", size, ImGuiButtonFlags_MouseButtonLeft);
        draw_key_strip(*ImGui::GetWindowDrawList(), origin, size, *map, bank, false);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            const int key = bank * 128 + std::clamp(int((ImGui::GetIO().MousePos.x - origin.x) / size.x * 128), 0, 127);
            if (ImGui::IsItemClicked()) cut_anchor_ = key;
            if (ImGui::IsItemActive() && cut_anchor_ >= 0)
            {
                map->min_val = static_cast<std::uint8_t>(std::min(cut_anchor_, key));
                map->max_val = static_cast<std::uint8_t>(std::max(cut_anchor_, key));
            }
            const auto result = map->process(static_cast<std::uint8_t>(key));
            if (result) ImGui::SetTooltip("%s (%d) -> %s (%u)", key_name(key).c_str(), key, key_name(*result).c_str(), unsigned(*result));
            else ImGui::SetTooltip("%s (%d) is removed", key_name(key).c_str(), key);
        }
        ImGui::PopID();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
    {
        const int low = int(map->min_val) + int(ImGui::IsKeyPressed(ImGuiKey_D)) - int(ImGui::IsKeyPressed(ImGuiKey_A));
        const int high = int(map->max_val) + int(ImGui::IsKeyPressed(ImGuiKey_E)) - int(ImGui::IsKeyPressed(ImGuiKey_Q));
        const int offset = int(map->transpose_val) + int(ImGui::IsKeyPressed(ImGuiKey_W)) - int(ImGui::IsKeyPressed(ImGuiKey_S));
        map->min_val = static_cast<std::uint8_t>(std::clamp(low, 0, 255));
        map->max_val = static_cast<std::uint8_t>(std::clamp(high, 0, 255));
        map->transpose_val = static_cast<std::int16_t>(std::clamp(offset, -255, 255));
    }
    int kept = 0;
    for (int key = 0; key <= 127; ++key) kept += map->process(static_cast<std::uint8_t>(key)).has_value();
    ImGui::Text("%d of 128 standard keys retained; output range %d..%d", kept,
        std::max(0, int(map->min_val) + map->transpose_val), std::min(255, int(map->max_val) + map->transpose_val));
    end_folded_window();
}

template<class Curve>
void mapping_panel::draw_curve(const char* title, const char* label, std::shared_ptr<Curve>& map,
    bool* open, curve_state& state, std::optional<Curve>& clipboard, int maximum)
{
    using key_type = typename Curve::key_type;
    using value_type = typename Curve::value_type;
    ImGui::SetNextWindowSize(ImVec2(690, 660), ImGuiCond_FirstUseEver);
    if (!begin_folded_window(title, open)) { end_folded_window(); return; }
    ImGui::TextUnformatted(label);
    bool enabled = bool(map);
    if (ImGui::Checkbox("Enable mapping", &enabled))
    {
        if (enabled) { map = std::make_shared<Curve>(); identity(*map, maximum); }
        else map.reset();
    }
    if (!map) { ImGui::TextDisabled("Values pass through unchanged."); end_folded_window(); return; }
    if (state.owner != map.get()) { state.owner = map.get(); state.first_x = -1; }
    if (ImGui::Button("Identity")) identity(*map, maximum);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { map->clear(); state.first_x = -1; }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) clipboard = *map;
    ImGui::SameLine();
    ImGui::BeginDisabled(!clipboard);
    if (ImGui::Button("Paste")) { *map = *clipboard; state.first_x = -1; }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Simplify")) simplify(*map);
    ImGui::SameLine();
    if (ImGui::Button("Trace"))
    {
        const auto original = *map;
        for (int i = 0; i <= maximum; ++i)
            map->insert(static_cast<key_type>(i), original.template evaluate_as<value_type>(
                static_cast<key_type>(i), dixelu::polyline_extrapolation::linear).value_or(static_cast<value_type>(i)));
    }
    ImGui::SetNextItemWidth(110);
    ImGui::InputFloat("Power", &state.degree, .1f, 1.f, "%.3f");
    ImGui::SameLine();
    if (maximum == 255) { ImGui::Checkbox("0..255", &state.extended); ImGui::SameLine(); }
    else { ImGui::Checkbox("Keep center", &state.symmetric); ImGui::SameLine(); }
    if (ImGui::Button("Generate power curve"))
    {
        const int limit = (maximum == 255 && !state.extended) ? 127 : maximum;
        state.degree = std::isfinite(state.degree) ? std::clamp(state.degree, .01f, 32.f) : 1.f;
        map->clear();
        for (int i = 0; i <= limit; ++i)
        {
            double value = std::pow(double(i) / limit, state.degree) * limit;
            if (maximum > 255 && state.symmetric)
            {
                const double center = 8192;
                const double range = i < center ? center : maximum - center;
                value = center + std::copysign(std::pow(std::abs(i - center) / range, state.degree) * range, i - center);
            }
            map->insert(static_cast<key_type>(i), static_cast<value_type>(std::clamp(std::ceil(value), 0., double(limit))));
        }
        simplify(*map);
    }
    ImGui::RadioButton("Single point", &state.mode, 0); ImGui::SameLine();
    ImGui::RadioButton("Two-point segment", &state.mode, 1); ImGui::SameLine();
    // Reserve the same toolbar row while a segment starts, completes or cancels.
    // Omitting this item leaves SameLine active for the instructions below.
    ImGui::BeginDisabled(state.first_x < 0);
    if (ImGui::SmallButton("Cancel segment")) state.first_x = -1;
    ImGui::EndDisabled();
    ImGui::TextDisabled("Click or drag to add points; segment mode replaces the interval. Right-click deletes the nearest point.");

    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(160.f, ImGui::GetContentRegionAvail().x), 310);
    ImGui::InvisibleButton("curve_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 far(origin.x + size.x, origin.y + size.y);
    const auto point = [&](double x, double y) { return ImVec2(origin.x + float(x / maximum) * size.x, far.y - float(y / maximum) * size.y); };
    draw->AddRectFilled(origin, far, IM_COL32(6, 19, 30, 255));
    if (maximum == 255)
        draw->AddRectFilled(point(0, 127), point(127, 0), IM_COL32(22, 53, 46, 180));
    for (int i = 1; i < 8; ++i)
    {
        draw->AddLine(ImVec2(origin.x + size.x * i / 8, origin.y), ImVec2(origin.x + size.x * i / 8, far.y), IM_COL32(33, 54, 73, 150));
        draw->AddLine(ImVec2(origin.x, origin.y + size.y * i / 8), ImVec2(far.x, origin.y + size.y * i / 8), IM_COL32(33, 54, 73, 150));
    }
    draw->AddLine(point(0, 0), point(maximum, maximum), IM_COL32(105, 130, 144, 90));
    draw->PushClipRect(origin, far, true);
    ImVec2 previous;
    bool have_previous = false;
    for (int pixel = 0, pixels = std::max(2, int(size.x)); pixel <= pixels; ++pixel)
    {
        const auto x = static_cast<key_type>(std::round(double(pixel) / pixels * maximum));
        const auto value = map->evaluate(x, dixelu::polyline_extrapolation::linear);
        const auto current = point(x, value.value_or(x));
        if (have_previous) draw->AddLine(previous, current, curve_color, 1.7f);
        previous = current; have_previous = true;
    }
    const auto mouse = ImGui::GetIO().MousePos;
    int nearest = -1;
    float nearest_distance = 100.f;
    for (const auto& [x, y] : map->points())
    {
        const auto p = point(x, y);
        const float distance = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
        if (distance < nearest_distance) { nearest_distance = distance; nearest = x; }
        if (map->size() < 1500) draw->AddCircleFilled(p, x == state.x ? 4.f : 2.5f, IM_COL32(220, 231, 240, 255));
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        const int x = std::clamp(int(std::round((mouse.x - origin.x) / size.x * maximum)), 0, maximum);
        const int y = std::clamp(int(std::round((far.y - mouse.y) / size.y * maximum)), 0, maximum);
        draw->AddLine(point(x, 0), point(x, maximum), IM_COL32(165, 189, 210, 95));
        draw->AddLine(point(0, y), point(maximum, y), IM_COL32(165, 189, 210, 95));
        if (state.first_x >= 0 && state.mode == 1) draw->AddLine(point(state.first_x, state.first_y), point(x, y), accent, 2.f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (nearest >= 0) (void)map->erase(static_cast<key_type>(nearest));
            state.first_x = -1;
        }
        if ((state.mode == 0 && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            || (state.mode == 1 && ImGui::IsItemClicked(ImGuiMouseButton_Left)))
        {
            state.x = x; state.y = y;
            if (state.mode == 1 && state.first_x < 0) { state.first_x = x; state.first_y = y; }
            else
            {
                if (state.mode == 1)
                {
                    std::vector<key_type> erase;
                    for (auto it = map->points().lower_bound(static_cast<key_type>(std::min(x, state.first_x)));
                        it != map->points().end() && it->first <= std::max(x, state.first_x); ++it) erase.push_back(it->first);
                    for (auto key : erase) (void)map->erase(key);
                    map->insert(static_cast<key_type>(state.first_x), static_cast<value_type>(state.first_y));
                    state.first_x = -1;
                }
                map->insert(static_cast<key_type>(x), static_cast<value_type>(y));
            }
        }
        ImGui::SetTooltip("Input %d / output %d%s", x, y, nearest >= 0 ? " (right-click to delete point)" : "");
    }
    draw->PopClipRect();
    draw->AddRect(origin, far, accent);
    ImGui::Text("Input 0..%d / output 0..%d | %zu control points", maximum, maximum, map->size());
    ImGui::SetNextItemWidth(100); ImGui::InputInt("Input", &state.x); ImGui::SameLine();
    ImGui::SetNextItemWidth(100); ImGui::InputInt("Output", &state.y); ImGui::SameLine();
    state.x = std::clamp(state.x, 0, maximum); state.y = std::clamp(state.y, 0, maximum);
    if (ImGui::Button("Set point")) map->insert(static_cast<key_type>(state.x), static_cast<value_type>(state.y));
    ImGui::SameLine();
    if (ImGui::Button("Delete point")) (void)map->erase(static_cast<key_type>(state.x));
    if (ImGui::CollapsingHeader("Control points"))
    {
        std::vector<typename Curve::point_type> points(map->points().begin(), map->points().end());
        if (ImGui::BeginChild("point_list", ImVec2(0, 140), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(points.size()));
            while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto [x, y] = points[i];
                const auto text = std::to_string(x) + " -> " + std::to_string(y);
                if (ImGui::Selectable(text.c_str(), state.x == x)) { state.x = x; state.y = y; }
            }
        }
        ImGui::EndChild();
    }
    end_folded_window();
}

void mapping_panel::draw_volume_map(const char* label, std::shared_ptr<volume_curve>& map, bool* open)
{
    draw_curve("Volume mapping", label, map, open, volume_, volume_clipboard_, 255);
}

void mapping_panel::draw_pitch_map(const char* label, std::shared_ptr<pitch_curve>& map, bool* open)
{
    draw_curve("Pitch bend mapping", label, map, open, pitch_, pitch_clipboard_, 16383);
}
}
