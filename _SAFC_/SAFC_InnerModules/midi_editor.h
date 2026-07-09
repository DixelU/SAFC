#pragma once
#ifndef SAFC_MIDI_EDITOR
#define SAFC_MIDI_EDITOR

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <optional>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <set>

#include "../bbb_ffio.h"
#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"

/**
 * MIDI Piano Roll Editor
 * 
 * Architecture:
 * - Reads MIDI directly from mmap file (like simple_player.h)
 * - Maintains editor-friendly note representation for piano roll
 * - Uses functor-based transformation system compatible with single_midi_processor_2.h
 * - Supports undo/redo through command pattern
 */

struct midi_editor
{
    using tick_type = std::uint64_t;
    using sgtick_type = std::int64_t;
    using base_type = std::uint8_t;

    // ========================================================================
    // Core Data Structures
    // ========================================================================

    /**
     * Editor-friendly note representation for piano roll
     */
    struct piano_note
    {
        tick_type start_tick;
        tick_type end_tick;
        std::uint8_t key;          // 0-127
        std::uint8_t velocity;     // 1-127
        std::uint8_t channel;      // 0-15
        std::uint8_t track_index;  // Track identifier

        piano_note() : start_tick(0), end_tick(0), key(60), velocity(100), channel(0), track_index(0) {}
        
        piano_note(tick_type start, tick_type end, std::uint8_t k, std::uint8_t vel, 
                   std::uint8_t ch = 0, std::uint8_t track = 0)
            : start_tick(start), end_tick(end), key(k), velocity(vel), channel(ch), track_index(track) {}

        tick_type length() const { return end_tick - start_tick; }
        
        bool operator<(const piano_note& other) const
        {
            if (start_tick != other.start_tick) return start_tick < other.start_tick;
            if (key != other.key) return key < other.key;
            return track_index < other.track_index;
        }
    };

    /**
     * Track metadata
     */
    struct track_info
    {
        std::string name;
        std::uint8_t channel;  // Primary channel (0-15) or 0xFF for multi-channel
        bool is_visible;
        bool is_solo;
        bool is_muted;

        track_info() : channel(0xFF), is_visible(true), is_solo(false), is_muted(false) {}
    };

    /**
     * Selection state for piano roll operations
     */
    struct selection
    {
        static constexpr std::uint8_t all_tracks = 0xFF;

        tick_type begin_tick;
        tick_type end_tick;
        std::uint8_t key_begin;
        std::uint8_t key_end;
        std::uint8_t track_filter; // all_tracks or a specific track index
        bool has_selection;

        selection() : begin_tick(0), end_tick(0), key_begin(0), key_end(127),
                      track_filter(all_tracks), has_selection(false) {}

        bool is_active() const { return has_selection && begin_tick < end_tick; }

        bool contains(tick_type tick, std::uint8_t key) const
        {
            if (!has_selection) return false;
            return tick >= begin_tick && tick < end_tick &&
                   key >= key_begin && key <= key_end;
        }

        bool intersects(const piano_note& note) const
        {
            if (!has_selection) return false;
            if (track_filter != all_tracks && note.track_index != track_filter) return false;
            return note.start_tick < end_tick && note.end_tick > begin_tick &&
                   note.key >= key_begin && note.key <= key_end;
        }
    };

    // ========================================================================
    // Functor System - Edit Operations
    // ========================================================================

    /**
     * Base class for editor operations (command pattern)
     * Each operation can be executed, undone, and redone
     */
    struct edit_operation
    {
        virtual ~edit_operation() = default;
        virtual void execute(midi_editor&) = 0;
        virtual void undo(midi_editor&) = 0;
        virtual void redo(midi_editor& ed) { execute(ed); }
        virtual std::string description() const = 0;
    };

    /**
     * Functor for inserting a note
     */
    struct insert_note_op : edit_operation
    {
        piano_note note;
        
        insert_note_op(piano_note n) : note(n) {}
        
        void execute(midi_editor& editor) override
        {
            editor.notes.push_back(note);
            editor.mark_dirty();
        }
        
        void undo(midi_editor& editor) override
        {
            editor.remove_note_by_position(note);
            editor.mark_dirty();
        }

        std::string description() const override { return "Insert Note"; }
    };

    /**
     * Functor for deleting selected notes
     */
    struct delete_notes_op : edit_operation
    {
        std::vector<piano_note> removed_notes;
        selection sel;

        delete_notes_op(selection s) : sel(s) {}
        
        void execute(midi_editor& editor) override
        {
            removed_notes.clear();
            auto& notes = editor.notes;
            notes.erase(
                std::remove_if(notes.begin(), notes.end(),
                    [this, &editor](const piano_note& note) {
                        if (sel.intersects(note)) {
                            removed_notes.push_back(note);
                            return true;
                        }
                        return false;
                    }),
                notes.end());
            editor.mark_dirty();
        }
        
        void undo(midi_editor& editor) override
        {
            for (auto& note : removed_notes)
                editor.notes.push_back(note);
            editor.mark_dirty();
        }

        std::string description() const override { return "Delete Notes"; }
    };

    /**
     * Functor for moving notes by delta ticks and/or semitones
     */
    struct move_notes_op : edit_operation
    {
        selection sel;
        std::vector<std::pair<piano_note, piano_note>> changes; // before, after
        sgtick_type delta_ticks;
        int delta_keys;

        move_notes_op(selection s, sgtick_type dt = 0, int dk = 0)
            : sel(s), delta_ticks(dt), delta_keys(dk) {}
        
        void execute(midi_editor& editor) override
        {
            changes.clear();
            for (auto& note : editor.notes)
            {
                if (sel.intersects(note))
                {
                    piano_note before = note;
                    
                    if (delta_ticks != 0)
                    {
                        auto new_start = sgtick_type(note.start_tick) + delta_ticks;
                        auto new_end = sgtick_type(note.end_tick) + delta_ticks;
                        if (new_start >= 0 && new_end >= 0)
                        {
                            note.start_tick = tick_type(new_start);
                            note.end_tick = tick_type(new_end);
                        }
                    }
                    
                    if (delta_keys != 0)
                    {
                        int new_key = int(note.key) + delta_keys;
                        if (new_key >= 0 && new_key <= 127)
                            note.key = base_type(new_key);
                    }
                    
                    if (before.start_tick != note.start_tick || before.key != note.key)
                        changes.emplace_back(before, note);
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
                        note.end_tick == after.end_tick &&
                        note.key == after.key &&
                        note.channel == after.channel &&
                        note.track_index == after.track_index)
                    {
                        note.start_tick = before.start_tick;
                        note.end_tick = before.end_tick;
                        note.key = before.key;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override 
        { 
            return "Move Notes (" + std::to_string(delta_ticks) + " ticks, " + 
                   std::to_string(delta_keys) + " semitones)"; 
        }
    };

    /**
     * Functor for resizing note length
     */
    struct resize_note_op : edit_operation
    {
        piano_note target_note;
        tick_type new_length;
        tick_type old_length = 0;
        bool applied = false;

        resize_note_op(piano_note n, tick_type len) : target_note(n), new_length(len) {}

        void execute(midi_editor& editor) override
        {
            applied = false;
            for (auto& note : editor.notes)
            {
                if (note.start_tick == target_note.start_tick &&
                    note.key == target_note.key &&
                    note.channel == target_note.channel &&
                    note.track_index == target_note.track_index)
                {
                    old_length = note.length();
                    note.end_tick = note.start_tick + new_length;
                    applied = true;
                    break;
                }
            }
            editor.mark_dirty();
        }

        void undo(midi_editor& editor) override
        {
            if (!applied)
                return;
            for (auto& note : editor.notes)
            {
                if (note.start_tick == target_note.start_tick &&
                    note.key == target_note.key &&
                    note.channel == target_note.channel &&
                    note.track_index == target_note.track_index)
                {
                    note.end_tick = note.start_tick + old_length;
                    break;
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override { return "Resize Note"; }
    };

    /**
     * Functor for changing note velocity
     */
    struct velocity_change_op : edit_operation
    {
        selection sel;
        std::uint8_t new_velocity;
        std::vector<std::pair<piano_note, std::uint8_t>> changes; // note, old_velocity

        velocity_change_op(selection s, std::uint8_t vel) : sel(s), new_velocity(vel) {}
        
        void execute(midi_editor& editor) override
        {
            changes.clear();
            for (auto& note : editor.notes)
            {
                if (sel.intersects(note))
                {
                    changes.emplace_back(note, note.velocity);
                    note.velocity = new_velocity;
                }
            }
            editor.mark_dirty();
        }
        
        void undo(midi_editor& editor) override
        {
            for (auto& [note, old_vel] : changes)
            {
                for (auto& n : editor.notes)
                {
                    if (n.start_tick == note.start_tick &&
                        n.key == note.key &&
                        n.channel == note.channel &&
                        n.track_index == note.track_index)
                    {
                        n.velocity = old_vel;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override { return "Change Velocity"; }
    };

    /**
     * Functor for adjusting note velocity by a relative amount
     */
    struct velocity_adjust_op : edit_operation
    {
        selection sel;
        int delta;
        std::vector<std::pair<piano_note, std::uint8_t>> changes; // note (pre-change), old_velocity

        velocity_adjust_op(selection s, int d) : sel(s), delta(d) {}

        void execute(midi_editor& editor) override
        {
            changes.clear();
            for (auto& note : editor.notes)
            {
                if (sel.intersects(note))
                {
                    changes.emplace_back(note, note.velocity);
                    note.velocity = std::uint8_t(std::clamp(int(note.velocity) + delta, 1, 127));
                }
            }
            editor.mark_dirty();
        }

        void undo(midi_editor& editor) override
        {
            for (auto& [note, old_vel] : changes)
            {
                for (auto& n : editor.notes)
                {
                    if (n.start_tick == note.start_tick &&
                        n.key == note.key &&
                        n.channel == note.channel &&
                        n.track_index == note.track_index)
                    {
                        n.velocity = old_vel;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override
        {
            return "Adjust Velocity (" + std::to_string(delta) + ")";
        }
    };

    /**
     * Functor for deleting one exact note (eraser tool)
     */
    struct delete_single_note_op : edit_operation
    {
        piano_note target;
        bool applied = false;

        delete_single_note_op(piano_note n) : target(n) {}

        void execute(midi_editor& editor) override
        {
            applied = false;
            auto& notes = editor.notes;
            for (auto it = notes.begin(); it != notes.end(); ++it)
            {
                if (it->start_tick == target.start_tick &&
                    it->end_tick == target.end_tick &&
                    it->key == target.key &&
                    it->channel == target.channel &&
                    it->track_index == target.track_index)
                {
                    notes.erase(it);
                    applied = true;
                    break;
                }
            }
            editor.mark_dirty();
        }

        void undo(midi_editor& editor) override
        {
            if (applied)
                editor.notes.push_back(target);
            editor.mark_dirty();
        }

        std::string description() const override { return "Erase Note"; }
    };

    /**
     * Undo entry for a velocity-lane gesture: per-note old/new velocities recorded
     * while the drag was applied transiently, committed as one operation.
     */
    struct recorded_velocity_op : edit_operation
    {
        struct entry
        {
            piano_note note; // identity (velocity field is ignored for matching)
            std::uint8_t old_velocity;
            std::uint8_t new_velocity;
        };
        std::vector<entry> entries;

        recorded_velocity_op(std::vector<entry>&& e) : entries(std::move(e)) {}

        void apply(midi_editor& editor, bool use_new)
        {
            for (const auto& en : entries)
            {
                for (auto& n : editor.notes)
                {
                    if (n.start_tick == en.note.start_tick &&
                        n.end_tick == en.note.end_tick &&
                        n.key == en.note.key &&
                        n.channel == en.note.channel &&
                        n.track_index == en.note.track_index)
                    {
                        n.velocity = use_new ? en.new_velocity : en.old_velocity;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        void execute(midi_editor& editor) override { apply(editor, true); }
        void undo(midi_editor& editor) override { apply(editor, false); }

        std::string description() const override { return "Edit Velocities"; }
    };

    /**
     * Functor for quantizing notes to grid
     */
    struct quantize_op : edit_operation
    {
        selection sel;
        tick_type grid_resolution;
        std::vector<std::pair<piano_note, piano_note>> changes;

        quantize_op(selection s, tick_type grid) : sel(s), grid_resolution(grid) {}
        
        void execute(midi_editor& editor) override
        {
            changes.clear();
            for (auto& note : editor.notes)
            {
                if (sel.intersects(note))
                {
                    piano_note before = note;
                    
                    // Quantize start
                    auto remainder = note.start_tick % grid_resolution;
                    if (remainder < grid_resolution / 2)
                        note.start_tick -= remainder;
                    else
                        note.start_tick += (grid_resolution - remainder);
                    
                    // Adjust end to maintain relative length
                    auto length_before = before.length();
                    auto length_after = length_before;
                    // Optionally quantize length too
                    auto end_remainder = note.end_tick % grid_resolution;
                    if (end_remainder < grid_resolution / 2)
                        note.end_tick = note.start_tick + (length_before - end_remainder);
                    else
                        note.end_tick = note.start_tick + (length_before + (grid_resolution - end_remainder));
                    
                    if (before.start_tick != note.start_tick)
                        changes.emplace_back(before, note);
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
                        note.end_tick == after.end_tick &&
                        note.key == after.key)
                    {
                        note.start_tick = before.start_tick;
                        note.end_tick = before.end_tick;
                        break;
                    }
                }
            }
            editor.mark_dirty();
        }

        std::string description() const override { return "Quantize to Grid"; }
    };

    // ========================================================================
    // Filter Functors for single_midi_processor_2.h Integration
    // ========================================================================

    /**
     * Creates an event_transforming_filter that applies editor changes
     * This bridges the piano roll edits to the processor's filter system
     */
    using filter_func_t = std::function<bool(
        const single_midi_processor_2::data_iterator&,
        const single_midi_processor_2::data_iterator&,
        const single_midi_processor_2::data_iterator&,
        single_midi_processor_2::single_track_data&)>;

    /**
     * Filter that removes notes marked for deletion
     */
    static filter_func_t make_deletion_filter(const std::vector<piano_note>& notes_to_remove)
    {
        return [notes_to_remove](
            const single_midi_processor_2::data_iterator& begin,
            const single_midi_processor_2::data_iterator& end,
            const single_midi_processor_2::data_iterator& cur,
            single_midi_processor_2::single_track_data& std_ref) -> bool
        {
            const auto& tick = single_midi_processor_2::get_value<tick_type>(cur, 
                single_midi_processor_2::tick_position);
            const auto& type = single_midi_processor_2::get_value<base_type>(cur, 
                single_midi_processor_2::event_type);

            if ((type & 0xF0) == 0x90 || (type & 0xF0) == 0x80)
            {
                const auto& key = single_midi_processor_2::get_value<base_type>(cur, 
                    single_midi_processor_2::event_param1);
                const auto& channel = type & 0x0F;

                for (const auto& note : notes_to_remove)
                {
                    if (note.key == key && note.channel == channel &&
                        tick >= note.start_tick && tick <= note.end_tick)
                    {
                        // Disable this event
                        auto& ref_tick = single_midi_processor_2::get_value<tick_type>(
                            begin, single_midi_processor_2::get_value<tick_type>(cur, 
                                single_midi_processor_2::event_param3));
                        ref_tick = single_midi_processor_2::disable_tick;
                        return false;
                    }
                }
            }
            return true;
        };
    }

    /**
     * Filter that modifies velocity of matching notes
     */
    static filter_func_t make_velocity_filter(const std::vector<piano_note>& velocity_changes)
    {
        return [velocity_changes](
            const single_midi_processor_2::data_iterator& begin,
            const single_midi_processor_2::data_iterator& end,
            const single_midi_processor_2::data_iterator& cur,
            single_midi_processor_2::single_track_data& std_ref) -> bool
        {
            const auto& tick = single_midi_processor_2::get_value<tick_type>(cur, 
                single_midi_processor_2::tick_position);
            const auto& type = single_midi_processor_2::get_value<base_type>(cur, 
                single_midi_processor_2::event_type);

            if ((type & 0xF0) == 0x90)
            {
                const auto& key = single_midi_processor_2::get_value<base_type>(cur, 
                    single_midi_processor_2::event_param1);
                const auto& channel = type & 0x0F;

                for (const auto& note : velocity_changes)
                {
                    if (note.key == key && note.channel == channel &&
                        tick >= note.start_tick && tick <= note.end_tick)
                    {
                        auto& vel = single_midi_processor_2::get_value<base_type>(cur, 
                            single_midi_processor_2::event_param2);
                        vel = note.velocity;
                        break;
                    }
                }
            }
            return true;
        };
    }

    // ========================================================================
    // Editor State
    // ========================================================================

private:
    std::wstring filename;
    std::unique_ptr<bbb_mmap> mmap_file;
    
    // Editor-friendly note list (piano roll view)
    std::vector<piano_note> notes;
    std::map<std::uint8_t, track_info> tracks;

    // Tempo map captured from the source file (tick, microseconds per quarter note).
    // Used for time computations only; the events themselves live in raw_track_events.
    std::vector<std::pair<tick_type, std::uint32_t>> tempo_events;

    /**
     * Non-note event captured verbatim from the source file so it survives a save
     * (CC, program change, pitch bend, aftertouch, meta, sysex).
     * bytes holds the full normalized event: status byte, data, and for meta/sysex
     * the type byte and VLV-encoded length.
     */
    struct raw_event
    {
        tick_type tick;
        std::vector<base_type> bytes;
    };
    std::map<std::uint8_t, std::vector<raw_event>> raw_track_events;

    // Track edits apply to (piano roll focus); notes of other tracks are shown dimmed
    std::uint8_t active_track = 0;

    // Selection state
    selection current_selection;

    // Undo/Redo stacks
    std::vector<std::unique_ptr<edit_operation>> undo_stack;
    std::vector<std::unique_ptr<edit_operation>> redo_stack;
    static constexpr size_t max_undo_depth = 100;

    // State flags
    std::atomic_bool is_dirty;
    std::atomic_bool is_loaded;
    // Recursive: public entry points lock, and some of them call other locking getters.
    // Guards notes/tracks/tempo/selection/view state against the GL thread drawing
    // while a worker thread loads or edits.
    mutable std::recursive_mutex editor_mutex;
    
    // PPQN from MIDI file
    std::uint16_t ppqn;
    tick_type ticks_per_beat;
    
    // View state (for piano roll rendering)
    tick_type view_start_tick;
    tick_type view_duration_ticks;
    std::uint8_t view_key_low;
    std::uint8_t view_key_high;
    float zoom_level;

public:
    midi_editor() 
        : is_dirty(false), is_loaded(false), ppqn(480), ticks_per_beat(480),
          view_start_tick(0), view_duration_ticks(480 * 4), 
          view_key_low(0), view_key_high(127), zoom_level(1.0f) {}

    // ========================================================================
    // File Loading (mmap-based like simple_player)
    // ========================================================================

    /**
     * Load MIDI file via memory-mapped I/O
     * Parses directly from mmap without loading into intermediate buffer
     */
    bool load_file(const std::wstring& filepath)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        is_loaded = false;
        is_dirty = false;
        notes.clear();
        tracks.clear();
        tempo_events.clear();
        raw_track_events.clear();
        undo_stack.clear();
        redo_stack.clear();
        current_selection = selection();
        active_track = 0;

        mmap_file = std::make_unique<bbb_mmap>(filepath.c_str());
        if (!mmap_file || !mmap_file->good())
            return false;

        filename = filepath;

        const auto begin = mmap_file->begin();
        const auto size = mmap_file->length();
        const auto end = begin + size;

        if (size < 18 || std::memcmp(begin, "MThd", 4) != 0)
            return false;

        // Read division; SMPTE division is unsupported, fall back to a sane default
        ppqn = (begin[12] << 8) | (begin[13]);
        if (ppqn == 0 || (ppqn & 0x8000))
            ppqn = 480;
        ticks_per_beat = ppqn;

        // Parse tracks and extract notes
        auto ptr = begin + 14;
        std::uint8_t track_index = 0;

        while (ptr < end && track_index < 0xFF)
        {
            if (!parse_track_mmap(ptr, end, track_index++))
                break;
        }

        std::sort(tempo_events.begin(), tempo_events.end());
        std::sort(notes.begin(), notes.end());

        is_loaded = true;

        reset_view_to_content();
        return true;
    }

    /**
     * Fit the viewport to the loaded notes: full length, pitch range with margins
     */
    void reset_view_to_content()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        view_start_tick = 0;
        view_duration_ticks = tick_type(ppqn) * 4 * 4;

        return;

        const auto total = get_total_ticks();
        view_duration_ticks = total ? total : tick_type(ppqn) * 4;

        if (notes.empty())
        {
            view_key_low = 24;
            view_key_high = 108;
            return;
        }

        std::uint8_t min_key = 127, max_key = 0;
        for (const auto& note : notes)
        {
            min_key = std::min(min_key, note.key);
            max_key = std::max(max_key, note.key);
        }
        view_key_low = min_key > 2 ? min_key - 2 : 0;
        view_key_high = max_key < 125 ? max_key + 2 : 127;

        // Keep a sensible minimum of visible lanes so short files don't look stretched
        if (view_key_high - view_key_low < 24)
        {
            int mid = (int(view_key_low) + int(view_key_high)) / 2;
            int low = std::max(0, mid - 12);
            view_key_low = std::uint8_t(low);
            view_key_high = std::uint8_t(std::min(127, low + 24));
        }
    }

    /**
     * Parse a single track from mmap (similar to simple_player::read_through_one_track)
     */
    bool parse_track_mmap(const uint8_t*& cur, const uint8_t* end, std::uint8_t track_index)
    {
        tick_type current_tick = 0;
        std::uint8_t rsb = 0;
        std::uint8_t primary_channel = 0xFF;
        std::string track_name;
        std::vector<raw_event> captured_events;
        
        // Track state for note parsing
        struct pending_note
        {
            tick_type start;
            std::uint8_t key;
            std::uint8_t velocity;
            std::uint8_t channel;
        };
        std::unordered_map<std::uint16_t, pending_note> active_notes; // key=(channel<<8)|key

        // Skip to MTrk header
        std::uint32_t header = 0;
        while (header != single_midi_processor_2::MTrk_header && (cur < end))
            header = (header << 8) | (*(cur++));

        if (cur >= end)
            return false;

        // Read track size
        std::uint32_t track_size = 0;
        for (int i = 0; i < 4; i++)
            track_size = (track_size << 8) | (*(cur++));

        const auto track_end = cur + track_size;
        
        while (cur < track_end)
        {
            // Read delta time (VLV)
            std::uint64_t delta = 0;
            std::uint8_t byte;
            do
            {
                byte = *(cur++);
                delta = (delta << 7) | (byte & 0x7F);
            } while (byte & 0x80 && cur < track_end);

            current_tick += delta;

            // Read event
            std::uint8_t command = *(cur++);
            std::uint8_t data1 = 0, data2 = 0;

            if (command < 0x80)
            {
                // Running status
                data1 = command;
                command = rsb;
            }
            else
            {
                if (command < 0xF0)
                    rsb = command;
                
                if (command < 0xF0 || command == 0xFF)
                    data1 = *(cur++);
                else
                    data1 = 0xFF;
            }

            if (command < 0x80)
                break;

            switch (command >> 4)
            {
                case 0x9: // Note On
                {
                    data2 = *(cur++);
                    if (data2 > 0)
                    {
                        std::uint16_t note_key = (std::uint16_t(command & 0x0F) << 8) | data1;
                        active_notes[note_key] = { current_tick, data1, data2, static_cast<std::uint8_t>(command & 0x0F) };
                        if (primary_channel == 0xFF)
                            primary_channel = command & 0x0F;
                    }
                    else
                    {
                        // Note On with velocity 0 = Note Off
                        std::uint16_t note_key = (std::uint16_t(command & 0x0F) << 8) | data1;
                        auto it = active_notes.find(note_key);
                        if (it != active_notes.end())
                        {
                            notes.emplace_back(it->second.start, current_tick,
                                           it->second.key, it->second.velocity,
                                           it->second.channel, track_index);
                            active_notes.erase(it);
                        }
                    }
                    break;
                }
                case 0x8: // Note Off
                {
                    data2 = *(cur++);
                    std::uint16_t note_key = (std::uint16_t(command & 0x0F) << 8) | data1;
                    auto it = active_notes.find(note_key);
                    if (it != active_notes.end())
                    {
                        notes.emplace_back(it->second.start, current_tick, 
                                         it->second.key, it->second.velocity,
                                         it->second.channel, track_index);
                        active_notes.erase(it);
                    }
                    break;
                }
                case 0xA: case 0xB: case 0xE:
                {
                    data2 = *(cur++);
                    captured_events.push_back({ current_tick, { command, data1, data2 } });
                    break;
                }
                case 0xC: case 0xD:
                {
                    captured_events.push_back({ current_tick, { command, data1 } });
                    break;
                }
                case 0xF:
                {
                    if (command == 0xFF && data1 == 0x2F)
                    {
                        // End of track
                        cur = track_end;
                        continue;
                    }

                    // Read meta/sysex length
                    std::uint32_t length = 0;
                    do
                    {
                        byte = *(cur++);
                        length = (length << 7) | (byte & 0x7F);
                    } while (byte & 0x80 && cur < track_end);

                    if (cur + length <= track_end)
                    {
                        // Tempo map for time computations
                        if (command == 0xFF && data1 == 0x51 && length == 3)
                            tempo_events.emplace_back(current_tick,
                                (std::uint32_t(cur[0]) << 16) | (std::uint32_t(cur[1]) << 8) | cur[2]);

                        // Track name meta
                        if (command == 0xFF && data1 == 0x03 && track_name.empty())
                            track_name.assign(reinterpret_cast<const char*>(cur), length);

                        // Keep the event verbatim so it survives a save
                        raw_event ev{ current_tick, {} };
                        ev.bytes.push_back(command);
                        if (command == 0xFF)
                            ev.bytes.push_back(data1);
                        single_midi_processor_2::push_vlv(length, ev.bytes);
                        ev.bytes.insert(ev.bytes.end(), cur, cur + length);
                        captured_events.push_back(std::move(ev));
                    }

                    cur += length;
                    break;
                }
            }
        }

        // Close any remaining active notes (malformed MIDI)
        for (auto& [key, note] : active_notes)
        {
            notes.emplace_back(note.start, current_tick + 1,
                             note.key, note.velocity, note.channel, track_index);
        }

        auto& info = tracks[track_index];
        info.name = std::move(track_name);
        info.channel = primary_channel;

        if (!captured_events.empty())
            raw_track_events[track_index] = std::move(captured_events);

        return true;
    }

    // ========================================================================
    // Piano Roll Query Interface
    // ========================================================================

    /**
     * Get all notes within a time and pitch range
     */
    std::vector<piano_note> get_notes_in_range(
        tick_type start_tick, tick_type end_tick,
        std::uint8_t key_low = 0, std::uint8_t key_high = 127) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        std::vector<piano_note> result;
        for (const auto& note : notes)
        {
            if (note.key >= key_low && note.key <= key_high &&
                note.start_tick < end_tick && note.end_tick > start_tick)
            {
                result.push_back(note);
            }
        }
        return result;
    }

    /**
     * Get notes at a specific time slice (for vertical selection)
     */
    std::vector<piano_note> get_notes_at_tick(tick_type tick) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        std::vector<piano_note> result;
        for (const auto& note : notes)
        {
            if (tick >= note.start_tick && tick < note.end_tick)
            {
                result.push_back(note);
            }
        }
        return result;
    }

    /**
     * Get notes on a specific key/pitch (for horizontal selection)
     */
    std::vector<piano_note> get_notes_on_key(std::uint8_t key) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        std::vector<piano_note> result;
        for (const auto& note : notes)
        {
            if (note.key == key)
            {
                result.push_back(note);
            }
        }
        return result;
    }

    /**
     * Iterator interface for piano roll rendering
     */
    struct note_iterator
    {
        using iterator_category = std::forward_iterator_tag;
        using value_type = piano_note;
        using difference_type = std::ptrdiff_t;
        using pointer = const piano_note*;
        using reference = const piano_note&;

        std::vector<piano_note>::const_iterator it;
        std::vector<piano_note>::const_iterator end;
        tick_type view_start;
        tick_type view_end;

        note_iterator(std::vector<piano_note>::const_iterator begin,
                     std::vector<piano_note>::const_iterator end,
                     tick_type vs, tick_type ve)
            : it(begin), end(end), view_start(vs), view_end(ve)
        {
            // Skip notes outside view
            while (it != end && (it->end_tick <= view_start || it->start_tick >= view_end))
                ++it;
        }

        reference operator*() const { return *it; }
        pointer operator->() const { return &(*it); }

        note_iterator& operator++()
        {
            ++it;
            while (it != end && (it->end_tick <= view_start || it->start_tick >= view_end))
                ++it;
            return *this;
        }

        note_iterator operator++(int)
        {
            note_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const note_iterator& other) const { return it == other.it; }
        bool operator!=(const note_iterator& other) const { return it != other.it; }
    };

    note_iterator begin_notes() const
    {
        return note_iterator(notes.begin(), notes.end(), view_start_tick, 
                            view_start_tick + view_duration_ticks);
    }

    note_iterator end_notes() const
    {
        return note_iterator(notes.end(), notes.end(), view_start_tick, 
                            view_start_tick + view_duration_ticks);
    }

    // ========================================================================
    // Edit Operations (with Undo/Redo)
    // ========================================================================

    void insert_note(tick_type start, tick_type end, std::uint8_t key,
                    std::uint8_t velocity, std::uint8_t channel = 0, std::uint8_t track = 0)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        auto op = std::make_unique<insert_note_op>(
            piano_note(start, end, key, velocity, channel, track));
        op->execute(*this);
        push_undo(std::move(op));
    }

    /**
     * Insert a note into the active track using its primary channel
     */
    void insert_note_active_track(tick_type start, tick_type end,
                                  std::uint8_t key, std::uint8_t velocity)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        std::uint8_t channel = 0;
        auto it = tracks.find(active_track);
        if (it != tracks.end() && it->second.channel != 0xFF)
            channel = it->second.channel;
        insert_note(start, end, key, velocity, channel, active_track);
    }

    /**
     * Set one note's velocity immediately without touching undo history.
     * Used by the velocity lane during a drag; the whole gesture is then
     * committed as one undo entry via commit_velocity_gesture().
     * Returns false if the note was not found; old_velocity receives the prior value.
     */
    bool set_note_velocity_transient(const piano_note& ident, std::uint8_t velocity,
                                     std::uint8_t& old_velocity)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        for (auto& n : notes)
        {
            if (n.start_tick == ident.start_tick &&
                n.end_tick == ident.end_tick &&
                n.key == ident.key &&
                n.channel == ident.channel &&
                n.track_index == ident.track_index)
            {
                old_velocity = n.velocity;
                n.velocity = std::clamp<std::uint8_t>(velocity, 1, 127);
                mark_dirty();
                return true;
            }
        }
        return false;
    }

    /**
     * Push an already-applied velocity gesture onto the undo stack
     */
    void commit_velocity_gesture(std::vector<recorded_velocity_op::entry>&& entries)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (entries.empty())
            return;
        push_undo(std::make_unique<recorded_velocity_op>(std::move(entries)));
        mark_dirty();
    }

    void adjust_velocity_selected(int delta)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!current_selection.is_active() || !delta)
            return;

        auto op = std::make_unique<velocity_adjust_op>(current_selection, delta);
        op->execute(*this);
        push_undo(std::move(op));
    }

    /**
     * Erase the topmost note at (tick, key). track_filter = selection::all_tracks
     * erases from any track, otherwise only from the given one.
     */
    bool erase_note_at(tick_type tick, std::uint8_t key,
                       std::uint8_t track_filter = selection::all_tracks)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        const piano_note* found = nullptr;
        for (const auto& note : notes)
        {
            if (note.key == key && tick >= note.start_tick && tick < note.end_tick &&
                (track_filter == selection::all_tracks || note.track_index == track_filter))
                found = &note; // last one wins: it is drawn on top
        }
        if (!found)
            return false;

        auto op = std::make_unique<delete_single_note_op>(*found);
        op->execute(*this);
        push_undo(std::move(op));
        return true;
    }

    /**
     * Find the topmost note at (tick, key) across all tracks.
     * With tolerances > 0, falls back to the nearest note within
     * ±key_tolerance semitones and ±tick_tolerance ticks of the point.
     */
    bool find_note_at(tick_type tick, std::uint8_t key, piano_note& out,
                      tick_type tick_tolerance = 0, std::uint8_t key_tolerance = 0) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        bool found = false;
        for (const auto& note : notes)
        {
            if (note.key == key && tick >= note.start_tick && tick < note.end_tick)
            {
                out = note;
                found = true;
            }
        }
        if (found || (!tick_tolerance && !key_tolerance))
            return found;

        const auto lo_tick = tick > tick_tolerance ? tick - tick_tolerance : 0;
        const auto hi_tick = tick + tick_tolerance;
        for (const auto& note : notes)
        {
            if (std::abs(int(note.key) - int(key)) <= int(key_tolerance) &&
                note.start_tick <= hi_tick && note.end_tick > lo_tick)
            {
                out = note;
                found = true;
            }
        }
        return found;
    }

    /**
     * Next/previous track that actually contains notes, in cyclic order.
     * Returns the current active track if no track has notes.
     */
    std::uint8_t next_track_with_notes(int direction) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        std::set<std::uint8_t> populated;
        for (const auto& note : notes)
            populated.insert(note.track_index);
        if (populated.empty())
            return active_track;

        if (direction >= 0)
        {
            auto it = populated.upper_bound(active_track);
            if (it == populated.end())
                it = populated.begin();
            return *it;
        }

        auto it = populated.lower_bound(active_track);
        if (it == populated.begin())
            it = populated.end();
        return *(--it);
    }

    void delete_selected_notes()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!current_selection.is_active())
            return;

        auto op = std::make_unique<delete_notes_op>(current_selection);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void move_selected_notes(sgtick_type delta_ticks = 0, int delta_keys = 0)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!current_selection.is_active())
            return;

        auto op = std::make_unique<move_notes_op>(current_selection, delta_ticks, delta_keys);
        op->execute(*this);
        push_undo(std::move(op));

        // Keep the selection rectangle attached to the moved notes
        auto& sel = current_selection;
        auto new_begin = sgtick_type(sel.begin_tick) + delta_ticks;
        auto new_end = sgtick_type(sel.end_tick) + delta_ticks;
        if (new_begin >= 0 && new_end >= 0)
        {
            sel.begin_tick = tick_type(new_begin);
            sel.end_tick = tick_type(new_end);
        }
        int kb = std::clamp(int(sel.key_begin) + delta_keys, 0, 127);
        int ke = std::clamp(int(sel.key_end) + delta_keys, 0, 127);
        sel.key_begin = std::uint8_t(kb);
        sel.key_end = std::uint8_t(ke);
    }

    void resize_note(tick_type start_tick, std::uint8_t key, tick_type new_length)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        piano_note target;
        target.start_tick = start_tick;
        target.key = key;

        auto op = std::make_unique<resize_note_op>(target, new_length);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void change_velocity_selected(std::uint8_t velocity)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!current_selection.is_active())
            return;

        auto op = std::make_unique<velocity_change_op>(current_selection, velocity);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void quantize_selected(tick_type grid_resolution)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!current_selection.is_active() || !grid_resolution)
            return;

        auto op = std::make_unique<quantize_op>(current_selection, grid_resolution);
        op->execute(*this);
        push_undo(std::move(op));
    }

    // ========================================================================
    // Undo/Redo System
    // ========================================================================

    void undo()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (undo_stack.empty())
            return;

        auto op = std::move(undo_stack.back());
        undo_stack.pop_back();
        op->undo(*this);
        redo_stack.push_back(std::move(op));
    }

    void redo()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (redo_stack.empty())
            return;

        auto op = std::move(redo_stack.back());
        redo_stack.pop_back();
        op->redo(*this);
        undo_stack.push_back(std::move(op));
    }

    bool can_undo() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return !undo_stack.empty();
    }
    bool can_redo() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return !redo_stack.empty();
    }

    void clear_undo_history()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        undo_stack.clear();
        redo_stack.clear();
    }

private:
    void push_undo(std::unique_ptr<edit_operation> op)
    {
        if (undo_stack.size() >= max_undo_depth)
            undo_stack.erase(undo_stack.begin());
        undo_stack.push_back(std::move(op));
        redo_stack.clear();
    }

    void remove_note_by_position(const piano_note& note)
    {
        notes.erase(
            std::remove_if(notes.begin(), notes.end(),
                [&note](const piano_note& n) {
                    return n.start_tick == note.start_tick &&
                           n.key == note.key &&
                           n.channel == note.channel &&
                           n.track_index == note.track_index;
                }),
            notes.end());
    }

    void mark_dirty() { is_dirty = true; }

public:
    // ========================================================================
    // Selection Management
    // ========================================================================

    void set_selection(tick_type begin, tick_type end,
                      std::uint8_t key_begin = 0, std::uint8_t key_end = 127,
                      std::uint8_t track_filter = selection::all_tracks)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        current_selection.begin_tick = begin;
        current_selection.end_tick = end;
        current_selection.key_begin = key_begin;
        current_selection.key_end = key_end;
        current_selection.track_filter = track_filter;
        current_selection.has_selection = (begin < end);
    }

    // ========================================================================
    // Active Track
    // ========================================================================

    void set_active_track(std::uint8_t track)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        active_track = track;
        current_selection.has_selection = false; // selection belongs to a track
    }

    std::uint8_t get_active_track() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return active_track;
    }

    std::string get_track_label(std::uint8_t track) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        std::string label = "Track " + std::to_string(track);
        auto it = tracks.find(track);
        if (it != tracks.end() && !it->second.name.empty())
            label += " (" + it->second.name + ")";
        return label;
    }

    std::uint8_t get_track_count() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return std::uint8_t(tracks.size());
    }

    std::uint8_t get_track_primary_channel(std::uint8_t track) const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        auto it = tracks.find(track);
        if (it != tracks.end() && it->second.channel != 0xFF)
            return it->second.channel;
        return 0;
    }

    std::uint8_t get_active_track_channel() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return get_track_primary_channel(active_track);
    }

    void clear_selection()
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        current_selection.has_selection = false;
    }

    selection get_selection() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        return current_selection;
    }

    // ========================================================================
    // View/Viewport Management
    // ========================================================================

    void set_view_range(tick_type start, tick_type duration)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        view_start_tick = start;
        view_duration_ticks = std::max<tick_type>(duration, min_view_duration);
    }

    void set_view_keys(std::uint8_t low, std::uint8_t high)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        view_key_low = std::min(low, high);
        view_key_high = std::max(low, high);
    }

    void zoom_in(float factor = 1.5f)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        zoom_level *= factor;
        set_zoomed_duration(tick_type(view_duration_ticks / factor));
    }

    void zoom_out(float factor = 1.5f)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        zoom_level /= factor;
        set_zoomed_duration(tick_type(view_duration_ticks * factor));
    }

    void scroll_left(tick_type amount)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (view_start_tick >= amount)
            view_start_tick -= amount;
        else
            view_start_tick = 0;
    }

    void scroll_right(tick_type amount)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        view_start_tick += amount;
    }

private:
    static constexpr tick_type min_view_duration = 16;

    // Change zoom while keeping the view centre in place
    void set_zoomed_duration(tick_type new_duration)
    {
        const auto max_duration = std::max({ get_total_ticks(), tick_type(ppqn) * 4, min_view_duration }) * 2;
        new_duration = std::clamp(new_duration, min_view_duration, max_duration);

        const auto center = view_start_tick + view_duration_ticks / 2;
        view_duration_ticks = new_duration;
        view_start_tick = center > new_duration / 2 ? center - new_duration / 2 : 0;
    }

public:

    // Getters for viewer
    tick_type get_view_start_tick() const { return view_start_tick; }
    tick_type get_view_duration_ticks() const { return view_duration_ticks; }
    std::uint8_t get_view_key_low() const { return view_key_low; }
    std::uint8_t get_view_key_high() const { return view_key_high; }

    // ========================================================================
    // File Saving
    // ========================================================================

    /**
     * Save edited MIDI back to file
     * Rebuilds the file from the editor's note list and the captured tempo map.
     * Non-note events other than tempo are not preserved.
     */
    bool save_file(const std::wstring& filepath)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        if (!is_loaded)
            return false;

        if (!write_midi_from_notes(filepath))
            return false;

        is_dirty = false;
        return true;
    }

    /**
     * Write the current in-memory state to a file without treating it as a save
     * (dirty flag untouched). Used for playback of unsaved edits.
     */
    bool export_current(const std::wstring& filepath)
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        if (!is_loaded)
            return false;
        return write_midi_from_notes(filepath);
    }

private:
    static void write_be16(std::ostream& out, std::uint16_t v)
    {
        const char bytes[2] = { char(v >> 8), char(v) };
        out.write(bytes, 2);
    }

    static void write_be32(std::ostream& out, std::uint32_t v)
    {
        const char bytes[4] = { char(v >> 24), char(v >> 16), char(v >> 8), char(v) };
        out.write(bytes, 4);
    }

    static void finish_and_write_track(std::ostream& out, std::vector<base_type>& track_data)
    {
        // End of track meta event
        single_midi_processor_2::push_vlv(0, track_data);
        track_data.push_back(0xFF);
        track_data.push_back(0x2F);
        track_data.push_back(0x00);

        out.write("MTrk", 4);
        write_be32(out, std::uint32_t(track_data.size()));
        out.write(reinterpret_cast<const char*>(track_data.data()), track_data.size());
    }

    bool write_midi_from_notes(const std::wstring& filepath)
    {
        std::ofstream out(filepath, std::ios::binary);
        if (!out)
            return false;

        // Group notes by track
        std::map<std::uint8_t, std::vector<piano_note>> notes_by_track;
        for (const auto& note : notes)
            notes_by_track[note.track_index].push_back(note);

        // A track is written if it has notes or captured non-note events
        std::vector<std::uint8_t> track_ids;
        for (const auto& [id, _] : notes_by_track)
            track_ids.push_back(id);
        for (const auto& [id, _] : raw_track_events)
            if (!notes_by_track.count(id))
                track_ids.push_back(id);
        std::sort(track_ids.begin(), track_ids.end());

        out.write("MThd", 4);
        write_be32(out, 6);
        write_be16(out, 1); // format 1
        write_be16(out, std::uint16_t(std::max<size_t>(track_ids.size(), 1)));
        write_be16(out, ppqn);

        if (track_ids.empty())
        {
            // Keep the file valid: one empty track
            std::vector<base_type> track_data;
            finish_and_write_track(out, track_data);
            return out.good();
        }

        static const std::vector<piano_note> no_notes;
        static const std::vector<raw_event> no_raw;
        for (auto id : track_ids)
        {
            auto notes_it = notes_by_track.find(id);
            auto raw_it = raw_track_events.find(id);
            write_midi_track(out,
                notes_it != notes_by_track.end() ? notes_it->second : no_notes,
                raw_it != raw_track_events.end() ? raw_it->second : no_raw);
        }

        return out.good();
    }

    void write_midi_track(std::ostream& out,
                          const std::vector<piano_note>& track_notes,
                          const std::vector<raw_event>& raw_events)
    {
        // Merged output event: notes are reconstructed, everything else verbatim.
        // Order at equal ticks: note-offs, then raw events, then note-ons.
        struct out_event
        {
            tick_type tick;
            std::uint8_t order;
            std::vector<base_type> bytes;
        };

        std::vector<out_event> events;
        events.reserve(track_notes.size() * 2 + raw_events.size());

        for (const auto& note : track_notes)
        {
            // Velocity 0 on a note-on would read as a note-off, clamp to 1
            events.push_back({ note.start_tick, 2,
                { base_type(0x90 | note.channel), note.key, std::max<base_type>(note.velocity, 1) } });
            events.push_back({ note.end_tick, 0,
                { base_type(0x80 | note.channel), note.key, 0x40 } });
        }
        for (const auto& raw : raw_events)
            events.push_back({ raw.tick, 1, raw.bytes });

        std::stable_sort(events.begin(), events.end(),
            [](const out_event& a, const out_event& b)
            {
                if (a.tick != b.tick) return a.tick < b.tick;
                return a.order < b.order;
            });

        std::vector<base_type> track_data;
        tick_type current_tick = 0;

        for (const auto& event : events)
        {
            single_midi_processor_2::push_vlv_s(event.tick - current_tick, track_data);
            track_data.insert(track_data.end(), event.bytes.begin(), event.bytes.end());
            current_tick = event.tick;
        }

        finish_and_write_track(out, track_data);
    }

public:

    // ========================================================================
    // Getters
    // ========================================================================

    bool is_file_loaded() const { return is_loaded; }
    bool is_modified() const { return is_dirty; }
    
    const std::vector<piano_note>& get_all_notes() const { return notes; }
    size_t get_note_count() const { return notes.size(); }
    
    std::uint16_t get_ppqn() const { return ppqn; }
    tick_type get_ticks_per_beat() const { return ticks_per_beat; }
    
    tick_type get_total_ticks() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);
        tick_type max_tick = 0;
        for (const auto& note : notes)
            max_tick = std::max(max_tick, note.end_tick);
        return max_tick;
    }

    double get_total_seconds() const
    {
        std::lock_guard<std::recursive_mutex> lock(editor_mutex);

        constexpr double default_tempo_us = 500000.0; // 120 BPM
        const auto total = get_total_ticks();
        const double us_per_tick_divisor = double(ppqn) * 1000000.0;

        double seconds = 0;
        tick_type last_tick = 0;
        double current_tempo_us = default_tempo_us;

        // tempo_events is sorted on load
        for (const auto& [tick, tempo] : tempo_events)
        {
            if (tick >= total)
                break;
            seconds += double(tick - last_tick) * current_tempo_us / us_per_tick_divisor;
            last_tick = tick;
            current_tempo_us = double(tempo);
        }
        seconds += double(total - last_tick) * current_tempo_us / us_per_tick_divisor;
        return seconds;
    }

    const std::map<std::uint8_t, track_info>& get_tracks() const { return tracks; }
    const std::wstring& get_filename() const { return filename; }
};

#endif // SAFC_MIDI_EDITOR
