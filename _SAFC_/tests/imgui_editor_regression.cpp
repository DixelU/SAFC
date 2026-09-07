#define NOMINMAX
#include <Windows.h>

#include "../imgui/editor_panel.h"
#include "../imgui/playback_session.h"
#include "../imgui/folded_theme.h"
#include "../SAFC_InnerModules/midi_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    ~context_guard() { ImGui::DestroyContext(context); }
};
}

// This is a native ImGui input regression. It uses the production transport
// explicitly configured with its silent output; no physical MIDI or audio sink
// is opened. Geometry is taken from the generated ImGui child window rather
// than screen captures or fixed desktop coordinates.
int main()
{
    try
    {
        const auto directory = std::filesystem::current_path() /
            (L"imgui-editor-regression-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        const auto output = directory / L"editor-native-input.mid";
        safc::imgui_ui::playback_session playback;
        safc::imgui_ui::native_dialogs dialogs;
        dialogs.save_midi = [output](const std::wstring&) { return output.wstring(); };
        safc::imgui_ui::editor_panel editor(playback, std::move(dialogs));
        std::string report;
        const bool smoke_passed = editor.run_smoke(directory.wstring(), report);
        require(smoke_passed, report.c_str());
        std::cout << report << '\n';

        require(playback.open((directory / L"editor-input.mid").wstring(), true, true), "Silent output did not start.");
        const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!playback.snapshot().playing && std::chrono::steady_clock::now() < ready_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(playback.snapshot().playing && playback.snapshot().error.empty(), "Silent playback output did not become ready.");

        context_guard context;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1400, 1000};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int font_width, font_height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
        safc::imgui_ui::apply_theme();
        bool visible = true;
        auto frame = [&]
        {
            ImGui::NewFrame();
            editor.draw(&visible);
            ImGui::Render();
        };
        frame(); frame(); frame();
        require(ImGui::GetDrawData()->TotalVtxCount > 1000, "Editor did not render its native widgets and piano roll.");

        ImGuiWindow* roll = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(window->Name, "Roll panel")) roll = window;
        require(roll != nullptr, "Editor roll child window is absent.");
        const auto origin = roll->DC.CursorStartPos;
        const float size_x = roll->WorkRect.Max.x - origin.x;
        const float size_y = roll->WorkRect.Max.y - origin.y - 34.f;
        auto baseline = std::make_unique<midi_editor>();
        require(baseline->load_file((directory / L"editor-output.mid").wstring()), "Editor baseline could not be reloaded.");
        const auto initial_count = baseline->get_note_count();
        const int low = baseline->get_view_key_low(), high = baseline->get_view_key_high();
        const float key_height = (size_y - 24.f - 110.f - 6.f) / (high - low + 1);
        const auto duration = baseline->get_view_duration_ticks();
        const float note_width = size_x - 47.f;
        auto position = [&](double tick, int key)
        {
            return ImVec2(origin.x + 47.f + float(tick / double(duration)) * note_width,
                origin.y + 24.f + (high - key + .5f) * key_height);
        };
        auto drag = [&](ImVec2 from, ImVec2 to)
        {
            io.AddMousePosEvent(from.x, from.y); frame();
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMousePosEvent(to.x, to.y); frame();
            io.AddMouseButtonEvent(0, false); frame(); frame();
        };
        auto key = [&](ImGuiKey value, bool ctrl)
        {
            if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(value, true); frame();
            io.AddKeyEvent(value, false);
            if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };
        auto save = [&]
        {
            key(ImGuiKey_S, true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.busy() && std::chrono::steady_clock::now() < deadline)
            { std::this_thread::sleep_for(std::chrono::milliseconds(2)); frame(); }
            frame();
            require(!editor.busy() && !editor.has_unsaved_changes(), "Native Ctrl+S did not finish saving the document.");
            auto model = std::make_unique<midi_editor>();
            require(model->load_file(output.wstring()), "Native Ctrl+S did not save a readable MIDI.");
            return model;
        };

        const auto draw_at = position(double(duration) * .5, 76);
        drag(draw_at, {draw_at.x + 35.f, draw_at.y});
        auto saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native canvas draw did not insert one note.");
        midi_editor::piano_note drawn;
        require(saved->find_note_at(duration / 2, 76, drawn), "Native canvas draw used the wrong time or pitch.");
        saved.reset(); // Release the mapped output before the next atomic save.

        auto move_from = position(double(drawn.start_tick) + double(drawn.length()) * .25, 76);
        drag(move_from, {move_from.x + 30.f, move_from.y - key_height * 2.f});
        saved = save();
        const auto after_move = saved->get_all_notes();
        const auto moved = std::find_if(after_move.begin(), after_move.end(), [](const auto& value) { return value.key == 78; });
        require(moved != after_move.end() && moved->start_tick > drawn.start_tick, "Native canvas drag did not move time and pitch.");
        saved.reset();
        key(ImGuiKey_Z, true);
        saved = save();
        midi_editor::piano_note restored;
        require(saved->find_note_at(drawn.start_tick, drawn.key, restored) && restored.length() == drawn.length(), "Native Ctrl+Z did not restore the moved note.");
        saved.reset();
        key(ImGuiKey_Y, true);
        saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native Ctrl+Y changed note count.");
        saved.reset();
        key(ImGuiKey_Delete, false);
        saved = save();
        require(saved->get_note_count() == initial_count, "Native Delete did not erase the selected note.");
        saved.reset();
        key(ImGuiKey_Z, true);
        saved = save();
        require(saved->get_note_count() == initial_count + 1, "Native delete undo did not restore the note.");
        saved.reset();

        editor.shutdown();
        playback.shutdown();
        std::cout << "Native ImGui draw, move, keyboard undo/redo/delete, atomic Save and silent transport passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor regression failed: " << error.what() << '\n';
        return 1;
    }
}
