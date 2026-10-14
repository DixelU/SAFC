#define NOMINMAX
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <string>
#include <thread>
#include <vector>

#include "../SAFC_InnerModules/single_midi_processor_lean.h"
#include "../SAFC_InnerModules/midi_collection_threaded_merger.h"

void throw_alert_error(std::string&& message)
{
    throw std::runtime_error(message);
}

namespace
{
using merger = midi_collection_threaded_merger;
using bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write_file(const fs::path& path, const bytes& data)
{
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    stream.close();
}

bytes read_file(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    check(stream.is_open(), "expected output file to exist");
    return bytes(std::istreambuf_iterator<char>(stream), {});
}

std::uint16_t read_u16(const bytes& data, std::size_t offset)
{
    return (std::uint16_t(data.at(offset)) << 8) | data.at(offset + 1);
}

std::uint32_t read_u32(const bytes& data, std::size_t offset)
{
    return (std::uint32_t(read_u16(data, offset)) << 16) | read_u16(data, offset + 2);
}

void push_vlv(bytes& data, std::uint64_t value)
{
    bytes reversed;
    do
    {
        reversed.push_back(std::uint8_t(value & 0x7F) | (reversed.empty() ? 0 : 0x80));
        value >>= 7;
    } while (value);
    data.insert(data.end(), reversed.rbegin(), reversed.rend());
}

std::uint64_t read_vlv(const bytes& data, std::size_t& position, int* length = nullptr)
{
    std::uint64_t value = 0;
    std::uint8_t byte;
    int count = 0;
    do
    {
        byte = data.at(position++);
        value = (value << 7) | (byte & 0x7F);
        ++count;
    } while (byte & 0x80);
    if (length)
        *length = count;
    return value;
}

using timed_events = std::vector<std::pair<std::uint64_t, bytes>>;

// Builds an MTrk body from events at absolute ticks, in tick order
bytes track_body(timed_events events)
{
    std::stable_sort(events.begin(), events.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    bytes data;
    std::uint64_t previous = 0;
    for (const auto& [tick, event] : events)
    {
        push_vlv(data, tick - previous);
        previous = tick;
        data.insert(data.end(), event.begin(), event.end());
    }
    data.insert(data.end(), {0, 0xFF, 0x2F, 0});
    return data;
}

bytes midi_file(const std::vector<bytes>& tracks)
{
    bytes data{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, std::uint8_t(tracks.size()), 1, 0xE0};
    for (const auto& track : tracks)
    {
        data.insert(data.end(), {'M', 'T', 'r', 'k'});
        for (int shift = 24; shift >= 0; shift -= 8)
            data.push_back(std::uint8_t(track.size() >> shift));
        data.insert(data.end(), track.begin(), track.end());
    }
    return data;
}

// What a MIDI sounds like regardless of how its tracks are cut into chunks: the
// net note-on count change per tick, channel and key, and every source event at
// its tick. Notes that end and restart held notes at a cut have velocity 1, which
// the test inputs never use.
struct midi_content
{
    std::uint16_t header_tracks = 0;
    std::size_t chunks = 0;
    std::map<std::tuple<std::uint64_t, int, int>, int> note_changes;
    std::multiset<std::pair<std::uint64_t, bytes>> events;
};

midi_content read_content(const bytes& data)
{
    midi_content content;
    content.header_tracks = read_u16(data, 10);
    std::size_t position = 14;
    while (position < data.size())
    {
        check(read_u32(data, position) == 0x4D54726B, "expected an MTrk chunk");
        const std::size_t end = position + 8 + read_u32(data, position + 4);
        check(end <= data.size(), "a chunk length must stay within the file");
        position += 8;

        std::uint64_t tick = 0;
        bool ended = false;
        while (position < end)
        {
            check(!ended, "end of track must be the last event of its chunk");
            int delta_length = 0;
            tick += read_vlv(data, position, &delta_length);
            check(delta_length <= 4, "a delta time must fit four bytes");

            const std::uint8_t status = data.at(position);
            check(status >= 0x80, "the merge does not write running status");
            std::size_t size = (status & 0xE0) == 0xC0 ? 2 : 3;
            if ((status & 0xE0) == 0x80)
            {
                const auto velocity = data.at(position + 2);
                content.note_changes[{tick, status & 0xF, data.at(position + 1)}] +=
                    (status & 0x10) && velocity ? 1 : -1;
                if (velocity != 1)
                    content.events.emplace(tick, bytes(data.begin() + position, data.begin() + position + size));
                position += size;
                continue;
            }

            if (status == 0xFF || status == 0xF0 || status == 0xF7)
            {
                std::size_t data_position = position + (status == 0xFF ? 2 : 1);
                const auto length = read_vlv(data, data_position);
                size = data_position + length - position;
            }
            bytes event(data.begin() + position, data.begin() + position + size);
            position += size;

            if (status == 0xFF && event.at(1) == 0x2F)
                ended = true;
            else if (event != bytes{0xFF, 0x01, 0x00}) // carries time across a cut
                content.events.emplace(tick, std::move(event));
        }
        check(ended && position == end, "every chunk must end with its end of track");
        ++content.chunks;
    }
    std::erase_if(content.note_changes, [](const auto& change) { return change.second == 0; });
    return content;
}

bytes merge_in_place(const fs::path& directory, const std::vector<fs::path>& inputs,
    std::uint64_t split_edge, std::uint64_t& reported_tracks)
{
    std::vector<merger::proc_data_ptr> requests;
    for (const auto& input : inputs)
    {
        auto data = std::make_shared<merger::proc_data_ptr::element_type>();
        data->filename = input.wstring();
        data->postfix = L".processed.mid";
        data->settings.new_ppqn = 480;
        data->settings.filter.pass_sysex = true;
        data->settings.details.inplace_mergable = true;
        data->settings.proc_details.remove_remnants = true;
        requests.push_back(data);
    }

    const auto output = directory / "merged.mid";
    auto collection = std::make_shared<merger>(requests, 480, output.wstring(), false);
    collection->inplace_split_edge = split_edge;
    collection->start_processing();
    while (!collection->is_smrp_complete())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    collection->start_ri_merge();
    while (!collection->is_ri_merge_complete())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    collection->start_final_merge();
    while (!collection->complete)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (collection->has_failed())
        throw std::runtime_error(collection->failure_message());

    reported_tracks = collection->inplace_track_count;
    return read_file(output);
}

// A merged track cut into chunks must keep every event at its tick: notes held
// across a cut end in one chunk and restart in the next, and time crosses a cut
// whether or not a note is held there.
void test_split(const fs::path& directory)
{
    fs::create_directories(directory);

    timed_events first{{0, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}}, {0, {0x90, 60, 100}},
        {200, {0xF0, 0x03, 0x01, 0x02, 0xF7}}, {500, {0x80, 60, 0}},
        {100000, {0x90, 64, 80}}, {100010, {0x80, 64, 0}}};
    for (std::uint64_t i = 0; i < 40; ++i)
    {
        first.push_back({10 * i, {0x90, 62, 90}});
        first.push_back({10 * i + 5, {0x80, 62, 0}});
    }
    const timed_events second{{5, {0xB2, 7, 100}}, {7, {0xE2, 0, 0x40}}, {20, {0x92, 48, 80}},
        {300, {0xC2, 5}}, {310, {0x82, 48, 0}}, {100005, {0x92, 50, 70}}, {100015, {0x82, 50, 0}}};
    // Overlapping notes of one key keep several of them held at once
    timed_events third{{100002, {0x91, 71, 60}}, {100100, {0x81, 71, 0}}};
    for (std::uint64_t i = 0; i < 30; ++i)
    {
        third.push_back({7 * i, {0x91, 70, 80}});
        third.push_back({7 * i + 20, {0x81, 70, 0}});
    }

    const std::vector<fs::path> inputs{directory / "a.mid", directory / "b.mid"};
    write_file(inputs[0], midi_file({track_body(first), track_body(second)}));
    write_file(inputs[1], midi_file({track_body(third)}));

    std::uint64_t reported = 0;
    const auto whole = read_content(merge_in_place(directory, inputs, merger::default_inplace_split_edge, reported));
    check(whole.chunks == 2 && reported == 2, "an unsplit in-place merge keeps one chunk per track");

    for (const std::uint64_t split_edge : {1, 5, 16, 50})
    {
        const auto split = read_content(merge_in_place(directory, inputs, split_edge, reported));
        check(split.chunks > whole.chunks, "a small split edge must cut the merged tracks");
        check(split.header_tracks == split.chunks && reported == split.chunks,
            "the header and the merger must count every chunk");
        check(split.events == whole.events, "cutting must keep every event at its tick");
        check(split.note_changes == whole.note_changes, "cutting must keep held notes sounding across a cut");
    }
}

void test_merge(const fs::path& directory, unsigned inplace_files,
    unsigned regular_files, std::uint16_t ppqn)
{
    fs::create_directories(directory);
    // A note lasting one quarter note at the source PPQ of 480.
    const bytes source{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 1, 1, 0xE0,
        'M', 'T', 'r', 'k', 0, 0, 0, 13,
        0, 0x90, 60, 64, 0x83, 0x60, 0x80, 60, 64, 0, 0xFF, 0x2F, 0};
    std::vector<merger::proc_data_ptr> inputs;
    for (unsigned index = 0; index < inplace_files + regular_files; ++index)
    {
        const auto input = directory / (std::to_string(index) + ".mid");
        write_file(input, source);
        auto data = std::make_shared<merger::proc_data_ptr::element_type>();
        data->filename = input.wstring();
        data->postfix = L".processed.mid";
        data->settings.new_ppqn = ppqn;
        data->settings.details.inplace_mergable = index < inplace_files;
        data->settings.proc_details.remove_remnants = true;
        inputs.push_back(data);
    }

    const auto output = directory / "merged.mid";
    const fs::path inplace_path(output.wstring() + L".I.mid");
    const fs::path regular_path(output.wstring() + L".R.mid");
    // A failed previous run must not supply an otherwise absent stage file.
    fs::remove(inplace_path);
    fs::remove(regular_path);
    auto collection = std::make_shared<merger>(inputs, ppqn, output.wstring(), false);
    collection->start_processing();
    while (!collection->is_smrp_complete())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (collection->has_failed())
        throw std::runtime_error(collection->failure_message());

    for (const auto& input : inputs)
        check(read_u16(read_file(input->filename + input->postfix), 12) == ppqn,
            "processing must write the requested PPQ");

    collection->start_ri_merge();
    while (!collection->is_ri_merge_complete())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (collection->has_failed())
        throw std::runtime_error(collection->failure_message());

    bytes expected;
    for (const auto& path : {inplace_path, regular_path})
    {
        if (!fs::exists(path))
            continue;
        const auto stage = read_file(path);
        check(read_u16(stage, 12) == ppqn, "intermediate merge must retain the requested PPQ");
        if (expected.empty())
            expected = stage;
        else
        {
            const auto tracks = read_u16(expected, 10) + read_u16(stage, 10);
            expected[10] = static_cast<std::uint8_t>(tracks >> 8);
            expected[11] = static_cast<std::uint8_t>(tracks);
            expected.insert(expected.end(), stage.begin() + 14, stage.end());
        }
    }

    collection->start_final_merge();
    while (!collection->complete)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (collection->has_failed())
        throw std::runtime_error(collection->failure_message());
    const auto result = read_file(output);
    check(read_u16(result, 12) == ppqn, "final merge must retain the requested PPQ");
    check(read_u16(result, 10) == (inplace_files ? 1 : 0) + regular_files,
        "final merge must report all output tracks");
    check(result == expected, "final merge must preserve the intermediate tracks exactly");
    check(!fs::exists(inplace_path) && !fs::exists(regular_path),
        "final merge must consume or remove its intermediate files");
    for (const auto& input : inputs)
        check(!fs::exists(input->output_path()), "merge must remove processed remnant files");
    for (const auto& input : inputs)
        check(read_file(input->filename) == source, "merge must leave source files unchanged");
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "expected a test data directory\n";
        return 1;
    }
    unsigned failures = 0;
    for (const std::uint16_t ppqn : {96, 960})
        for (const auto [inplace, regular] :
            {std::pair{1u, 0u}, {2u, 0u}, {0u, 1u}, {0u, 2u}, {2u, 1u}, {2u, 2u}})
        {
            const auto name = std::to_string(ppqn) + "-" + std::to_string(inplace)
                + "-inplace-" + std::to_string(regular) + "-regular";
            try
            {
                test_merge(fs::path(argv[1]) / name, inplace, regular, ppqn);
                std::cout << "PASS: " << name << '\n';
            }
            catch (const std::exception& error)
            {
                std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
                ++failures;
            }
        }
    try
    {
        test_split(fs::path(argv[1]) / "split");
        std::cout << "PASS: split\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: split: " << error.what() << '\n';
        ++failures;
    }
    return failures ? 1 : 0;
}
