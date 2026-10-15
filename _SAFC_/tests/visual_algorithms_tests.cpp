#define NOMINMAX
#include <Windows.h>
#include "../SAFC_InnerModules/simple_player.h"
#include "../SAFC_InnerModules/midi_editor.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct fragment
{
    float begin, end;
    std::uint32_t color;
    bool operator==(const fragment&) const = default;
};

// The original general interval-union algorithm is intentionally kept as an
// independent reference for the ordered stack optimization.
std::vector<fragment> reference_overlaps(const std::vector<simple_player::draw_data::note_span>& notes)
{
    std::vector<std::pair<float, float>> covered;
    std::vector<fragment> result;
    for (auto note = notes.rbegin(); note != notes.rend(); ++note)
    {
        float cursor = note->begin_y;
        for (const auto& [begin, end] : covered)
        {
            if (end <= cursor) continue;
            if (begin >= note->end_y) break;
            if (begin > cursor) result.push_back({cursor, std::min(begin, note->end_y), note->color});
            cursor = std::max(cursor, end);
            if (cursor >= note->end_y) break;
        }
        if (cursor < note->end_y) result.push_back({cursor, note->end_y, note->color});
        if (note->begin_y >= note->end_y) continue;
        auto first = covered.begin();
        while (first != covered.end() && first->second < note->begin_y) ++first;
        float begin = note->begin_y, end = note->end_y;
        while (first != covered.end() && first->first <= end)
        {
            begin = std::min(begin, first->first);
            end = std::max(end, first->second);
            first = covered.erase(first);
        }
        covered.insert(first, {begin, end});
    }
    return result;
}

void overlap_checks()
{
    auto player = std::make_unique<simple_player>();
    auto data = std::make_unique<simple_player::draw_data>();
    std::mt19937 random(20261015);
    for (unsigned scenario = 0; scenario < 500; ++scenario)
    {
        auto& spans = data->key_note_spans;
        spans.clear();
        for (unsigned i = 0, count = random() % 1000; i < count; ++i)
        {
            const float begin = float(random() % 1024) / 1024.f;
            const float length = float(random() % (scenario % 2 ? 16 : 1024)) / 1024.f;
            spans.push_back({begin, std::min(1.f, begin + length), i, i, false});
        }
        std::stable_sort(spans.begin(), spans.end(),
            [](const auto& a, const auto& b) { return a.begin_y < b.begin_y; });
        const auto expected = reference_overlaps(spans);
        std::vector<fragment> actual;
        player->overlaps_removal_v0(*data,
            [&](float begin, float end, std::uint32_t color) { actual.push_back({begin, end, color}); });
        require(actual == expected, "Overlap fragments or drawing order changed");
    }
    data->key_note_spans.clear();
    for (unsigned i = 0; i < 50000; ++i)
        data->key_note_spans.push_back({float(i * 2), float(i * 2 + 1), i, i, false});
    std::size_t emitted = 0;
    player->overlaps_removal_v0(*data, [&](float, float, std::uint32_t) { ++emitted; });
    require(emitted == 50000, "Disjoint notes must all remain visible");
}

void compare_display_snapshot(midi_editor& editor)
{
    std::vector<midi_editor::display_note> displayed;
    for (const auto start : {0ULL, 240ULL, 480ULL, 2000ULL, 15000ULL, 40000ULL})
        for (const auto low : {0, 60, 100})
            for (const auto track : {midi_editor::all_tracks, std::uint16_t(0), std::uint16_t(2)})
            {
                auto expected = editor.get_notes_in_range(start, start + 480, low, 127);
                std::erase_if(expected, [&](const auto& value)
                { return track != midi_editor::all_tracks && value.track_index != track; });
                editor.get_display_notes(displayed, start, start + 480, low, 127, track);
                require(displayed.size() == expected.size(), "Display snapshot range/track filtering changed");
                for (std::size_t i = 0; i < expected.size(); ++i)
                {
                    require(displayed[i].note == expected[i], "Display snapshot note order or content changed");
                    require(displayed[i].selected == editor.is_note_selected(expected[i].id),
                        "Display snapshot selection is stale");
                }
            }
}

void editor_snapshot_checks()
{
    const auto path = std::filesystem::current_path() /
        ("visual-query-test-" + std::to_string(GetCurrentProcessId()) + ".mid");
    auto notes_path = path;
    notes_path += ".notes.mid";
    struct cleanup
    {
        std::filesystem::path path, notes_path;
        ~cleanup()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(notes_path, error);
        }
    } file_lifetime{path, notes_path};
    {
        const unsigned char empty[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,224,
            'M','T','r','k',0,0,0,4,0,0xff,0x2f,0};
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(empty), sizeof(empty));
    }
    auto fixture = std::make_unique<midi_editor>();
    require(fixture->load_file(path.wstring()), "Load empty MIDI fixture");
    for (unsigned i = 0; i < 500; ++i)
        fixture->insert_note(i * 10, i * 10 + 30, i % 128, 90, i % 16, i % 4);
    fixture->insert_note(0, 30000, 64, 80, 0, 0);
    require(fixture->save_file(notes_path.wstring()), "Save loaded-note fixture");
    auto editor = std::make_unique<midi_editor>();
    require(editor->load_file(notes_path.wstring()), "Reload sorted base notes");
    compare_display_snapshot(*editor);
    editor->select_rect(0, 5000, 0, 127);
    compare_display_snapshot(*editor);
    editor->move_selected_notes(137, 2);
    compare_display_snapshot(*editor);
    editor->change_velocity_selected(70);
    compare_display_snapshot(*editor);
    const auto inserted = editor->insert_note(100, 20000, 70, 120, 2, 2);
    editor->select_note(inserted);
    compare_display_snapshot(*editor);
    editor->erase_notes({inserted});
    compare_display_snapshot(*editor);
    for (int i = 0; i < 4; ++i)
    {
        editor->undo();
        compare_display_snapshot(*editor);
    }
    for (int i = 0; i < 4; ++i)
    {
        editor->redo();
        compare_display_snapshot(*editor);
    }
    editor->clear_selection();
    compare_display_snapshot(*editor);
}
}

int main()
{
    try
    {
        overlap_checks();
        editor_snapshot_checks();
        std::cout << "Player overlap and editor display snapshot checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
