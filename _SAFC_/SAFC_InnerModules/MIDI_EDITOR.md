# MIDI Piano Roll Editor

## Overview

A memory-efficient MIDI piano roll editor that reads directly from memory-mapped files and integrates with the existing `single_midi_processor_2.h` filter system.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        midi_editor.h                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │  Piano Roll     │  │   Functor       │  │   mmap-based    │ │
│  │  Data Structure │  │   System        │  │   File I/O      │ │
│  │                 │  │                 │  │                 │ │
│  │  - piano_note   │  │  - insert_note  │  │  - bbb_mmap     │ │
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
std::unique_ptr<bbb_mmap> mmap_file;
```

**Benefits:**
- Zero-copy file access
- Minimal RAM usage for large files
- Fast random access for piano roll queries

**Trade-offs:**
- File must remain open during editing
- Not suitable for streaming from network

### 2. Dual Representation

The editor maintains notes in a piano-roll friendly format while the original MIDI remains in mmap:

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
- O(1) note operations (insert, delete, move)
- Easy piano roll rendering
- Simple query interface

**Trade-offs:**
- Duplicate storage (editor notes + mmap)
- Need to synchronize on save

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
| Load file | O(n) | O(1)* |
| Insert note | O(1) amortized | O(1) |
| Delete note | O(n) | O(1) |
| Move notes | O(n) | O(k) |
| Query range | O(n) | O(k) |
| Undo/Redo | O(k) | O(k) |
| Save file | O(n log n) | O(n) |

*n = total notes, k = affected notes
*File mmap not counted in memory

## Memory Layout

```
Memory Map:
┌─────────────────────────────────────────┐
│  mmap_file (bbb_mmap)                   │
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
2. **No tempo track editing**: Tempo changes are read-only
3. **No sysex support**: System exclusive events are skipped
4. **No running status optimization**: Save always writes full events

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
- Windows API (for memory-mapped files via bbb_mmap)
- Existing SAFC headers:
  - `bbb_ffio.h` (memory-mapped file wrapper)
  - `single_midi_processor_2.h` (filter types)
  - `single_midi_info_collector.h` (reference)

## License

Same as the parent SAFC project.
