# MIDI Piano Roll Editor

## Overview

A memory-efficient MIDI piano roll editor that reads directly from memory-mapped files and integrates with the existing `single_midi_processor_2.h` filter system.

## Architecture

### Source layout

- `midi_editor.h` contains the editor model and public interface.
- `midi_editor_tools.cpp` contains Chopper, Flip, Claw, LFO, and reversible
  preview implementation.
- `midi_editor_viewer.h` contains piano-roll drawing and interaction.
- `midi_editor_tools_ui.h/.cpp` contains score-tool callbacks and settings
  window construction.
- `simple_player_viewer.h` contains player visualization; playback remains in
  `simple_player.h`, while its reusable SPSC queue comes from
  `utility/include/buffered_queue_spsc.h`.

File I/O and live playback-source generation remain in `midi_editor.h` for now.
They share private parser/editor representation and should move after a
deliberate private implementation boundary is introduced.

```
┌─────────────────────────────────────────────────────────────────┐
│                        midi_editor.h                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │  Piano Roll     │  │   Functor       │  │   mmap-based    │ │
│  │  Data Structure │  │   System        │  │   File I/O      │ │
│  │                 │  │                 │  │                 │ │
│  │  - piano_note   │  │  - insert_note  │  │  - mapped reader│ │
│  │  - track_info   │  │  - delete_notes │  │  - Direct parse │ │
│  │  - selection    │  │  - move_notes   │  │    from file    │ │
│  │                 │  │  - resize_note  │  │                 │ │
│  │                 │  │  - velocity_ch  │  │                 │ │
│  │                 │  │  - quantize     │  │                 │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              Undo/Redo Command System                   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              midi_editor_integration.h                          │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │   Filter Adapters for single_midi_processor_2.h           │ │
│  │                                                           │ │
│  │   - editor_playback_filter (real-time)                    │ │
│  │   - bulk_edit_filter (batch processing)                   │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              single_midi_processor_2.h                          │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │   Event Transforming Filters                              │ │
│  │   - Applied during MIDI processing                        │ │
│  │   - Keyed by event type (0x80-0xF0)                       │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. Memory-Mapped File Reading

Like `simple_player.h`, the editor reads MIDI data directly from memory-mapped files:

```cpp
std::unique_ptr<dixelu::memory_mapped_file_reader> mmap_file;
```

**Benefits:**
- Zero-copy file access
- Minimal RAM usage for large files
- Fast random access for piano roll queries

**Trade-offs:**
- File must remain open during editing
- Not suitable for streaming from network

### 2. Loaded Base + Sparse Edit Overlay

The editor keeps the loaded piano-roll notes in one time-sorted base vector while
interactive changes are stored in a sparse overlay. Changed/deleted base-note IDs
are suppressed without shifting or re-sorting the source vector:

```cpp
struct piano_note {
    tick_type start_tick;
    tick_type end_tick;
    std::uint8_t key;
    std::uint8_t velocity;
    std::uint8_t channel;
    std::uint8_t track_index;
};
```

**Benefits:**
- Average O(1) lookup and mutation for one-note insert/delete/move/undo
- Easy piano roll rendering
- Range queries remain binary-searched over the base and scan only session edits

**Trade-offs:**
- Duplicate storage (editor notes + mmap)
- Four bytes per loaded note for the dense ID-to-base-index table
- Saving/playback materializes the logical base-plus-overlay note sequence

### 3. Functor-Based Edit System

All edits are functors implementing the command pattern:

```cpp
struct edit_operation {
    virtual void execute(midi_editor&) = 0;
    virtual void undo(midi_editor&) = 0;
    virtual void redo(midi_editor&) = 0;
};
```

**Benefits:**
- Natural undo/redo support
- Composable operations
- Clear separation of concerns

### 4. Filter Integration

Editor operations can be translated to `single_midi_processor_2.h` filters:

```cpp
using filter_func_t = std::function<bool(
    const data_iterator&,
    const data_iterator&,
    const data_iterator&,
    single_track_data&)>;
```

**Benefits:**
- Reuse existing processor infrastructure
- Real-time edit application during playback
- Batch processing support

## Usage Examples

### Basic Loading and Viewing

```cpp
midi_editor editor;
editor.load_file(L"song.mid");

// Set view to show first 4 beats
editor.set_view_range(0, editor.get_ticks_per_beat() * 4);
editor.set_view_keys(36, 96); // C2 to C7

// Iterate visible notes
for (auto it = editor.begin_notes(); it != editor.end_notes(); ++it) {
    const auto& note = *it;
    // Render note...
}
```

### Edit Operations

```cpp
// Insert note
editor.insert_note(0, 480, 60, 100);

// Select and delete
editor.set_selection(0, 480 * 4, 60, 72);
editor.delete_selected_notes();

// Move selection
editor.move_selected_notes(120, 2); // +120 ticks, +2 semitones

// Quantize
editor.quantize_selected(120); // 1/16 note grid

// Undo/Redo
editor.undo();
editor.redo();
```

### Score tools

The editor includes four undoable, selection-aware score tools. When there is
no note selection they operate on the active track.

- **Chopper (Alt+U)** splits notes using a beat subdivision, time multiplier,
  optional gap, and relative or absolute grid alignment.
- **Flip (Alt+Y)** mirrors notes horizontally and/or vertically, with an option
  to preserve note lengths while reversing start positions.
- **Claw machine (Alt+W)** slices and removes periodic regions, warps their
  timing, optionally removes very short results, and can stretch the result
  back to its original duration.
- **LFO (Alt+O)** writes sine, triangle, or square modulation to the selected
  bottom lane. Note velocity is applied to target notes; pitch bend, pan CC10,
  and channel-volume CC7 are written as MIDI events over the selected or visible
  time range.

Each shortcut opens a settings window with editable values, wheel increments,
checkboxes, Accept, and Cancel. Settings update a reversible live preview in the
piano roll. Accept turns that preview into one undo-history entry; Cancel restores
the notes, controller events, selection, and dirty state from before the dialog.

The bottom lane also edits the global tempo map. Tempo uses a logarithmic vertical
scale spanning the complete MIDI Set Tempo range (about 3.576 to 60,000,000 BPM).
Ctrl+wheel zooms the BPM scale around the cursor, Shift+wheel pans it, and
Ctrl+Shift+wheel restores the full range. Space toggles playback from the visible
start tick; Ctrl+S opens the edited-MIDI save workflow.

### Integration with Processor

```cpp
// Create filter from editor state
midi_editor_processor_integration::bulk_edit_filter filter;

// Add modifications
filter.add_velocity_change(0, 480, 60, 0, 100);
filter.add_deletion(480, 960, 62, 0);

// Apply during processing
// processor.process_file(input, output, settings, { {0x90, filter} });
```

## Performance Characteristics

| Operation | Time Complexity | Memory |
|-----------|----------------|--------|
| Load file | O(n log n) | O(n)* |
| Insert note | O(1) average | O(1) |
| Delete note | O(1) average | O(1) |
| Move notes | O(k) average | O(k) |
| Query range | O(log n + v + e) | O(v) |
| Undo/Redo | O(k) | O(k) |
| Save file | O(n log n) | O(n) |

*n = loaded notes, k = affected notes, v = notes intersecting the viewport,
e = sparse session edits
*File mmap not counted in memory

## Memory Layout

```
Memory Map:
┌─────────────────────────────────────────┐
│  mmap_file (memory_mapped_file_reader)  │
│  ┌───────────────────────────────────┐  │
│  │  Raw MIDI bytes                   │  │
│  │  - MThd header                    │  │
│  │  - MTrk chunks                    │  │
│  │  - All events (compressed)        │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘

Editor State:
┌─────────────────────────────────────────┐
│  notes (std::vector<piano_note>)        │
│  ┌───────────────────────────────────┐  │
│  │  [note0] [note1] [note2] ...      │  │
│  │  start  start  start              │  │
│  │  end    end    end                │  │
│  │  key    key    key                │  │
│  │  vel    vel    vel                │  │
│  └───────────────────────────────────┘  │
│                                         │
│  undo_stack (std::vector<op_ptr>)       │
│  redo_stack (std::vector<op_ptr>)       │
│  edited_notes / suppressed base ids     │
│  compact selection bitset + dense ids   │
└─────────────────────────────────────────┘
```

## Thread Safety

The editor uses a mutex for write operations:

```cpp
std::mutex editor_mutex;

bool load_file(const std::wstring& filepath) {
    std::lock_guard<std::mutex> lock(editor_mutex);
    // ...
}
```

**Guidelines:**
- Read operations (queries, iteration) are thread-safe
- Write operations (edits, load, save) acquire the mutex
- For UI integration, keep edits on the main thread
- Playback can run on a separate thread using filters

## Limitations and Future Work

### Current Limitations

1. **No multi-track editing**: All notes are in a flat list
2. **No sysex support**: System exclusive events are skipped
3. **No running status optimization**: Save always writes full events

### Potential Enhancements

1. **Incremental loading**: Load only visible portion of large files
2. **Note grouping**: Group notes by chord/voice for smarter editing
3. **Automation curves**: Support for CC event editing
4. **MIDI effects**: Arpeggiator, chord generator as functors
5. **Collaborative editing**: CRDT-based sync for multi-user

## Integration Checklist

To integrate with your existing codebase:

- [ ] Include `midi_editor.h` in your project
- [ ] Create `midi_editor` instance
- [ ] Call `load_file()` with MIDI path
- [ ] Use query methods for piano roll rendering
- [ ] Apply edits via operation methods
- [ ] Optionally integrate with `single_midi_processor_2` via filters
- [ ] Call `save_file()` to persist changes

## File Structure

```
SAFC_InnerModules/
├── midi_editor.h              # Core editor implementation
├── midi_editor_integration.h  # Processor integration layer
├── midi_editor_examples.cpp   # Usage examples
└── MIDI_EDITOR.md             # This documentation
```

## Dependencies

- C++17 or later (std::optional, structured bindings)
- Windows API (used by the consolidated memory-mapped reader)
- Shared utility and existing SAFC headers:
  - `utility/include/memory_mapped_file_reader.h` (memory-mapped file reader)
  - `single_midi_processor_2.h` (filter types)
  - `single_midi_info_collector.h` (reference)

## License

Same as the parent SAFC project.
