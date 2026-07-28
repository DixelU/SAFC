/**
 * MIDI Piano Roll Editor - Example Usage
 * 
 * This file demonstrates how to use the midi_editor module
 * and integrate it with single_midi_processor_2.h
 */

#include "SAFC_InnerModules/midi_editor.h"
#include "SAFC_InnerModules/midi_editor_integration.h"
#include "SAFC_InnerModules/single_midi_processor_2.h"
#include "SAFC_InnerModules/simple_player.h"

#include <iostream>
#include <chrono>
#include <thread>

void example_basic_usage()
{
    using namespace std;

    // Create editor instance
    midi_editor editor;

    // Load MIDI file via memory-mapped I/O
    if (!editor.load_file(L"song.mid"))
    {
        cout << "Failed to load MIDI file\n";
        return;
    }

    cout << "Loaded: " << editor.get_filename() << "\n";
    cout << "Notes: " << editor.get_note_count() << "\n";
    cout << "PPQN: " << editor.get_ppqn() << "\n";
    cout << "Duration: " << editor.get_total_seconds() << " seconds\n";

    // Set up piano roll view
    editor.set_view_range(0, editor.get_ticks_per_beat() * 4); // Show first 4 beats
    editor.set_view_keys(36, 96); // Show keys from C2 to C7

    // Query notes in view range
    auto visible_notes = editor.get_notes_in_range(
        0, editor.get_ticks_per_beat() * 4, 36, 96);
    
    cout << "Visible notes: " << visible_notes.size() << "\n";

    // Iterate through notes for rendering
    for (auto it = editor.begin_notes(); it != editor.end_notes(); ++it)
    {
        const auto& note = *it;
        // Render note in piano roll UI
        // render_note(note.start_tick, note.end_tick, note.key, note.velocity);
    }
}

void example_edit_operations()
{
    midi_editor editor;
    editor.load_file(L"song.mid");

    // Example 1: Insert a new note
    editor.insert_note(
        0,              // start tick
        480,            // end tick (1 beat at 480 PPQN)
        60,             // key (Middle C)
        100,            // velocity
        0               // channel
    );

    // Example 2: Select a region and delete notes
    editor.set_selection(
        0, 480 * 4,     // time range (4 beats)
        60, 72          // pitch range (C4 to C5)
    );
    editor.delete_selected_notes();

    // Example 3: Move selected notes
    editor.set_selection(100, 500, 50, 70);
    editor.move_selected_notes(120, 2); // +120 ticks, +2 semitones

    // Example 4: Resize a note
    editor.resize_note(
        0,      // start tick
        60,     // key
        960     // new length (2 beats)
    );

    // Example 5: Change velocity
    editor.set_selection(0, 10000, 0, 127);
    editor.change_velocity_selected(80);

    // Example 6: Quantize to grid
    editor.set_selection(0, 10000, 0, 127);
    editor.quantize_selected(120); // Quantize to 1/16 note (at 480 PPQN)

    // Undo/Redo
    editor.undo(); // Undo quantize
    editor.undo(); // Undo velocity change
    editor.redo(); // Redo velocity change

    // Save changes
    editor.save_file(L"song_edited.mid");
}

void example_piano_roll_rendering()
{
    midi_editor editor;
    editor.load_file(L"song.mid");

    // Viewport setup
    tick_type current_playback_tick = 0;
    tick_type viewport_duration = editor.get_ticks_per_beat() * 8; // 8 beats visible

    // Main render loop (pseudo-code for a GUI framework)
    /*
    while (application_running)
    {
        // Update playback position
        current_playback_tick = get_current_playback_position();
        
        // Center view on playback position
        editor.set_view_range(
            current_playback_tick - viewport_duration / 2,
            viewport_duration
        );

        // Clear screen
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw grid
        draw_time_grid(editor.get_ticks_per_beat(), viewport_duration);
        draw_keyboard(0, 127);

        // Draw notes
        for (auto it = editor.begin_notes(); it != editor.end_notes(); ++it)
        {
            const auto& note = *it;
            
            // Convert MIDI ticks to screen coordinates
            float x = ticks_to_screen_x(note.start_tick);
            float width = ticks_to_screen_x(note.length());
            float y = key_to_screen_y(note.key);
            float height = key_height();

            // Color based on velocity or channel
            glColor3ub(
                255 - note.velocity,
                100 + note.velocity / 2,
                50
            );

            // Draw note rectangle
            glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + width, y);
            glVertex2f(x + width, y + height);
            glVertex2f(x, y + height);
            glEnd();

            // Draw note border
            glColor3ub(0, 0, 0);
            glBegin(GL_LINE_LOOP);
            glVertex2f(x, y);
            glVertex2f(x + width, y);
            glVertex2f(x + width, y + height);
            glVertex2f(x, y + height);
            glEnd();
        }

        // Draw playback cursor
        float cursor_x = ticks_to_screen_x(current_playback_tick);
        glColor3ub(255, 0, 0);
        glBegin(GL_LINES);
        glVertex2f(cursor_x, 0);
        glVertex2f(cursor_x, screen_height);
        glEnd();

        // Swap buffers
        swap_buffers();
    }
    */
}

void example_integration_with_processor()
{
    // This shows how to use editor functors with single_midi_processor_2

    midi_editor editor;
    editor.load_file(L"song.mid");

    // Make some edits
    editor.set_selection(0, 480 * 4, 60, 72);
    editor.change_velocity_selected(100);

    // Create processor instance
    single_midi_processor_2 processor;

    // Create filter from editor changes
    // Note: This requires the integration layer
    midi_editor_processor_integration::bulk_edit_filter edit_filter;

    // Add modifications to filter
    auto selected_notes = editor.get_notes_in_range(
        0, 480 * 4, 60, 72);
    
    for (const auto& note : selected_notes)
    {
        edit_filter.add_velocity_change(
            note.start_tick, note.end_tick,
            note.key, note.channel,
            100
        );
    }

    // Configure processor settings
    single_midi_processor_2::settings_obj settings;
    settings.old_ppqn = editor.get_ppqn();
    settings.new_ppqn = editor.get_ppqn();
    settings.offset = 0;
    settings.filter.pass_notes = true;
    settings.filter.pass_tempo = true;
    settings.filter.pass_pitch = true;
    settings.filter.pass_other = true;

    // Process with edits applied
    // processor.process_file(
    //     editor.get_filename(),
    //     L"output.mid",
    //     settings,
    //     { {0x90, edit_filter} }  // Add filter for note-on events
    // );
}

void example_realtime_playback_with_edits()
{
    // Integration with simple_player for real-time playback with edits

    simple_player player;
    player.init();

    midi_editor editor;
    editor.load_file(L"song.mid");

    // Make edits
    editor.insert_note(0, 480, 60, 100);

    // Create integration wrapper
    midi_editor_processor_integration::editor_playback_filter playback_filter(&editor);

    // During playback, the filter will:
    // 1. Silence deleted notes
    // 2. Apply velocity overrides
    // 3. Apply pitch shifts

    // Start playback
    // player.open(editor.get_filename());
    // player.get_visuals(); // Get visuals for piano roll display

    // Main playback loop with visual updates
    /*
    while (player.is_playing())
    {
        const auto& state = player.get_state();
        const auto& visuals = player.get_visuals();

        // Update editor view to match playback
        editor.set_view_range(
            state.current_tick - 480 * 4,
            480 * 8
        );

        // Render piano roll with falling notes
        // visuals are automatically updated by simple_player
        // editor provides the note data for rendering
    }
    */
}

void example_batch_processing()
{
    // Apply multiple edits and process them together

    midi_editor editor;
    editor.load_file(L"song.mid");

    // Queue multiple operations
    std::vector<std::unique_ptr<midi_editor::edit_operation>> operations;

    // Operation 1: Delete all notes in range
    auto del_op = std::make_unique<midi_editor::delete_notes_op>(
        midi_editor::selection(0, 480 * 4, 60, 72));
    operations.push_back(std::move(del_op));

    // Operation 2: Move remaining notes
    auto move_op = std::make_unique<midi_editor::move_notes_op>(
        midi_editor::selection(0, 10000, 0, 127), 120, 0);
    operations.push_back(std::move(move_op));

    // Execute operations
    for (auto& op : operations)
    {
        op->execute(editor);
    }

    // Save result
    editor.save_file(L"processed.mid");
}

void example_advanced_queries()
{
    midi_editor editor;
    editor.load_file(L"song.mid");

    // Get all notes on a specific key
    auto notes_on_c4 = editor.get_notes_on_key(60);
    std::cout << "Notes on C4: " << notes_on_c4.size() << "\n";

    // Get all notes at a specific time
    auto notes_at_beat_1 = editor.get_notes_at_tick(480);
    std::cout << "Notes at tick 480: " << notes_at_beat_1.size() << "\n";

    // Get notes in complex range
    auto bass_notes = editor.get_notes_in_range(0, 100000, 24, 48);
    auto melody_notes = editor.get_notes_in_range(0, 100000, 60, 84);

    // Analyze note density
    std::map<tick_type, int> notes_per_beat;
    for (const auto& note : editor.get_all_notes())
    {
        tick_type beat = note.start_tick / editor.get_ticks_per_beat();
        notes_per_beat[beat]++;
    }

    // Find busiest beat
    tick_type busiest_beat = 0;
    int max_notes = 0;
    for (const auto& [beat, count] : notes_per_beat)
    {
        if (count > max_notes)
        {
            max_notes = count;
            busiest_beat = beat;
        }
    }
    std::cout << "Busiest beat: " << busiest_beat << " with " << max_notes << " notes\n";
}

void example_undo_redo_workflow()
{
    midi_editor editor;
    editor.load_file(L"song.mid");

    // Make a series of edits
    editor.insert_note(0, 480, 60, 100);
    editor.insert_note(480, 960, 62, 100);
    editor.insert_note(960, 1440, 64, 100);

    std::cout << "Notes after inserts: " << editor.get_note_count() << "\n";

    // Undo last insert
    editor.undo();
    std::cout << "After undo: " << editor.get_note_count() << "\n";

    // Redo
    editor.redo();
    std::cout << "After redo: " << editor.get_note_count() << "\n";

    // Undo all
    while (editor.can_undo())
    {
        editor.undo();
    }
    std::cout << "After undo all: " << editor.get_note_count() << "\n";
}

void example_custom_operations()
{
    // Create custom edit operation
    struct transpose_octave_op : midi_editor::edit_operation
    {
        midi_editor::selection sel;
        std::vector<std::pair<midi_editor::piano_note, midi_editor::piano_note>> changes;

        transpose_octave_op(midi_editor::selection s) : sel(s) {}

        void execute(midi_editor& editor) override
        {
            changes.clear();
            for (auto& note : editor.notes)
            {
                if (sel.intersects(note))
                {
                    midi_editor::piano_note before = note;
                    int new_key = int(note.key) + 12; // +1 octave
                    if (new_key <= 127)
                    {
                        note.key = midi_editor::base_type(new_key);
                        changes.emplace_back(before, note);
                    }
                }
            }
            editor.mark_dirty();
        }

        void undo(midi_editor& editor) override
        {
            for (auto& [before, after] : changes)
            {
                for (auto& note : editor.notes)
                {
                    if (note.start_tick == after.start_tick &&
                        note.key == after.key)
                    {
                        note.key = before.key;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override { return "Transpose Octave"; }
    };

    midi_editor editor;
    editor.load_file(L"song.mid");

    editor.set_selection(0, 10000, 0, 127);
    
    auto op = std::make_unique<transpose_octave_op>(editor.get_selection());
    op->execute(editor);
    
    // Note: For proper undo support, you'd push to undo stack
    // editor.push_undo(std::move(op));
}

int main()
{
    std::cout << "=== MIDI Piano Roll Editor Examples ===\n\n";

    std::cout << "1. Basic Usage:\n";
    example_basic_usage();
    std::cout << "\n";

    std::cout << "2. Edit Operations:\n";
    example_edit_operations();
    std::cout << "\n";

    std::cout << "3. Integration with Processor:\n";
    example_integration_with_processor();
    std::cout << "\n";

    std::cout << "4. Advanced Queries:\n";
    example_advanced_queries();
    std::cout << "\n";

    std::cout << "5. Undo/Redo Workflow:\n";
    example_undo_redo_workflow();
    std::cout << "\n";

    std::cout << "Examples completed.\n";
    return 0;
}
