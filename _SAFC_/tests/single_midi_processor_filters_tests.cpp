#define NOMINMAX
#include <iostream>
#include <stdexcept>

#include "../SAFC_InnerModules/single_midi_processor_2.h"

namespace
{
using processor = single_midi_processor_2;
using bytes = std::vector<std::uint8_t>;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

auto make_settings()
{
    processor::settings_obj settings{};
    settings.old_ppqn = settings.new_ppqn = 480;
    settings.filter.pass_sysex = true;
    return settings;
}

bytes make_event(std::uint8_t type, std::uint8_t param1 = 60, std::uint8_t param2 = 64)
{
    const bool meta = type == 0xFF;
    const bool sysex = type == 0xF0 || type == 0xF7;
    const bool tempo = meta && param1 == 0x51;
    bytes event(meta || sysex ? processor::event_meta_raw - sysex + 1 + 3 * tempo
        : processor::expected_size(type));
    processor::get_value<processor::tick_type>(event, 0) = 10;
    event[processor::event_type] = type;
    event[processor::event_param1] = param1;
    if (meta || sysex)
    {
        event[processor::event_param2 - sysex] = 1;
        processor::get_value<processor::metasize_type>(event, processor::event_param3 - sysex) = 1 + 3 * tempo;
        event[processor::event_meta_raw - sysex] = 3 * tempo;
        if (tempo)
        {
            const auto start = processor::get_meta_param_index(1, 0);
            event[start] = 0x07;
            event[start + 1] = 0xA1;
            event[start + 2] = 0x20;
        }
    }
    else if (event.size() > processor::event_param2)
        event[processor::event_param2] = param2;
    return event;
}

bytes make_note_pair(std::uint8_t channel = 0, std::uint8_t velocity = 64)
{
    auto events = make_event(0x90 | channel, 60, velocity);
    auto off = make_event(0x80 | channel, 60, 45);
    processor::get_value<processor::tick_type>(off, 0) = 11;
    processor::get_value<processor::tick_type>(events, processor::event_param3) = events.size();
    events.insert(events.end(), off.begin(), off.end());
    return events;
}

void process(bytes& events, const processor::filter_table& filters, processor::single_track_data& track)
{
    processor::message_buffers logs;
    check(processor::process_buffer(events, filters, track, logs), "filter processing must succeed");
    check(logs.error->get_last_event().type == log_event_type::none, "filters must not corrupt event boundaries");
}

void process(bytes& events, const processor::filter_table& filters)
{
    processor::single_track_data track;
    process(events, filters, track);
}

void test_volume_tables()
{
    static_assert(sizeof(processor::volume_lookup_table) == 256);
    const dixelu::polyline_converter<double, double> curves[] = {
        {}, {{0, 0}, {255, 255}}, {{64, 0}, {128, 255}}, {{0, 0}, {255, 0}},
    };
    for (const auto& curve : curves)
    {
        const dixelu::byte_polyline_lookup_table original(curve, dixelu::polyline_extrapolation::linear);
        auto settings = make_settings();
        settings.volume_map = std::make_shared<const processor::volume_lookup_table>(processor::bake_volume_map(original));
        auto bundle = processor::filters_constructor(settings);
        for (int velocity = 0; velocity < 256; ++velocity)
        {
            const auto expected = original[velocity].value_or(static_cast<std::uint8_t>(velocity));
            check((*settings.volume_map)[velocity] == expected, "volume fallback must retain its input velocity");
            auto events = make_note_pair(0, static_cast<std::uint8_t>(velocity));
            process(events, bundle.second);
            const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
            if (!expected)
            {
                check(processor::get_value<processor::tick_type>(events, 0) == processor::disable_tick
                    && processor::get_value<processor::tick_type>(off, 0) == processor::disable_tick,
                    "zero mapped velocity must reject both events");
            }
            else
            {
                check(processor::get_value<processor::tick_type>(events, 0) == 10
                    && processor::get_value<processor::tick_type>(off, 0) == 11, "valid volume mappings must retain notes");
                check(events[processor::event_param2] == expected, "note-on must use the baked velocity");
                check(off[processor::event_param2] == 45, "volume mapping must preserve note-off velocity");
            }
        }
    }
}

void test_pitch_tables()
{
    static_assert(sizeof(processor::pitch_lookup_table) == 32768);
    const dixelu::polyline_converter<double, double> curves[] = {
        {}, {{0, 0}, {16383, 16383}}, {{100, 0}, {1000, 16383}},
    };
    for (const auto& curve : curves)
    {
        const auto original = dixelu::make_midi14_polyline_lookup_table(curve, dixelu::polyline_extrapolation::linear);
        auto settings = make_settings();
        settings.pitch_map = std::make_shared<const processor::pitch_lookup_table>(processor::bake_pitch_map(original));
        auto bundle = processor::filters_constructor(settings);
        for (unsigned pitch = 0; pitch < 16384; ++pitch)
        {
            const auto expected = original.at(pitch).value_or(0x4000);
            check((*settings.pitch_map)[pitch] == expected, "pitch fallback must preserve the previous 0x4000 value");
            auto event = make_event(0xE0, pitch & 0x7F, pitch >> 7);
            process(event, bundle.second);
            check(event[processor::event_param1] == (expected & 0x7F)
                && event[processor::event_param2] == ((expected >> 7) & 0x7F),
                "pitch output bytes must match the original table and fallback");
        }
        auto high_bits = make_event(0xEF, 0xFF, 0xFF);
        process(high_bits, bundle.second);
        const auto expected = original.at(16383).value_or(0x4000);
        check(high_bits[processor::event_param1] == (expected & 0x7F)
            && high_bits[processor::event_param2] == ((expected >> 7) & 0x7F),
            "input data bytes must be masked to a valid 14-bit table index");
    }
}

void test_note_off_compression()
{
    for (const bool compression : {false, true})
    {
        auto settings = make_settings();
        settings.legacy.rsb_compression = compression;
        auto bundle = processor::filters_constructor(settings);
        for (int channel = 0; channel < 16; ++channel)
        {
            auto events = make_note_pair(static_cast<std::uint8_t>(channel));
            process(events, bundle.second);
            const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
            check(events[processor::event_type] == (0x90 | channel) && events[processor::event_param2] == 64,
                "compression must leave note-on events intact");
            check(off[processor::event_type] == ((compression ? 0x90 : 0x80) | channel)
                && off[processor::event_param2] == (compression ? 0 : 45),
                "compression must preserve the channel and rewrite only note-off status and velocity");
        }
    }
}

void test_filter_order_and_lifetime()
{
    auto settings = make_settings();
    settings.key_converter = std::make_shared<cut_and_transpose>(60, 60, 12);
    auto volume = std::make_shared<processor::volume_lookup_table>();
    volume->fill(65);
    settings.volume_map = volume;
    settings.legacy.rsb_compression = true;
    settings.offset = 3;
    settings.new_ppqn = 960;
    for (const bool after : {false, true})
    {
        settings.proc_details.apply_offset_after = after;
        auto bundle = processor::filters_constructor(settings);
        auto moved = std::move(bundle);
        auto events = make_note_pair();
        process(events, moved.second);
        const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
        check(processor::get_value<processor::tick_type>(events, 0) == (after ? 23 : 26)
            && processor::get_value<processor::tick_type>(off, 0) == (after ? 25 : 28),
            "moving the filter bundle must preserve callable lifetimes and timing settings");
        check(events[processor::event_param1] == 72 && off[processor::event_param1] == 72,
            "both events must be transposed before compression");
        check(events[processor::event_param2] == 65 && off[processor::event_param2] == 0,
            "note-off rewrite must run after volume mapping");
    }

    volume = std::make_shared<processor::volume_lookup_table>();
    volume->fill(0);
    settings.volume_map = volume;
    auto rejected = processor::filters_constructor(settings);
    auto events = make_note_pair();
    process(events, rejected.second);
    const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
    check(processor::get_value<processor::tick_type>(events, 0) == processor::disable_tick
        && processor::get_value<processor::tick_type>(off, 0) == processor::disable_tick,
        "rejected note pairs must stop their pipelines");
    check(off[processor::event_type] == 0x80, "a disabled note-off must not reach the compression filter");

    settings = make_settings();
    settings.offset = 100;
    settings.selection_data = processor::settings_obj::selection(20, 100);
    auto selected = processor::filters_constructor(settings);
    auto control = make_event(0xB0);
    process(control, selected.second);
    check(processor::get_value<processor::tick_type>(control, 0) == processor::disable_tick,
        "selection must evaluate the original tick before offset conversion");
}

void test_importance_tables()
{
    for (unsigned flags = 0; flags < 32; ++flags)
    {
        auto settings = make_settings();
        settings.enable_imp_events_filter = true;
        auto& filter = settings.imp_events_filter;
        filter.pass_notes = flags & 1;
        filter.pass_instument_cnage = flags & 2;
        filter.pass_pitch = flags & 4;
        filter.pass_tempo = flags & 8;
        filter.pass_other = flags & 16;
        auto bundle = processor::filters_constructor(settings);
        for (unsigned type = 0x80; type <= 0xEF; ++type)
        {
            auto event = make_event(static_cast<std::uint8_t>(type));
            processor::single_track_data track;
            process(event, bundle.second, track);
            const unsigned flag = type < 0xA0 ? 1 : type < 0xC0 ? 16 : type < 0xD0 ? 2 : type < 0xE0 ? 16 : 4;
            check(track.has_important_events == bool(flags & flag), "channel importance must match all flag combinations");
        }
        for (unsigned meta = 0; meta < 256; ++meta)
        {
            auto event = make_event(0xFF, static_cast<std::uint8_t>(meta));
            processor::single_track_data track;
            process(event, bundle.second, track);
            check(track.has_important_events == bool((flags & 16) || (meta == 0x51 && (flags & 8))),
                "tempo importance must retain its OR with the other-events flag");
        }
        for (const auto type : {0xF0, 0xF7})
        {
            auto event = make_event(static_cast<std::uint8_t>(type));
            processor::single_track_data track;
            process(event, bundle.second, track);
            check(track.has_important_events == bool(flags & 16), "SysEx importance must follow other events");
        }
    }

    auto settings = make_settings();
    settings.enable_imp_events_filter = true;
    settings.imp_events_filter = {false, false, true, false, false};
    auto bundle = processor::filters_constructor(settings);
    processor::single_track_data track;
    auto events = make_note_pair();
    process(events, bundle.second, track);
    auto control = make_event(0xB0);
    process(control, bundle.second, track);
    check(track.has_important_events, "finding an important event must remain sticky across later events");
    track.clear();
    process(control, bundle.second, track);
    check(!track.has_important_events, "importance must reset for the next track");

    settings.filter.pass_notes = false;
    auto filtered = processor::filters_constructor(settings);
    events = make_note_pair();
    process(events, filtered.second, track);
    check(track.has_important_events && processor::get_value<processor::tick_type>(events, 0) == processor::disable_tick,
        "importance classification must retain its position before class-specific rejection");
}

void test_selection_timing()
{
    for (const auto ppqn : {240, 480, 960})
        for (const int offset : {-10, 0, 100})
            for (const bool after : {false, true})
                for (const bool compression : {false, true})
                {
                    auto settings = make_settings();
                    settings.selection_data = processor::settings_obj::selection(10, 10);
                    settings.new_ppqn = ppqn;
                    settings.offset = offset;
                    settings.proc_details.apply_offset_after = after;
                    settings.legacy.rsb_compression = compression;
                    const auto transform = [&](processor::tick_type tick) {
                        return after ? std::int64_t(tick * ppqn / 480) + offset
                            : (std::int64_t(tick) + offset) * ppqn / 480;
                    };
                    // Keep both transformed endpoints nonnegative for this test.
                    if (transform(10) < 0)
                        continue;
                    auto bundle = processor::filters_constructor(settings);
                    for (const int start : {0, 10, 15, 19})
                    {
                        auto events = make_note_pair();
                        const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
                        processor::get_value<processor::tick_type>(events, 0) = start;
                        processor::get_value<processor::tick_type>(off, 0) = 30;
                        process(events, bundle.second);
                        check(processor::get_value<processor::tick_type>(events, 0) == transform(std::max(start, 10))
                            && processor::get_value<processor::tick_type>(off, 0) == transform(19),
                            "selection must retain both clipped endpoints regardless of offset or PPQ conversion");
                    }
                }
}

void test_selection_sort_order()
{
    auto settings = make_settings();
    settings.selection_data = processor::settings_obj::selection(10, 10);
    auto bundle = processor::filters_constructor(settings);
    bytes events;
    // Selection collapses all these pairs onto tick 19. An unstable tick-only
    // sort can emit a note-off before its own note-on, leaving the note hanging.
    for (int source_track = 0; source_track < 2; ++source_track)
        for (int endpoint = 0; endpoint < 2; ++endpoint)
            for (int key = 0; key < 64; ++key)
            {
                auto event = make_event((endpoint ? 0x80 : 0x90) | source_track, key);
                processor::get_value<processor::tick_type>(event, 0) = endpoint ? 30 : (source_track ? 15 : 19);
                processor::get_value<processor::tick_type>(event, processor::event_param3)
                    = (source_track * 128 + (1 - endpoint) * 64 + key) * event.size();
                events.insert(events.end(), event.begin(), event.end());
            }
    process(events, bundle.second);
    bytes expected;
    const auto event_size = processor::expected_size(std::uint8_t(0x90));
    for (const int tick : {15, 19})
        for (auto event = events.begin(); event != events.end(); event += event_size)
            if (processor::get_value<processor::tick_type>(event, 0) == tick)
                expected.insert(expected.end(), event, event + event_size);
    processor::single_track_data track;
    processor::message_buffers logs;
    check(processor::sort_buffer(events, track, logs), "selected events must sort successfully");
    check(events == expected, "events at the same selected tick must retain source order, including note-on before note-off");
}

void test_selection_flattening()
{
    auto settings = make_settings();
    settings.selection_data = processor::settings_obj::selection(10, 10);
    settings.flatten = true;
    // A constant 1,000,000 us/quarter tempo flattened to 500,000 doubles ticks.
    settings.original_time_map[0] = {0, 480000000};
    settings.original_time_map[100] = {100000000, 480000000};
    auto bundle = processor::filters_constructor(settings);
    auto events = make_note_pair();
    const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
    processor::get_value<processor::tick_type>(events, 0) = 15;
    processor::get_value<processor::tick_type>(off, 0) = 30;
    process(events, bundle.second);
    check(processor::get_value<processor::tick_type>(events, 0) == 30
        && processor::get_value<processor::tick_type>(off, 0) == 38,
        "selection must retain a cut note-off after its note-on has been tempo-flattened");
}

void test_selection_rejected_notes()
{
    for (int rejection = 0; rejection < 3; ++rejection)
    {
        auto settings = make_settings();
        settings.selection_data = processor::settings_obj::selection(10, 10);
        if (rejection == 0)
            settings.filter.pass_notes = false;
        else if (rejection == 1)
            settings.key_converter = std::make_shared<cut_and_transpose>(0, 59, 0);
        else
        {
            auto volume = std::make_shared<processor::volume_lookup_table>();
            volume->fill(0);
            settings.volume_map = volume;
        }
        auto bundle = processor::filters_constructor(settings);
        for (const int start : {0, 10, 15, 20})
        {
            auto events = make_note_pair();
            const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
            processor::get_value<processor::tick_type>(events, 0) = start;
            processor::get_value<processor::tick_type>(off, 0) = 30;
            process(events, bundle.second);
            check(processor::get_value<processor::tick_type>(events, 0) == processor::disable_tick
                && processor::get_value<processor::tick_type>(off, 0) == processor::disable_tick,
                "selection must not retain either endpoint of a rejected note");
        }
    }
}

void test_empty_selection()
{
    auto settings = make_settings();
    settings.selection_data = processor::settings_obj::selection(10, 0);
    auto bundle = processor::filters_constructor(settings);
    auto events = make_note_pair();
    const auto off = events.begin() + processor::expected_size(std::uint8_t(0x90));
    processor::get_value<processor::tick_type>(events, 0) = 0;
    processor::get_value<processor::tick_type>(off, 0) = 30;
    process(events, bundle.second);
    check(processor::get_value<processor::tick_type>(events, 0) == processor::disable_tick
        && processor::get_value<processor::tick_type>(off, 0) == processor::disable_tick,
        "an empty selection must reject spanning notes instead of placing note-off before note-on");
}
}

int main()
{
    try
    {
        test_volume_tables();
        test_pitch_tables();
        test_note_off_compression();
        test_filter_order_and_lifetime();
        test_importance_tables();
        test_selection_sort_order();
        test_selection_timing();
        test_selection_flattening();
        test_selection_rejected_notes();
        test_empty_selection();
        std::cout << "PASS: resolved maps, compression, filter ordering, event importance, and selection boundaries\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
