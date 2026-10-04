#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../SAFC_InnerModules/midi_track_collapse_handler.h"

namespace
{
using bytes = std::vector<std::uint8_t>;
using handler = midi_track_collapse_handler;
namespace fs = std::filesystem;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void append_u32(bytes& data, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        data.push_back(static_cast<std::uint8_t>(value >> shift));
}

bytes header(std::uint16_t tracks)
{
    return {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1,
        static_cast<std::uint8_t>(tracks >> 8), static_cast<std::uint8_t>(tracks), 0x01, 0xE0};
}

// Track payload from raw event bytes, with EOT appended
bytes track(bytes events)
{
    bytes chunk{'M', 'T', 'r', 'k'};
    events.insert(events.end(), {0x00, 0xFF, 0x2F, 0x00});
    append_u32(chunk, static_cast<std::uint32_t>(events.size()));
    chunk.insert(chunk.end(), events.begin(), events.end());
    return chunk;
}

bytes midi(const std::vector<bytes>& tracks)
{
    auto data = header(static_cast<std::uint16_t>(tracks.size()));
    for (const auto& item : tracks)
        data.insert(data.end(), item.begin(), item.end());
    return data;
}

void write_file(const fs::path& path, const bytes& data)
{
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

bytes read_file(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "collapsed MIDI must exist");
    return bytes(std::istreambuf_iterator<char>(input), {});
}

std::uint64_t collapse(const fs::path& directory, const bytes& input, const handler::options& options)
{
    const auto source = directory / "input.mid";
    write_file(source, input);
    return handler::collapse(source, handler::scan_tracks(source), directory / "output.mid", options);
}

void test_merge_order(const fs::path& directory)
{
    const auto input = midi({
        track({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
            0x00, 0x90, 0x3C, 0x64,
            0x05, 0xFF, 0x01, 0x02, 'h', 'i',
            0x05, 0x80, 0x3C, 0x40}),
        track({0x00, 0x91, 0x3D, 0x64,
            0x05, 0xF0, 0x03, 0x7E, 0x7F, 0xF7,
            0x05, 0x81, 0x3D, 0x40}),
        track({})});

    // Equal ticks keep the order of their tracks
    const auto expected = midi({track({
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0x90, 0x3C, 0x64,
        0x00, 0x91, 0x3D, 0x64,
        0x05, 0xFF, 0x01, 0x02, 'h', 'i',
        0x00, 0xF0, 0x03, 0x7E, 0x7F, 0xF7,
        0x05, 0x80, 0x3C, 0x40,
        0x00, 0x81, 0x3D, 0x40})});

    handler::options options;
    std::uint64_t reported = 0, total = 0;
    options.progress = [&](std::uint64_t done, std::uint64_t all) { reported = done; total = all; };
    check(collapse(directory, input, options) == 1, "two tracks collapse into one");
    check(read_file(directory / "output.mid") == expected, "events merge by tick, then by track");
    check(total && reported == total, "progress reaches the total");

    options.remove_empty_tracks = true;
    check(collapse(directory, midi({track({}), track({})}), options) == 0, "empty input leaves no tracks");
    options.remove_empty_tracks = false;
    check(collapse(directory, midi({track({}), track({})}), options) == 1 &&
        read_file(directory / "output.mid") == midi({track({})}), "kept empty output is one empty track");
}

void test_running_status(const fs::path& directory)
{
    // Inputs may use running status; offs here are velocity-zero note-ons
    const auto input = midi({
        track({0x00, 0x90, 0x3C, 0x64, 0x00, 0x3E, 0x64, 0x0A, 0x3C, 0x00, 0x00, 0x3E, 0x00}),
        track({0x05, 0x90, 0x40, 0x64, 0x0A, 0x40, 0x00})});

    handler::options options;
    options.running_status = true;
    check(collapse(directory, input, options) == 1, "running status collapses into one track");
    check(read_file(directory / "output.mid") == midi({track({
        0x00, 0x90, 0x3C, 0x64, 0x00, 0x3E, 0x64,
        0x05, 0x40, 0x64,
        0x05, 0x3C, 0x00, 0x00, 0x3E, 0x00,
        0x05, 0x40, 0x00})}),
        "the output reuses running status across merged tracks");

    options.running_status = false;
    collapse(directory, input, options);
    check(read_file(directory / "output.mid") == midi({track({
        0x00, 0x90, 0x3C, 0x64, 0x00, 0x90, 0x3E, 0x64,
        0x05, 0x90, 0x40, 0x64,
        0x05, 0x90, 0x3C, 0x00, 0x00, 0x90, 0x3E, 0x00,
        0x05, 0x90, 0x40, 0x00})}),
        "without compression every status is written");
}

void test_channel_split(const fs::path& directory)
{
    const auto input = midi({
        track({0x00, 0xFF, 0x03, 0x01, 'A', 0x00, 0x92, 0x3C, 0x64, 0x0A, 0x82, 0x3C, 0x40}),
        track({0x05, 0x95, 0x30, 0x64, 0x00, 0xFF, 0x01, 0x01, 'B', 0x0A, 0x85, 0x30, 0x40})});

    handler::options options;
    options.channel_split = true;
    options.remove_empty_tracks = true;
    check(collapse(directory, input, options) == 3, "channels 2 and 5 and the late meta get tracks");
    // As the in-track channel split: metas before the first channel event go
    // with that channel, later ones into track 0.
    check(read_file(directory / "output.mid") == [] {
        auto expected = header(3);
        for (const auto& item : {
            track({0x05, 0xFF, 0x01, 0x01, 'B'}),
            track({0x00, 0xFF, 0x03, 0x01, 'A', 0x00, 0x92, 0x3C, 0x64, 0x0A, 0x82, 0x3C, 0x40}),
            track({0x05, 0x95, 0x30, 0x64, 0x0A, 0x85, 0x30, 0x40})})
            expected.insert(expected.end(), item.begin(), item.end());
        return expected;
    }(), "channel tracks keep their events and order");

    options.remove_empty_tracks = false;
    check(collapse(directory, input, options) == 16, "every channel track is kept when empty ones are");
    check(handler::scan_tracks(directory / "output.mid").size() == 16, "16 complete chunks are written");
}

void test_delta_overflow(const fs::path& directory)
{
    // 2^28 + 5 needs a five-byte VLV
    const auto input = midi({track({0x81, 0x80, 0x80, 0x80, 0x05, 0x90, 0x3C, 0x64, 0x00, 0x80, 0x3C, 0x40})});

    handler::options options;
    options.delta_overflow_correction = true;
    collapse(directory, input, options);
    check(read_file(directory / "output.mid") == midi({track({
        0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x7F, 0x01, 0x00,
        0x06, 0x90, 0x3C, 0x64, 0x00, 0x80, 0x3C, 0x40})}),
        "corrected deltas are split with a dummy event");

    options.delta_overflow_correction = false;
    collapse(directory, input, options);
    check(read_file(directory / "output.mid") == input, "uncorrected deltas pass through");
}

struct note_event
{
    std::uint64_t tick;
    std::uint8_t status, key, velocity;
    bool operator==(const note_event&) const = default;
};

void test_track_size_limit(const fs::path& directory)
{
    // Overlapping notes, so held notes cross every track edge
    std::vector<note_event> notes;
    for (std::uint8_t i = 0; i < 40; ++i)
    {
        notes.push_back({i * 4u, 0x90, static_cast<std::uint8_t>(40 + i % 8), 100});
        notes.push_back({i * 4u + 10, 0x80, static_cast<std::uint8_t>(40 + i % 8), 64});
    }
    std::stable_sort(notes.begin(), notes.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
    bytes events;
    std::uint64_t previous = 0;
    for (const auto& note : notes)
    {
        events.insert(events.end(), {static_cast<std::uint8_t>(note.tick - previous), note.status, note.key, note.velocity});
        previous = note.tick;
    }

    handler::options options;
    options.max_track_size = 64;
    const auto count = collapse(directory, midi({track(events)}), options);
    check(count > 3, "output past the limit continues in more tracks");

    const auto output = read_file(directory / "output.mid");
    const auto extents = handler::scan_tracks(directory / "output.mid");
    check(extents.size() == count, "every track is a complete chunk");

    std::vector<note_event> restored;
    std::uint64_t end = 14;
    for (const auto& extent : extents)
    {
        check(extent.size <= options.max_track_size, "tracks respect the size limit");
        std::uint64_t tick = 0;
        std::array<int, 128> held{};
        auto position = extent.offset;
        for (;;)
        {
            std::uint64_t delta = 0;
            do
                delta = (delta << 7) | (output[position] & 0x7F);
            while (output[position++] & 0x80);
            tick += delta;
            const auto status = output[position];
            if (status == 0xFF)
            {
                check(output[position + 1] == 0x2F && position + 3 == extent.offset + extent.size,
                    "EOT ends every track");
                break;
            }
            const note_event note{tick, status, output[position + 1], output[position + 2]};
            position += 3;
            held[note.key] += note.status == 0x90 ? 1 : -1;
            check(held[note.key] >= 0, "every off follows its on within the track");
            // Edges close held notes with velocity 0 and reopen them with velocity 1
            if (!(note.status == 0x80 && note.velocity == 0) && !(note.status == 0x90 && note.velocity == 1))
                restored.push_back(note);
        }
        check(std::all_of(held.begin(), held.end(), [](int value) { return value == 0; }),
            "no note stays on past the end of a track");
        end = extent.offset + extent.size;
    }
    check(end == output.size(), "nothing follows the last track");
    check(restored == notes, "events survive the split unchanged and in order");
}

void test_cancellation(const fs::path& directory)
{
    std::vector<bytes> tracks;
    for (int channel = 0; channel < 4; ++channel)
    {
        bytes events;
        for (int i = 0; i < 200000; ++i)
            events.insert(events.end(), {0x01, static_cast<std::uint8_t>(0x90 | channel), 0x3C, 0x64,
                0x01, static_cast<std::uint8_t>(0x80 | channel), 0x3C, 0x40});
        tracks.push_back(track(events));
    }

    std::atomic_bool cancel{false};
    handler::options options;
    options.channel_split = true;
    options.remove_empty_tracks = true;
    options.cancel = &cancel;
    // Halfway through, channel tracks are being written to their temporary files
    options.progress = [&](std::uint64_t done, std::uint64_t total) { cancel = done * 2 >= total; };
    bool cancelled = false;
    try
    {
        collapse(directory, midi(tracks), options);
    }
    catch (const midi_processing_cancelled&)
    {
        cancelled = true;
    }
    check(cancelled, "a cancelled collapse stops");

    options.cancel = nullptr;
    options.progress = nullptr;
    check(collapse(directory, midi(tracks), options) == 4, "a full collapse splits the four channels");
}

void test_in_place(const fs::path& directory)
{
    const auto path = directory / "in-place.mid";
    const auto input = midi({
        track({0x00, 0x90, 0x3C, 0x64, 0x0A, 0x80, 0x3C, 0x40}),
        track({0x05, 0x91, 0x3D, 0x64, 0x0A, 0x81, 0x3D, 0x40})});
    write_file(path, input);

    std::atomic_bool cancel{true};
    handler::options options;
    options.cancel = &cancel;
    bool cancelled = false;
    try
    {
        handler::collapse_in_place(path, handler::scan_tracks(path), options);
    }
    catch (const midi_processing_cancelled&)
    {
        cancelled = true;
    }
    check(cancelled && read_file(path) == input, "a failed collapse leaves the processed MIDI in place");

    options.cancel = nullptr;
    check(handler::collapse_in_place(path, handler::scan_tracks(path), options) == 1 &&
        read_file(path) == midi({track({
            0x00, 0x90, 0x3C, 0x64, 0x05, 0x91, 0x3D, 0x64,
            0x05, 0x80, 0x3C, 0x40, 0x05, 0x81, 0x3D, 0x40})}),
        "the collapsed MIDI replaces the processed one");
}
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "expected a test data directory");
        const fs::path directory(argv[1]);
        fs::create_directories(directory);
        test_merge_order(directory);
        test_running_status(directory);
        test_channel_split(directory);
        test_delta_overflow(directory);
        test_track_size_limit(directory);
        test_cancellation(directory);
        test_in_place(directory);
        for (const auto& entry : fs::directory_iterator(directory))
            check(entry.path().extension() != ".tmp", "temporary track files must be removed");
        std::cout << "PASS: tracks collapse by tick into bounded, valid tracks\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
