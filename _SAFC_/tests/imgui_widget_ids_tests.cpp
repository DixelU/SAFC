#define NOMINMAX
#include <Windows.h>

#include "imgui_id_audit.h"
#include "../imgui/widgets.h"
#include "../imgui/folded_theme.h"
#include "../imgui/project_panel.h"
#include "../imgui/analysis_panel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace ui = safc::imgui_ui;
using safc::imgui_test::probe_item_id;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    context_guard()
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1400, 1100};
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::apply_theme();
    }
    ~context_guard() { ImGui::DestroyContext(context); }
};

template<class Draw>
void frame(Draw&& draw)
{
    ImGui::NewFrame();
    draw();
    ImGui::Render();
}

template<class Draw>
void click(ImVec2 point, Draw&& draw)
{
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(point.x, point.y); frame(draw);
    io.AddMouseButtonEvent(0, true); frame(draw);
    io.AddMouseButtonEvent(0, false); frame(draw);
    frame(draw);
}

ImGuiID scoped_int(ImGuiID seed, int value)
{
    return ImHashData(&value, sizeof(value), seed);
}

void check_detector()
{
    bool enabled = true;
    ImGuiID checkbox_id = 0;
    bool scoped = false;
    const auto draw = [&]
    {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 400});
        ImGui::Begin("Detector negative control");
        checkbox_id = ImGui::GetID("Polyphony");
        ImGui::Checkbox("Polyphony", &enabled);
        if (scoped) ImGui::PushID("graphs");
        ImGui::InvisibleButton("Polyphony", {300, 120});
        if (scoped) ImGui::PopID();
        ImGui::End();
    };
    frame(draw); frame(draw);
    require(probe_item_id(checkbox_id, draw) == 2,
        "ID detector did not catch the original checkbox/canvas collision.");
    scoped = true;
    require(probe_item_id(checkbox_id, draw) == 1,
        "ID detector did not distinguish the scoped canvas from the checkbox.");
}

void check_literal_rows()
{
    std::array<std::string, 6> names{
        "Identical MIDI output", "Identical MIDI output", "MIDI##device###suffix", "###",
        "Multiline\nMIDI endpoint", "Following row"};
    std::array<ImGuiID, 6> ids{};
    std::array<ImRect, 6> rectangles{};
    std::array<ImVec2, 6> text_positions{};
    std::array<int, 6> text_vertices{};
    int selected = -1;
    const auto draw = [&]
    {
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({600, 350});
        ImGui::Begin("MIDI endpoints");
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
        {
            ImGui::PushID(i);
            auto* list = ImGui::GetWindowDrawList();
            const int first_vertex = list->VtxBuffer.Size;
            text_positions[i] = ImGui::GetCursorScreenPos();
            if (ui::literal_selectable("##device", names[i], selected == i)) selected = i;
            ids[i] = ImGui::GetItemID();
            rectangles[i] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            text_vertices[i] = 0;
            const auto text_color = ImGui::GetColorU32(ImGuiCol_Text);
            for (int vertex = first_vertex; vertex < list->VtxBuffer.Size; ++vertex)
                if (list->VtxBuffer[vertex].col == text_color) ++text_vertices[i];
            ImGui::PopID();
        }
        ImGui::End();
    };
    frame(draw); frame(draw); frame(draw);
    require(ids[0] != ids[1], "Duplicate endpoint names share an item ID.");
    for (const auto id : ids)
        require(probe_item_id(id, draw) == 1, "A literal endpoint row shares an ID with another visible item.");
    require(text_vertices[2] == static_cast<int>(names[2].size()) * 4,
        "The literal row renderer hides the ##/### suffix.");
    require(text_vertices[3] == 12, "A name made only of ### is invisible.");
    const float multiline_height = ImGui::CalcTextSize(names[4].c_str(), nullptr, false).y;
    require(text_positions[5].y >= text_positions[4].y + multiline_height,
        "The row following a multiline name overlaps its text.");
    require(rectangles[4].Max.y >= text_positions[4].y + multiline_height,
        "A multiline row's native selection rectangle does not cover its text.");
    click(rectangles[1].GetCenter(), draw);
    require(selected == 1, "Clicking the second endpoint selected the wrong duplicate-named row.");
    click(rectangles[2].GetCenter(), draw);
    require(selected == 2, "A name containing ##/### does not accept native row input.");
    const auto original = ids;
    names[0] = "Renamed endpoint###same-tail";
    names[1] = "Another endpoint###same-tail";
    frame(draw);
    require(ids == original, "Changing the displayed device name changes its stable row ID.");
}

void check_literal_combo()
{
    std::array<std::string, 2> names{"MIDI##device###suffix", "Second###MIDI##endpoint"};
    std::array<ImRect, 2> rows{};
    int selected = 0;
    bool popup_open = false;
    ImRect combo;
    ImGuiID combo_id = 0;
    int preview_vertices = 0;
    float preview_right = 0.f;
    const auto draw = [&]
    {
        ImGui::SetNextWindowPos({650, 10});
        ImGui::SetNextWindowSize({600, 350});
        ImGui::Begin("Literal combo preview");
        ImGui::SetNextItemWidth(350.f);
        const auto position = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        combo = {position, {position.x + 350.f, position.y + height}};
        combo_id = ImGui::GetID("##output");
        auto* parent_draw = ImGui::GetWindowDrawList();
        const int first_vertex = parent_draw->VtxBuffer.Size;
        popup_open = ui::begin_literal_combo("##output", names[selected]);
        preview_vertices = 0;
        preview_right = position.x;
        const auto text_color = ImGui::GetColorU32(ImGuiCol_Text);
        const auto white_pixel = ImGui::GetIO().Fonts->TexUvWhitePixel;
        for (int index = first_vertex; index < parent_draw->VtxBuffer.Size; ++index)
        {
            const auto& vertex = parent_draw->VtxBuffer[index];
            // The native arrow also uses the text color, but its solid
            // triangle samples the white pixel rather than a font glyph.
            if (vertex.col == text_color &&
                (vertex.uv.x != white_pixel.x || vertex.uv.y != white_pixel.y))
            {
                ++preview_vertices;
                preview_right = (std::max)(preview_right, vertex.pos.x);
            }
        }
        if (popup_open)
        {
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                ImGui::PushID(i);
                if (ui::literal_selectable("##device", names[i], selected == i)) selected = i;
                rows[i] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::End();
    };
    frame(draw); frame(draw); frame(draw);
    require(preview_vertices == static_cast<int>(names[selected].size()) * 4,
        "Closed combo preview hides its literal ##/### suffix.");
    require(probe_item_id(combo_id, draw) == 1, "Literal combo has a conflicting widget ID.");
    click(combo.GetCenter(), draw);
    require(popup_open, "Literal combo did not open its native popup.");
    require(preview_vertices == static_cast<int>(names[selected].size()) * 4,
        "Open combo preview is truncated or drawn into the popup instead of its parent.");
    click(rows[1].GetCenter(), draw);
    require(selected == 1 && !popup_open, "Native combo row click did not select and close the popup.");
    require(preview_vertices == static_cast<int>(names[selected].size()) * 4,
        "Updated combo preview hides its literal ##/### suffix.");
    names[selected] = std::string(100, 'W') + "###tail";
    frame(draw);
    require(preview_vertices > 0 && preview_right <= combo.Max.x - ImGui::GetFrameHeight(),
        "Long combo preview paints over the native arrow button.");
}

std::filesystem::path write_midi_fixture()
{
    const auto directory = std::filesystem::current_path() /
        (L"imgui-widget-ids-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / L"same##name###tail.mid";
    constexpr unsigned char bytes[] = {
        'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,13,
        0,0x90,60,100,120,0x80,60,0,0,0xff,0x2f,0};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    require(static_cast<bool>(output), "Could not create the project ID test fixture.");
    return path;
}

void check_project_rows_and_properties()
{
    ui::project_session project;
    const auto path = write_midi_fixture();
    project.add_files({path.wstring(), path.wstring()});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (project.loading() && std::chrono::steady_clock::now() < deadline)
    { project.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    project.poll();
    require(project.data.files.size() == 2, "Duplicate project fixtures did not load.");
    const int first_file = static_cast<int>(project.id_at(0));
    const int second_file = static_cast<int>(project.id_at(1));
    require(first_file != second_file, "Duplicate project entries do not have distinct stable IDs.");
    ui::analysis_panel analysis;
    ui::project_panel panel(project, analysis, {});
    bool visible = true;
    const auto draw = [&] { panel.draw(&visible); };
    frame(draw); frame(draw); frame(draw);

    ImGuiWindow* properties = nullptr;
    ImGuiTable* files = nullptr;
    for (auto* window : GImGui->Windows)
        if (std::strstr(window->Name, "SAFC project/") && std::strstr(window->Name, "/Properties"))
            properties = window;
    for (int i = 0; i < GImGui->Tables.GetMapSize(); ++i)
        if (auto* table = GImGui->Tables.TryGetMapData(i); table && table->ColumnsCount == 4 &&
            table->InnerWindow && std::strstr(table->InnerWindow->Name, "MIDI files")) files = table;
    require(properties && files, "Project properties or files table was not submitted.");
    const auto row_id = [&](int file)
    { return ImHashStr("##file", 0, scoped_int(files->ID, file)); };
    require(probe_item_id(row_id(first_file), draw) == 1, "First duplicate filename row has an invalid or conflicting ID.");
    require(probe_item_id(row_id(second_file), draw) == 1, "Second duplicate filename row has an invalid or conflicting ID.");

    const auto play_id = [&](int file)
    { return ImHashStr("Play", 0, scoped_int(properties->ID, file)); };
    const auto ppqn_id = [&](int file)
    {
        auto seed = ImHashStr("File timing", 0, scoped_int(properties->ID, file));
        seed = ImHashStr("PPQN", 0, seed);
        return ImHashStr("##value", 0, seed);
    };
    require(probe_item_id(play_id(first_file), draw) == 1, "The first file's properties lack a stable file ID scope.");
    require(probe_item_id(ppqn_id(first_file), draw) == 1, "The first file's editable PPQN lacks a stable file ID scope.");
    require(probe_item_id(play_id(second_file), draw) == 0, "Unselected file properties share the selected file's IDs.");

    const ImVec2 second_row{files->WorkRect.Min.x + 30.f, (files->RowPosY1 + files->RowPosY2) * .5f};
    click(second_row, draw);
    require(probe_item_id(play_id(second_file), draw) == 1, "Native project row selection did not switch the properties' file ID.");
    require(probe_item_id(ppqn_id(second_file), draw) == 1, "Editable PPQN reused the previously selected file's ID.");
    require(probe_item_id(play_id(first_file), draw) == 0, "Previous file controls remain active in the new file's properties.");
    project.shutdown();
    analysis.shutdown();
}
}

int main()
{
    try
    {
        context_guard context;
        check_detector();
        check_literal_rows();
        check_literal_combo();
        check_project_rows_and_properties();
        std::cout << "PASS: conflict detector negative control, duplicate endpoint names, literal ##/### labels, "
            "multiline row layout, stable renamed row IDs, literal combo previews, native row clicks, "
            "duplicate project files, and per-file property IDs\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
