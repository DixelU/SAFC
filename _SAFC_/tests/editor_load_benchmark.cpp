#define NOMINMAX
#include <Windows.h>
#include "../SAFC_InnerModules/midi_editor.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
using clock_type = std::chrono::steady_clock;

void write_fixture(const std::filesystem::path& path, unsigned depth)
{
    std::vector<unsigned char> bytes;
    // Keep two pitches continuously active. Each key's FIFO repeatedly drains
    // its prefix, testing both ordinary short notes and sustained overlaps.
    for (unsigned i = 0; i < depth; ++i)
        bytes.insert(bytes.end(), {0, 0x90, 60, 100, 0, 0x91, 61, 90});
    for (unsigned i = 0; i < 500000; ++i)
    {
        const auto key = static_cast<unsigned char>(60 + (i & 1));
        const auto channel = static_cast<unsigned char>(i & 1);
        bytes.insert(bytes.end(), {1, static_cast<unsigned char>(0x90 | channel), key, 100,
            1, static_cast<unsigned char>(0x80 | channel), key, 0});
    }
    for (unsigned i = 0; i < depth; ++i)
        bytes.insert(bytes.end(), {0, 0x80, 60, 0, 0, 0x81, 61, 0});
    bytes.insert(bytes.end(), {0, 0xff, 0x2f, 0});
    std::ofstream file(path, std::ios::binary);
    const unsigned char header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,224};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write("MTrk", 4);
    for (int shift : {24, 16, 8, 0}) file.put(static_cast<char>(bytes.size() >> shift));
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file) throw std::runtime_error("Cannot write editor load fixture");
}

void benchmark(const std::filesystem::path& path, const char* name, int repetitions)
{
    auto editor = std::make_unique<midi_editor>();
    std::vector<double> times;
    std::uint64_t checksum = 0;
    for (int pass = -1; pass < repetitions; ++pass)
    {
        const auto start = clock_type::now();
        if (!editor->load_file(path.wstring())) throw std::runtime_error("Editor load failed");
        const auto elapsed = std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
        if (pass >= 0) times.push_back(elapsed);
        // Include stable identities as well as note content, outside timing.
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto& note : editor->get_all_notes())
            for (const std::uint64_t value : {note.start_tick, note.end_tick,
                std::uint64_t(note.id), std::uint64_t(note.track_index), std::uint64_t(note.key),
                std::uint64_t(note.velocity), std::uint64_t(note.channel)})
            {
                hash ^= value;
                hash *= 1099511628211ULL;
            }
        if (pass >= 0 && checksum != hash) throw std::runtime_error("Editor load is not deterministic");
        checksum = hash;
    }
    std::sort(times.begin(), times.end());
    std::cout << name << ',' << editor->get_note_count() << ',' << times[times.size() / 2]
        << ',' << checksum << '\n';
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        std::cout << std::fixed << std::setprecision(3) << "case,notes,median_load_ms,checksum\n";
        if (argc > 1)
            benchmark(argv[1], "file", 3);
        else
        {
            const auto path = std::filesystem::temp_directory_path() /
                (L"safc-editor-load-" + std::to_wstring(GetCurrentProcessId()) + L".mid");
            struct cleanup
            {
                std::filesystem::path path;
                ~cleanup() { std::error_code error; std::filesystem::remove(path, error); }
            } fixture{path};
            for (const unsigned depth : {0, 1, 64, 4096})
            {
                write_fixture(path, depth);
                const auto name = "overlap_" + std::to_string(depth);
                benchmark(path, name.c_str(), 7);
            }
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
