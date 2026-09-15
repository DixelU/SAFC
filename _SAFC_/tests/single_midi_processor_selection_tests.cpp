#define NOMINMAX
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../SAFC_InnerModules/single_midi_processor_lean.h"

namespace
{
using processor = single_midi_processor_2;
using bytes = std::vector<std::uint8_t>;
using tick_type = processor::tick_type;
namespace fs = std::filesystem;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct event
{
    tick_type tick;
    std::uint8_t status, key, velocity;

    bool operator==(const event&) const = default;
};

void append_u32(bytes& data, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        data.push_back(static_cast<std::uint8_t>(value >> shift));
}

bytes make_track(std::vector<event> events)
{
    std::stable_sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.tick < rhs.tick;
    });
    bytes result;
    tick_type previous = 0;
    std::uint8_t running_status = 0;
    for (const auto& item : events)
    {
        // Test fixtures deliberately use one-byte deltas and input running status.
        check(item.tick - previous < 128, "fixture delta must fit in one byte");
        result.push_back(static_cast<std::uint8_t>(item.tick - previous));
        if (item.status != running_status)
            result.push_back(item.status);
        result.insert(result.end(), {item.key, item.velocity});
        previous = item.tick;
        running_status = item.status;
    }
    result.insert(result.end(), {0, 0xFF, 0x2F, 0});
    return result;
}

void write_midi(const fs::path& path, const std::vector<bytes>& tracks)
{
    bytes data{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1,
        static_cast<std::uint8_t>(tracks.size() >> 8),
        static_cast<std::uint8_t>(tracks.size()), 1, 0xE0};
    for (const auto& track : tracks)
    {
        data.insert(data.end(), {'M', 'T', 'r', 'k'});
        append_u32(data, static_cast<std::uint32_t>(track.size()));
        data.insert(data.end(), track.begin(), track.end());
    }
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Decode the actual SMF output independently of the processor's buffer helpers.
// Checking order as well as counts catches an off-before-on that leaves a voice on.
std::vector<event> read_notes(const fs::path& path, std::uint64_t expected_tracks, int ppqn)
{
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "processed MIDI must exist");
    const bytes data(std::istreambuf_iterator<char>(input), {});
    std::size_t position = 0;
    const auto read = [&]() {
        check(position < data.size(), "output event must fit within the file");
        return data[position++];
    };
    const auto integer = [&](int size) {
        std::uint32_t value = 0;
        while (size--)
            value = (value << 8) | read();
        return value;
    };
    const auto vlv = [&]() {
        tick_type value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto byte = read();
            value = (value << 7) | (byte & 0x7F);
            if (!(byte & 0x80))
                return value;
        }
        throw std::runtime_error("output delta must be a valid four-byte VLV");
    };
    check(integer(4) == processor::MThd_header && integer(4) == 6, "valid MIDI header required");
    integer(2);
    const auto tracks = integer(2);
    check(tracks == expected_tracks && integer(2) == ppqn, "header must match track count and output PPQ");
    std::vector<event> notes;
    for (unsigned track = 0; track < tracks; ++track)
    {
        check(integer(4) == processor::MTrk_header, "valid track header required");
        const auto length = integer(4);
        const auto end = position + length;
        check(end <= data.size(), "track chunk must fit within output");
        std::array<int, 16 * 128> active{};
        tick_type tick = 0;
        std::uint8_t running_status = 0;
        bool ended = false;
        while (position < end)
        {
            tick += vlv();
            auto status = read();
            if (status == 0xFF)
            {
                check(read() == 0x2F && vlv() == 0, "only EOT is expected in these note fixtures");
                check(position == end, "EOT must finish the track chunk");
                ended = true;
                break;
            }
            std::uint8_t key;
            if (status < 0x80)
            {
                key = status;
                status = running_status;
            }
            else
            {
                running_status = status;
                key = read();
            }
            auto velocity = read();
            check((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90, "valid note status required");
            check(key < 128 && velocity < 128, "note data must be seven-bit bytes");
            const bool on = (status & 0xF0) == 0x90 && velocity != 0;
            auto& count = active[(status & 0x0F) * 128 + key];
            if (on)
                ++count;
            else
            {
                check(count > 0, "note-off must follow a matching note-on in the serialized track");
                --count;
                status = 0x80 | (status & 0x0F);
                velocity = 0; // release velocity can differ under running-status compression
            }
            notes.push_back({tick, status, key, velocity});
        }
        check(ended, "every output track must have EOT");
        check(std::all_of(active.begin(), active.end(), [](int count) { return count == 0; }),
            "no selected note may remain active at EOT");
    }
    check(position == data.size(), "all output chunks must be accounted for");
    return notes;
}

void sort_notes(std::vector<event>& events)
{
    std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.tick, lhs.status, lhs.key, lhs.velocity)
            < std::tie(rhs.tick, rhs.status, rhs.key, rhs.velocity);
    });
}

template<bool split>
void test_selection(const fs::path& directory, bool compression, bool collapse, bool convert, bool after)
{
    const auto input = directory / "selection.mid";
    std::vector<event> first, second, expected;
    const auto transform = [&](tick_type tick) { return convert ? (after ? tick * 2 + 100 : (tick + 100) * 2) : tick; };
    const auto add_note = [&](std::vector<event>& track, int key, int channel,
        int on, int off, int selected_on, int selected_off, bool zero_off = false) {
        track.push_back({tick_type(on), std::uint8_t(0x90 | channel), std::uint8_t(key), 64});
        track.push_back({tick_type(off), std::uint8_t((zero_off ? 0x90 : 0x80) | channel), std::uint8_t(key), 0});
        if (selected_on >= 0)
        {
            expected.push_back({transform(selected_on), std::uint8_t(0x90 | channel), std::uint8_t(key),
                std::uint8_t(on < 10 ? 1 : 64)});
            expected.push_back({transform(selected_off), std::uint8_t(0x80 | channel), std::uint8_t(key), 0});
        }
    };
    // Two source tracks force a real merge during collapse. The first has notes
    // starting on the final selected tick, where both endpoints become equal.
    for (int key = 0; key < 64; ++key)
    {
        add_note(first, key, 0, 19, 30, 19, 19);
        add_note(second, key, 1, 15, 30, 15, 19, true);
    }
    add_note(first, 70, 0, 0, 5, -1, -1);
    add_note(first, 71, 0, 0, 10, 10, 10);
    add_note(first, 72, 0, 0, 15, 10, 15);
    add_note(first, 73, 0, 0, 30, 10, 19);
    add_note(first, 74, 0, 10, 15, 10, 15);
    add_note(first, 75, 0, 20, 30, -1, -1);
    add_note(first, 76, 0, 11, 30, 11, 19);
    add_note(first, 76, 0, 12, 25, 12, 19); // overlapping instances of one key
    add_note(second, 77, 15, 11, 17, 11, 17, true);
    first.push_back({12, 0x90, 78, 64}); // parser synthesizes its missing off at EOT
    expected.push_back({transform(12), 0x90, 78, 64});
    expected.push_back({transform(19), 0x80, 78, 0});
    write_midi(input, {make_track(first), make_track(second)});

    auto data = std::make_unique<processor::processing_data>();
    data->filename = input.wstring();
    data->postfix = L".selected.mid";
    auto& settings = data->settings;
    settings.old_ppqn = 480;
    settings.new_ppqn = convert ? 960 : 480;
    settings.offset = convert ? 100 : 0;
    settings.selection_data = processor::settings_obj::selection(10, 10);
    settings.proc_details.remove_empty_tracks = true;
    settings.proc_details.whole_midi_collapse = collapse;
    settings.proc_details.channel_split = split;
    settings.proc_details.apply_offset_after = after;
    settings.legacy.rsb_compression = compression;
    check(!single_midi_processor_lean::can_handle(settings), "selections must use the feature-rich processor");
    processor::message_buffers logs;
    processor::sync_processing<split>(*data, logs);
    check(logs.finished && !logs.processing && logs.error->get_last_event().type == log_event_type::none,
        "selection processing must finish without errors");
    auto actual = read_notes(data->filename + data->postfix, data->tracks_count, settings.new_ppqn);
    sort_notes(actual);
    sort_notes(expected);
    check(actual == expected, "serialized notes must match the expected clipped pitches, channels, velocities, and ticks");
}
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "expected a test data directory");
        const fs::path directory(argv[1]);
        fs::create_directories(directory);
        for (const bool compression : {false, true})
            for (const bool collapse : {false, true})
                for (const bool convert : {false, true})
                    for (const bool after : {false, true})
                    {
                        test_selection<false>(directory, compression, collapse, convert, after);
                        test_selection<true>(directory, compression, collapse, convert, after);
                    }
        std::cout << "PASS: selected MIDI retains ordered, balanced note pairs across collapse, channels, compression, and timing transforms\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
