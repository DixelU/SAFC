#define NOMINMAX
#include "../SAFC_InnerModules/cut_and_transpose.h"

#include <iostream>
#include <stdexcept>

#include "../SAFC_InnerModules/single_midi_processor_2.h"

namespace
{
using processor = single_midi_processor_2;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void test_lookup_table()
{
    static_assert(sizeof(cut_and_transpose::lookup_table) == 512);
    constexpr auto identity = cut_and_transpose(0, 255, 0).bake();
    static_assert(identity[0] == 0 && identity[255] == 255);
    static_assert(cut_and_transpose::rejected == 0xFFFF);

    struct test_case
    {
        cut_and_transpose settings;
        int first_accepted, last_accepted, first_output;
    };
    const test_case cases[] = {
        {{0, 255, 0}, 0, 255, 0},
        {{0, 127, 0}, 0, 127, 0},
        {{0, 127, 128}, 0, 127, 128},
        {{60, 72, -60}, 60, 72, 0},
        {{60, 72, 183}, 60, 72, 243},
        {{0, 255, 1}, 0, 254, 1},
        {{0, 255, -1}, 1, 255, 0},
        {{0, 255, 255}, 0, 0, 255},
        {{0, 255, -255}, 255, 255, 0},
        {{0, 0, 0}, 0, 0, 0},
        {{255, 255, 0}, 255, 255, 255},
        {{72, 60, 0}, 1, 0, 0},
        {{0, 255, 256}, 1, 0, 0},
        {{0, 255, -256}, 1, 0, 0},
        {{0, 255, 32767}, 1, 0, 0},
        {{0, 255, -32768}, 1, 0, 0},
    };
    for (const auto& test : cases)
    {
        const auto table = test.settings.bake();
        for (int key = 0; key < 256; ++key)
        {
            const auto expected = key >= test.first_accepted && key <= test.last_accepted
                ? test.first_output + key - test.first_accepted : cut_and_transpose::rejected;
            check(table[key] == expected, "baked table must preserve inclusive cuts and reject output overflow");
            check(test.settings.process(static_cast<std::uint8_t>(key)) == expected,
                "direct conversion must use the same sentinel contract");
        }
    }
}

void check_note_pair(const processor::filters_multimap& filters,
    std::uint8_t key, cut_and_transpose::result_type expected)
{
    const auto event_size = processor::expected_size(std::uint8_t(0x90));
    std::vector<std::uint8_t> events(2 * event_size);
    for (int index = 0; index < 2; ++index)
    {
        const auto event = events.begin() + index * event_size;
        processor::get_value<processor::tick_type>(event, processor::tick_position) = 10 + index;
        processor::get_value<std::uint8_t>(event, processor::event_type) = index ? 0x80 : 0x90;
        processor::get_value<std::uint8_t>(event, processor::event_param1) = key;
        processor::get_value<std::uint8_t>(event, processor::event_param2) = 64;
        processor::get_value<processor::tick_type>(event, processor::event_param3) = (1 - index) * event_size;
    }

    processor::single_track_data track;
    processor::message_buffers logs;
    check(processor::process_buffer(events, processor::make_filter_bounding_iters(filters), track, logs),
        "note pair processing must succeed");
    for (int index = 0; index < 2; ++index)
    {
        const auto event = events.begin() + index * event_size;
        const auto tick = processor::get_value<processor::tick_type>(event, processor::tick_position);
        if (expected == cut_and_transpose::rejected)
            check(tick == processor::disable_tick, "rejection must disable both note-on and note-off");
        else
        {
            check(tick == 10 + index, "accepted notes must retain their timing");
            check(processor::get_value<std::uint8_t>(event, processor::event_param1) == expected,
                "both note-on and note-off must use the mapped key, including 0 and 255");
        }
    }
}

void test_processing_snapshot()
{
    processor::settings_obj settings{};
    settings.old_ppqn = settings.new_ppqn = 480;
    settings.key_converter = std::make_shared<cut_and_transpose>(60, 72, -60);
    auto original = processor::filters_constructor(settings);

    // Editing the UI settings must not alter a running filter's baked table.
    *settings.key_converter = cut_and_transpose(0, 255, 128);
    check_note_pair(original.second, 59, cut_and_transpose::rejected);
    check_note_pair(original.second, 60, 0);
    check_note_pair(original.second, 72, 12);
    check_note_pair(original.second, 73, cut_and_transpose::rejected);

    auto updated = processor::filters_constructor(settings);
    check_note_pair(updated.second, 0, 128);
    check_note_pair(updated.second, 127, 255);
    check_note_pair(updated.second, 128, cut_and_transpose::rejected);
    check_note_pair(updated.second, 255, cut_and_transpose::rejected);

    settings.key_converter.reset();
    auto disabled = processor::filters_constructor(settings);
    check(disabled.second.count(0x90) == 0 && disabled.second.count(0x80) == 0,
        "no note filters are needed when cut/transpose and volume mapping are disabled");
    check_note_pair(disabled.second, 255, 255);

    // Exercise the identity table while the note filter is enabled only for volume.
    const dixelu::polyline_converter<double, double> volume{{0, 0}, {255, 255}};
    settings.volume_map = std::make_shared<dixelu::byte_polyline_lookup_table>(
        volume, dixelu::polyline_extrapolation::linear);
    auto volume_only = processor::filters_constructor(settings);
    check_note_pair(volume_only.second, 0, 0);
    check_note_pair(volume_only.second, 255, 255);

    settings.filter.pass_notes = false;
    auto rejected = processor::filters_constructor(settings);
    check_note_pair(rejected.second, 60, cut_and_transpose::rejected);
}
}

int main()
{
    try
    {
        test_lookup_table();
        test_processing_snapshot();
        std::cout << "PASS: cut/transpose tables, rejection sentinel, note pairs, and processing snapshots\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
