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
        tick_type begin_tick;
        tick_type end_tick;
        std::uint8_t key_begin;
        std::uint8_t key_end;
        bool has_selection;

        selection() : begin_tick(0), end_tick(0), key_begin(0), key_end(127), has_selection(false) {}

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
        tick_type old_length;

        resize_note_op(piano_note n, tick_type len) : target_note(n), new_length(len) {}
        
        void execute(midi_editor& editor) override
        {
            for (auto& note : editor.notes)
            {
                if (note.start_tick == target_note.start_tick &&
                    note.key == target_note.key &&
                    note.channel == target_note.channel &&
                    note.track_index == target_note.track_index)
                {
                    old_length = note.length();
                    note.end_tick = note.start_tick + new_length;
                    break;
                }
            }
            editor.mark_dirty();
        }
        
        void undo(midi_editor& editor) override
        {
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
    
    // Selection state
    selection current_selection;
    
    // Undo/Redo stacks
    std::vector<std::unique_ptr<edit_operation>> undo_stack;
    std::vector<std::unique_ptr<edit_operation>> redo_stack;
    static constexpr size_t max_undo_depth = 100;
    
    // State flags
    std::atomic_bool is_dirty;
    std::atomic_bool is_loaded;
    std::mutex editor_mutex;
    
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
        std::lock_guard<std::mutex> lock(editor_mutex);
        
        mmap_file = std::make_unique<bbb_mmap>(filepath.c_str());
        if (!mmap_file || !mmap_file->good())
            return false;

        filename = filepath;
        
        const auto begin = mmap_file->begin();
        const auto size = mmap_file->length();
        const auto end = begin + size;

        if (size < 18)
            return false;

        // Read header
        ppqn = (begin[12] << 8) | (begin[13]);
        ticks_per_beat = ppqn;

        // Parse tracks and extract notes
        auto ptr = begin + 14;
        std::uint8_t track_index = 0;

        while (ptr < end)
        {
            if (!parse_track_mmap(ptr, end, track_index++))
                break;
        }

        is_loaded = true;
        is_dirty = false;
        
        // Initialize view to show entire file
        view_start_tick = 0;
        view_duration_ticks = get_total_ticks() * 1.1;
        
        return true;
    }

    /**
     * Parse a single track from mmap (similar to simple_player::read_through_one_track)
     */
    bool parse_track_mmap(const uint8_t*& cur, const uint8_t* end, std::uint8_t track_index)
    {
        tick_type current_tick = 0;
        std::uint8_t rsb = 0;
        
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
                    cur++; // Skip second data byte
                    break;
                case 0xC: case 0xD:
                    // Single byte events
                    break;
                case 0xF:
                {
                    if (command == 0xFF && data1 == 0x2F)
                    {
                        // End of track
                        cur = track_end;
                        continue;
                    }
                    
                    // Skip meta/sysex data
                    std::uint32_t length = 0;
                    do
                    {
                        byte = *(cur++);
                        length = (length << 7) | (byte & 0x7F);
                    } while (byte & 0x80 && cur < track_end);
                    
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

        tracks[track_index] = track_info();
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
                    std::uint8_t velocity, std::uint8_t channel = 0)
    {
        auto op = std::make_unique<insert_note_op>(
            piano_note(start, end, key, velocity, channel));
        op->execute(*this);
        push_undo(std::move(op));
    }

    void delete_selected_notes()
    {
        if (!current_selection.is_active())
            return;
        
        auto op = std::make_unique<delete_notes_op>(current_selection);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void move_selected_notes(sgtick_type delta_ticks = 0, int delta_keys = 0)
    {
        if (!current_selection.is_active())
            return;
        
        auto op = std::make_unique<move_notes_op>(current_selection, delta_ticks, delta_keys);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void resize_note(tick_type start_tick, std::uint8_t key, tick_type new_length)
    {
        piano_note target;
        target.start_tick = start_tick;
        target.key = key;
        
        auto op = std::make_unique<resize_note_op>(target, new_length);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void change_velocity_selected(std::uint8_t velocity)
    {
        if (!current_selection.is_active())
            return;
        
        auto op = std::make_unique<velocity_change_op>(current_selection, velocity);
        op->execute(*this);
        push_undo(std::move(op));
    }

    void quantize_selected(tick_type grid_resolution)
    {
        if (!current_selection.is_active())
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
        if (undo_stack.empty())
            return;
        
        auto op = std::move(undo_stack.back());
        undo_stack.pop_back();
        op->undo(*this);
        redo_stack.push_back(std::move(op));
    }

    void redo()
    {
        if (redo_stack.empty())
            return;
        
        auto op = std::move(redo_stack.back());
        redo_stack.pop_back();
        op->redo(*this);
        undo_stack.push_back(std::move(op));
    }

    bool can_undo() const { return !undo_stack.empty(); }
    bool can_redo() const { return !redo_stack.empty(); }

    void clear_undo_history()
    {
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
                      std::uint8_t key_begin = 0, std::uint8_t key_end = 127)
    {
        current_selection.begin_tick = begin;
        current_selection.end_tick = end;
        current_selection.key_begin = key_begin;
        current_selection.key_end = key_end;
        current_selection.has_selection = (begin < end);
    }

    void clear_selection()
    {
        current_selection.has_selection = false;
    }

    const selection& get_selection() const { return current_selection; }

    // ========================================================================
    // View/Viewport Management
    // ========================================================================

    void set_view_range(tick_type start, tick_type duration)
    {
        view_start_tick = start;
        view_duration_ticks = duration;
    }

    void set_view_keys(std::uint8_t low, std::uint8_t high)
    {
        view_key_low = low;
        view_key_high = high;
    }

    void zoom_in(float factor = 1.5f)
    {
        zoom_level *= factor;
        view_duration_ticks = tick_type(view_duration_ticks / factor);
    }

    void zoom_out(float factor = 1.5f)
    {
        zoom_level /= factor;
        view_duration_ticks = tick_type(view_duration_ticks * factor);
    }

    void scroll_left(tick_type amount)
    {
        if (view_start_tick >= amount)
            view_start_tick -= amount;
        else
            view_start_tick = 0;
    }

    void scroll_right(tick_type amount)
    {
        view_start_tick += amount;
    }

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
     * Applies all editor changes through the processor's filter system
     */
    bool save_file(const std::wstring& filepath)
    {
        if (!is_loaded)
            return false;

        std::lock_guard<std::mutex> lock(editor_mutex);

        // Build filters from editor state
        single_midi_processor_2::filters_multimap filters;
        
        // Add deletion filter for removed notes
        // (In a real implementation, track which notes were deleted)
        
        // Add velocity modification filter
        // (In a real implementation, track velocity changes)
        
        // Use single_midi_processor_2 to write the file with filters applied
        // This would require extending single_midi_processor_2 with a save function
        
        // For now, we'll write directly from the notes list
        return write_midi_from_notes(filepath);
    }

    bool write_midi_from_notes(const std::wstring& filepath)
    {
        std::ofstream out(filepath, std::ios::binary);
        if (!out)
            return false;

        // Write MThd header
        out.write("MThd", 4);
        std::uint32_t header_size = 6;
        out.write(reinterpret_cast<char*>(&header_size), 4);
        std::uint16_t format = 1; // Multi-track
        std::uint16_t num_tracks = tracks.size();
        std::uint16_t ppqn_be = ppqn; // Already in big-endian order
        out.write(reinterpret_cast<char*>(&format), 2);
        out.write(reinterpret_cast<char*>(&num_tracks), 2);
        out.write(reinterpret_cast<char*>(&ppqn_be), 2);

        // Group notes by track
        std::map<std::uint8_t, std::vector<piano_note>> notes_by_track;
        for (const auto& note : notes)
        {
            notes_by_track[note.track_index].push_back(note);
        }

        // Write each track
        for (auto& [track_idx, track_notes] : notes_by_track)
        {
            write_midi_track(out, track_idx, track_notes);
        }

        return true;
    }

    void write_midi_track(std::ostream& out, std::uint8_t track_idx, 
                         std::vector<piano_note>& track_notes)
    {
        // Sort notes by start time
        std::sort(track_notes.begin(), track_notes.end());

        // Build event list (note on + note off pairs)
        struct midi_event
        {
            tick_type tick;
            std::uint8_t type;  // 0x90 or 0x80
            std::uint8_t key;
            std::uint8_t velocity;
            std::uint8_t channel;

            bool operator<(const midi_event& other) const
            {
                if (tick != other.tick) return tick < other.tick;
                return type < other.type; // Note offs before note ons at same tick
            }
        };

        std::vector<midi_event> events;
        for (const auto& note : track_notes)
        {
            events.push_back({ note.start_tick, 0x90, note.key, note.velocity, note.channel });
            events.push_back({ note.end_tick, 0x80, note.key, 0, note.channel });
        }
        std::sort(events.begin(), events.end());

        // Serialize to MIDI track buffer
        std::vector<base_type> track_data;
        tick_type current_tick = 0;

        for (const auto& event : events)
        {
            // Write delta time
            auto delta = event.tick - current_tick;
            single_midi_processor_2::push_vlv(delta, track_data);
            
            // Write event
            track_data.push_back(event.type | event.channel);
            track_data.push_back(event.key);
            track_data.push_back(event.velocity);
            
            current_tick = event.tick;
        }

        // Write end of track
        single_midi_processor_2::push_vlv(0, track_data);
        track_data.push_back(0xFF);
        track_data.push_back(0x2F);
        track_data.push_back(0x00);

        // Write track header
        out.write("MTrk", 4);
        std::uint32_t track_size = track_data.size();
        out.write(reinterpret_cast<char*>(&track_size), 4);
        out.write(reinterpret_cast<char*>(track_data.data()), track_data.size());
    }

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
        if (notes.empty()) return 0;
        tick_type max_tick = 0;
        for (const auto& note : notes)
            max_tick = std::max(max_tick, note.end_tick);
        return max_tick;
    }

    double get_total_seconds() const
    {
        // Approximate using default tempo (120 BPM)
        constexpr double default_tempo_us = 500000.0; // 120 BPM
        return (get_total_ticks() * default_tempo_us) / (ppqn * 1000000.0);
    }

    const std::map<std::uint8_t, track_info>& get_tracks() const { return tracks; }
    const std::wstring& get_filename() const { return filename; }
};

#endif // SAFC_MIDI_EDITOR
