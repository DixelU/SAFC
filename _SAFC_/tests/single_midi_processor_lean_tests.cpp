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
using processor = single_midi_processor_lean;
using bytes = std::vector<std::uint8_t>;
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

bytes make_midi(const std::vector<bytes>& tracks)
{
    bytes result{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1,
        static_cast<std::uint8_t>(tracks.size() >> 8),
        static_cast<std::uint8_t>(tracks.size()), 1, 0xE0};
    for (const auto& track : tracks)
    {
        result.insert(result.end(), {'M', 'T', 'r', 'k'});
        append_u32(result, static_cast<std::uint32_t>(track.size()));
        result.insert(result.end(), track.begin(), track.end());
    }
    return result;
}

bytes make_track(std::size_t event_bytes)
{
    bytes track;
    for (std::size_t i = 0; i < event_bytes; i += 4)
        track.insert(track.end(), {0, 0xB0, 1, 64});
    track.insert(track.end(), {0, 0xFF, 0x2F, 0});
    return track;
}

bytes make_bad_track(std::size_t event_bytes, bool bad_running_status)
{
    auto track = make_track(event_bytes);
    track.resize(track.size() - 4);
    if (bad_running_status)
    {
        // A meta event clears running status before the next data byte.
        track.insert(track.end(), {0, 0xFF, 1, 0, 0, 1, 64});
    }
    else
        track.insert(track.end(), {0, 0xF1, 0});
    track.insert(track.end(), {0, 0xFF, 0x2F, 0});
    return track;
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

auto make_data(const fs::path& input)
{
    auto data = std::make_shared<processor::processing_data>();
    data->filename = input.wstring();
    data->postfix = L".processed.mid";
    data->settings.new_ppqn = 480;
    return data;
}

void test_parse_recovery(const fs::path& directory, std::size_t event_bytes,
    bool bad_running_status, bool preceding_track)
{
    std::vector<bytes> tracks;
    if (preceding_track)
        tracks.push_back(make_track(8));
    tracks.push_back(make_bad_track(event_bytes, bad_running_status));
    // The following track must be reachable by its chunk boundary in the output.
    tracks.push_back(make_track(12));
    const auto source = make_midi(tracks);
    const auto input = directory / "bad.mid";
    write_file(input, source);
    auto data = make_data(input);
    processor::message_buffers logs;
    processor::sync_processing(*data, logs);
    auto recovered_track = make_track(event_bytes);
    if (bad_running_status)
    {
        recovered_track.resize(recovered_track.size() - 4);
        recovered_track.insert(recovered_track.end(), {0, 0xFF, 1, 0, 0, 0xFF, 0x2F, 0});
    }
    tracks[preceding_track ? 1 : 0] = recovered_track;
    check(read_file(data->filename + data->postfix) == make_midi(tracks),
        "recovered output must preserve the valid prefix, append EOT, patch the length, and retain later tracks");
    check(data->tracks_count == tracks.size(), "recovered tracks must be counted");
    check(logs.finished && !logs.processing, "recovery must complete normally");
    check(logs.error->get_last_event().type == (bad_running_status
        ? log_event_type::unexpected_zero_rsb : log_event_type::unknown_event_type),
        "specific parser diagnostic must be preserved");
    check(read_file(input) == source, "parse failure must leave the source unchanged");
}

void test_valid_tracks(const fs::path& directory, bool remove_empty)
{
    // Exercise no flush, exactly one full buffer (including EOT), overflow
    // while writing EOT, and several payload flushes followed by another track.
    const std::vector<bytes> tracks{make_track(0), make_track(8),
        make_track(processor::track_writer::capacity - 4),
        make_track(processor::track_writer::capacity),
        make_track(2 * processor::track_writer::capacity + 12), make_track(12)};
    const auto input = directory / "valid.mid";
    write_file(input, make_midi(tracks));
    auto data = make_data(input);
    data->settings.proc_details.remove_empty_tracks = remove_empty;
    processor::message_buffers logs;
    processor::sync_processing(*data, logs);
    const std::vector<bytes> expected(tracks.begin() + (remove_empty ? 1 : 0), tracks.end());
    check(read_file(data->filename + data->postfix) == make_midi(expected),
        "valid output must have exact chunk sizes, track counts, and payloads");
    check(data->tracks_count == expected.size(), "valid track count must match output");
    check(logs.finished && !logs.processing, "valid input must complete normally at EOF");
    check(logs.error->get_last_event().type == log_event_type::none,
        "normal EOF must not be treated as a parse failure");
}

void test_empty_recovery(const fs::path& directory, bool remove_empty)
{
    const auto input = directory / "empty-bad.mid";
    // Both failures occur before any valid event, including an unset running status.
    write_file(input, make_midi({{0, 1, 64}, {0, 0xF1, 0}, make_track(12)}));
    auto data = make_data(input);
    data->settings.proc_details.remove_empty_tracks = remove_empty;
    processor::message_buffers logs;
    processor::sync_processing(*data, logs);
    const std::vector<bytes> expected = remove_empty ? std::vector<bytes>{make_track(12)}
        : std::vector<bytes>{make_track(0), make_track(0), make_track(12)};
    check(read_file(data->filename + data->postfix) == make_midi(expected),
        "empty recovered tracks must obey remove_empty_tracks");
    check(data->tracks_count == expected.size(), "empty recovery count must match output");
}

void test_fault_at_eof(const fs::path& directory)
{
    const auto input = directory / "bad-eof.mid";
    constexpr auto event_bytes = processor::track_writer::capacity + 12;
    auto track = make_bad_track(event_bytes, false);
    track.resize(event_bytes + 2); // The unsupported status is the final input byte.
    write_file(input, make_midi({track}));
    auto data = make_data(input);
    processor::message_buffers logs;
    processor::sync_processing(*data, logs);
    check(read_file(data->filename + data->postfix) == make_midi({make_track(event_bytes)}),
        "a streamed faulty final track must be finalized even without a following track");
    check(data->tracks_count == 1 && logs.finished && !logs.processing,
        "recovery at EOF must complete with one track");
}
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "expected a test data directory");
        const fs::path directory(argv[1]);
        fs::create_directories(directory);
        for (const auto event_bytes : {std::size_t(16), processor::track_writer::capacity - 4,
            processor::track_writer::capacity, processor::track_writer::capacity + 12,
            2 * processor::track_writer::capacity + 12})
            for (const bool bad_running_status : {false, true})
                for (const bool preceding_track : {false, true})
                    test_parse_recovery(directory, event_bytes, bad_running_status, preceding_track);
        test_valid_tracks(directory, false);
        test_valid_tracks(directory, true);
        test_empty_recovery(directory, false);
        test_empty_recovery(directory, true);
        test_fault_at_eof(directory);
        std::cout << "PASS: faulty tracks are finalized and processing resumes; valid streaming tracks and EOF remain intact\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
