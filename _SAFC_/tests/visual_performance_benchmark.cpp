#define NOMINMAX
#include <Windows.h>
#include "../SAFC_InnerModules/simple_player.h"
#include "../imgui/editor_panel.h"
#include "../imgui/playback_session.h"
#include "../imgui/folded_theme.h"
#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
using clock_type = std::chrono::steady_clock;

template<class Function>
double median_ms(Function&& run, int repetitions = 15)
{
    run();
    std::vector<double> times;
    for (int i = 0; i < repetitions; ++i)
    {
        const auto start = clock_type::now();
        run();
        times.push_back(std::chrono::duration<double, std::milli>(clock_type::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

void player_overlap_benchmark()
{
    auto player = std::make_unique<simple_player>();
    auto data = std::make_unique<simple_player::draw_data>();
    for (const int count : {1000, 10000, 50000})
    {
        data->key_note_spans.clear();
        for (int i = 0; i < count; ++i)
            data->key_note_spans.push_back({float(i * 2), float(i * 2 + 1), 0, unsigned(i), false});
        std::uint64_t checksum = 0;
        const auto ms = median_ms([&]
        {
            checksum = 0;
            player->overlaps_removal_v0(*data, [&](float begin, float end, std::uint32_t color)
            { checksum += std::uint64_t(begin + end) + color; });
        });
        std::cout << "player_overlap," << count << ',' << ms << ',' << checksum << '\n';
    }
}

void write_fixture(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    const unsigned char header[] = {'M','T','h','d',0,0,0,6,0,1,0,16,1,224};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (int track = 0; track < 16; ++track)
    {
        std::vector<unsigned char> bytes;
        for (int i = 0; i < 4096; ++i)
        {
            const auto key = static_cast<unsigned char>(24 + (i * 7 + track) % 84);
            bytes.insert(bytes.end(), {1, 0x90, key, 100, 1, 0x80, key, 0});
        }
        bytes.insert(bytes.end(), {0, 0xff, 0x2f, 0});
        file.write("MTrk", 4);
        for (int shift : {24, 16, 8, 0}) file.put(static_cast<char>(bytes.size() >> shift));
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    if (!file) throw std::runtime_error("Cannot write benchmark MIDI");
}

void editor_benchmark(const std::filesystem::path& path)
{
    namespace ui = safc::imgui_ui;
    ImGui::CreateContext();
    struct context_guard { ~context_guard() { ImGui::DestroyContext(); } } context_lifetime;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1400, 1000};
    io.DeltaTime = 1.f / 60.f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::apply_theme();
    ui::playback_session playback;
    ui::editor_panel editor(playback);
    editor.open_file(path.wstring());
    const auto deadline = clock_type::now() + std::chrono::minutes(3);
    while (editor.busy())
    {
        if (clock_type::now() > deadline) throw std::runtime_error("Editor load timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    editor.poll();
    bool open = true;
    auto frame = [&]
    {
        ImGui::NewFrame();
        editor.draw(&open);
        ImGui::Render();
    };
    frame(); frame(); frame();
    const auto ms = median_ms(frame);
    std::cout << "editor_frame," << ImGui::GetDrawData()->TotalVtxCount << ',' << ms << ','
        << ImGui::GetDrawData()->TotalIdxCount << '\n';
    editor.shutdown();
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        std::cout << std::fixed << std::setprecision(4) << "case,items,median_ms,checksum_or_indices\n";
        player_overlap_benchmark();
        const auto path = argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::current_path() / "visual-benchmark.mid";
        if (argc == 1) write_fixture(path);
        editor_benchmark(path);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
