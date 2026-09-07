#define NOMINMAX
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
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
    return failures ? 1 : 0;
}
