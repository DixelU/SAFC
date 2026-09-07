#include "analysis_panel.h"
#include "mapping_panel.h"
#include "folded_theme.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }

template<class Predicate>
void wait_for(Predicate&& predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() > end) throw std::runtime_error("Timed out waiting for owned worker.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void write_fixture(const std::filesystem::path& path, bool invalid_division = false)
{
    // 120 BPM, one note; at tick 480 change to 60 BPM and add another
    // note; releases at 720 and 960. Duration 1.5s, peak polyphony/NPS 2.
    const unsigned char bytes[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,0xE0,
        'M','T','r','k', 0,0,0,37,
        0,0xFF,0x51,3,7,0xA1,0x20,
        0,0x90,60,100,
        0x83,0x60,0xFF,0x51,3,0x0F,0x42,0x40,
        0,0x90,64,100,
        0x81,0x70,0x80,60,0,
        0x81,0x70,0x80,64,0,
        0,0xFF,0x2F,0
    };
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    if (invalid_division) { output.seekp(12); output.put(0); output.put(0); }
}

void write_cancellation_fixture(const std::filesystem::path& path)
{
    constexpr unsigned count = 100000, size = count * 8 + 4;
    const unsigned char header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0,
        'M','T','r','k', static_cast<unsigned char>(size >> 24), static_cast<unsigned char>(size >> 16),
        static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size)};
    const unsigned char notes[] = {1,0x90,60,100,1,0x80,60,0};
    const unsigned char end[] = {0,0xFF,0x2F,0};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (unsigned i = 0; i < count; ++i) output.write(reinterpret_cast<const char*>(notes), sizeof(notes));
    output.write(reinterpret_cast<const char*>(end), sizeof(end));
}
}

int main(int argc, char** argv)
{
    using namespace safc::imgui_ui;
    try
    {
        const auto directory = std::filesystem::path(argc > 1 ? argv[1] : "analysis-regression-output");
        std::filesystem::create_directories(directory);
        const auto path = directory / "tempo-change.mid";
        write_fixture(path);
        analysis_panel panel;
        panel.open_file(path.wstring());
        wait_for([&] { return !panel.busy(); });
        const auto result = panel.result();
        require(bool(result), panel.status().c_str());
        require(result->source->tracks.size() == 1, "Wrong track count.");
        require(result->source->ppq == 480, "Wrong PPQN.");
        require(result->last_tick == 960, "Wrong final tick.");
        require(std::abs(result->duration_seconds - 1.5) < 1e-9, "Wrong duration across tempo changes.");
        require(result->peak_polyphony == 2 && result->peak_nps == 2, "Wrong polyphony/NPS peaks.");
        require(std::abs(result->ticks_to_seconds(720) - 1.) < 1e-9, "Tick to time did not integrate tempo changes.");
        require(result->seconds_to_ticks(1.) == 720, "Time to tick did not integrate tempo changes.");
        require(result->seconds_to_ticks(2.5) == 1440, "Time extrapolation beyond final tempo failed.");
        for (std::int64_t tick = 0; tick <= 1440; ++tick)
            require(result->seconds_to_ticks(result->ticks_to_seconds(tick)) == tick, "Tick/time roundtrip lost a tick to floating-point rounding.");
        require(!result->source->internal_time_map.empty(), "Time map missing.");

        const auto csv = directory / "combined.csv";
        require(panel.export_to(csv.wstring(), analysis_panel::export_kind::combined_csv, ","), "Could not start CSV export.");
        wait_for([&] { return !panel.export_busy(); });
        require(panel.export_status().starts_with("Export saved:"), panel.export_status().c_str());
        std::ifstream input(csv);
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        require(text.starts_with("Tick,Polyphony,Time(seconds),Tempo\n"), "Combined CSV format changed.");
        require(text.find("-1,0,0,120\n") != std::string::npos, "Initial synthetic CSV row must retain time zero.");
        require(text.find("720,1,1,60\n") != std::string::npos, "Combined CSV has an incorrect tempo boundary row.");
        require(text.find("960,0,1.5,60\n") != std::string::npos, "Combined CSV tail is missing.");
        input.close();
        // Verify atomic overwrite of an existing destination as used by Save As.
        require(panel.export_to(csv.wstring(), analysis_panel::export_kind::tempo_csv), "Could not start overwrite export.");
        wait_for([&] { return !panel.export_busy(); });
        require(panel.export_status().starts_with("Export saved:"), panel.export_status().c_str());

        const auto binary = directory / "combined.atraw";
        require(panel.export_to(binary.wstring(), analysis_panel::export_kind::combined_binary), "Could not start ATRAW export.");
        wait_for([&] { return !panel.export_busy(); });
        require(panel.export_status().starts_with("Export saved:"), panel.export_status().c_str());
        require(std::filesystem::file_size(binary) % 32 == 0 && std::filesystem::file_size(binary) >= 160, "ATRAW row size changed.");
        std::ifstream raw(binary, std::ios::binary);
        bool found = false;
        while (raw)
        {
            std::int64_t tick = 0, poly = 0;
            double seconds = 0, tempo = 0;
            raw.read(reinterpret_cast<char*>(&tick), 8); raw.read(reinterpret_cast<char*>(&poly), 8);
            raw.read(reinterpret_cast<char*>(&seconds), 8); raw.read(reinterpret_cast<char*>(&tempo), 8);
            if (raw && tick == 720) { found = poly == 1 && seconds == 1. && tempo == 60.; }
        }
        require(found, "ATRAW values differ from CSV.");

        const auto midi_size = std::filesystem::file_size(path);
        require(panel.export_to(path.wstring(), analysis_panel::export_kind::combined_csv), "Could not start input-collision validation.");
        wait_for([&] { return !panel.export_busy(); });
        require(panel.export_status().find("must not overwrite") != std::string::npos,
            "Analysis export was allowed to overwrite the source MIDI.");
        require(std::filesystem::file_size(path) == midi_size, "Rejected analysis export changed the source MIDI.");

        // Exercise each native panel with the real ImGui layout/draw pipeline,
        // without requiring a GL context, display or audio device.
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = ImVec2(1920, 1200); io.DeltaTime = 1.f / 60;
        unsigned char* pixels = nullptr; int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        mapping_panel mappings;
        auto keys = std::make_shared<::cut_and_transpose>(0, 127, 0);
        auto volume = std::make_shared<mapping_panel::volume_curve>();
        volume->insert(0, 0); volume->insert(127, 127);
        auto pitch = std::make_shared<mapping_panel::pitch_curve>();
        pitch->insert(0, 0); pitch->insert(8192, 8192); pitch->insert(16383, 16383);
        bool opened = true;
        for (int frame = 0; frame < 3; ++frame)
        {
            apply_theme(); ImGui::NewFrame();
            panel.draw(&opened);
            mappings.draw_key_map("Fixture MIDI", keys, &opened);
            mappings.draw_volume_map("Fixture MIDI", volume, &opened);
            mappings.draw_pitch_map("Fixture MIDI", pitch, &opened);
            ImGui::Render();
            require(ImGui::GetDrawData()->TotalVtxCount > 0, "Native panels emitted no geometry.");
            for (const auto* list : ImGui::GetDrawData()->CmdLists)
                for (const auto& vertex : list->VtxBuffer)
                    require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y), "A graph produced non-finite geometry.");
        }
        ImGui::DestroyContext();
        require(keys->process(127).value_or(0) == 127, "Rendering unexpectedly changed key transform.");
        require(volume->evaluate_as<std::uint8_t>(127).value_or(0) == 127, "Rendering unexpectedly changed volume map.");
        require(pitch->evaluate_as<std::uint16_t>(8192).value_or(0) == 8192, "Pitch center 8192 became an invalid sentinel.");

        const auto cancellation_path = directory / "cancel-analysis.mid";
        write_cancellation_fixture(cancellation_path);
        panel.open_file(cancellation_path.wstring()); panel.cancel();
        wait_for([&] { return !panel.busy(); });
        require(!panel.result(), "Cancelled analysis published partial data.");
        panel.open_file(path.wstring());
        wait_for([&] { return !panel.busy(); });
        require(bool(panel.result()), "Reopening after cancellation failed.");
        const auto invalid = directory / "invalid-division.mid";
        write_fixture(invalid, true);
        panel.open_file(invalid.wstring());
        wait_for([&] { return !panel.busy(); });
        require(!panel.result() && panel.status().find("nonzero") != std::string::npos, "Invalid MIDI time division was accepted.");
        panel.open_file((directory / "missing.mid").wstring());
        wait_for([&] { return !panel.busy(); });
        require(!panel.result(), "Missing MIDI was accepted.");
        panel.shutdown();
        std::cout << "Analysis/mapping regression passed: tempo integration, exact graph peaks, CSV/ATRAW, overwrite, native draw, cancel/reopen and invalid input.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
