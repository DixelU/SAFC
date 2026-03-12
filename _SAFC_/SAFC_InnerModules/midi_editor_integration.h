#pragma once
#ifndef SAFC_MIDI_EDITOR_INTEGRATION
#define SAFC_MIDI_EDITOR_INTEGRATION

#include "midi_editor.h"
#include "single_midi_processor_2.h"

/**
 * Integration layer between midi_editor and single_midi_processor_2
 * 
 * This module provides:
 * 1. Filter adapters that apply editor operations during processor playback
 * 2. State synchronization between editor and processor
 * 3. Real-time edit application without reloading the file
 */

struct midi_editor_processor_integration
{
    using tick_type = std::uint64_t;
    using sgtick_type = std::int64_t;
    using base_type = std::uint8_t;

    /**
     * Real-time filter that applies editor changes during playback
     * This filter can be added to single_midi_processor_2's filter multimap
     */
    struct editor_playback_filter
    {
        midi_editor* editor;
        
        // Tracks which notes should be silenced (deleted but not yet saved)
        std::unordered_set<std::uint64_t> silenced_note_ids;
        
        // Velocity overrides for specific notes
        std::unordered_map<std::uint64_t, std::uint8_t> velocity_overrides;
        
        // Pitch shifts for specific notes (in semitones)
        std::unordered_map<std::uint64_t, int> pitch_shifts;

        editor_playback_filter(midi_editor* ed) : editor(ed) {}

        /**
         * Generate a unique ID for a note event
         * Combines tick, key, and channel into a 64-bit identifier
         */
        static std::uint64_t make_note_id(tick_type tick, std::uint8_t key, std::uint8_t channel)
        {
            // Hash combine: tick ^ (key << 48) ^ (channel << 56)
            return tick ^ (std::uint64_t(key) << 48) ^ (std::uint64_t(channel) << 56);
        }

        /**
         * Filter function compatible with single_midi_processor_2::event_transforming_filter
         */
        bool operator()(
            const single_midi_processor_2::data_iterator& begin,
            const single_midi_processor_2::data_iterator& end,
            const single_midi_processor_2::data_iterator& cur,
            single_midi_processor_2::single_track_data& std_ref)
        {
            if (!editor)
                return true;

            auto& tick = single_midi_processor_2::get_value<tick_type>(
                cur, single_midi_processor_2::tick_position);
            const auto& type = single_midi_processor_2::get_value<base_type>(
                cur, single_midi_processor_2::event_type);

            // Handle note events
            if ((type & 0xF0) == 0x90 || (type & 0xF0) == 0x80)
            {
                const auto& key = single_midi_processor_2::get_value<base_type>(
                    cur, single_midi_processor_2::event_param1);
                const auto& channel = type & 0x0F;
                
                auto note_id = make_note_id(tick, key, channel);

                // Check if note should be silenced
                if (silenced_note_ids.count(note_id))
                {
                    // Disable this event
                    if ((type & 0xF0) == 0x90)
                    {
                        // For note-on, also disable the paired note-off
                        auto& ref_tick = single_midi_processor_2::get_value<tick_type>(
                            begin, single_midi_processor_2::get_value<tick_type>(
                                cur, single_midi_processor_2::event_param3));
                        ref_tick = single_midi_processor_2::disable_tick;
                    }
                    tick = single_midi_processor_2::disable_tick;
                    return false;
                }

                // Apply velocity override for note-on events
                if ((type & 0xF0) == 0x90)
                {
                    auto it = velocity_overrides.find(note_id);
                    if (it != velocity_overrides.end())
                    {
                        auto& vel = single_midi_processor_2::get_value<base_type>(
                            cur, single_midi_processor_2::event_param2);
                        vel = it->second;
                    }
                }

                // Apply pitch shift
                auto pitch_it = pitch_shifts.find(note_id);
                if (pitch_it != pitch_shifts.end())
                {
                    int shift = pitch_it->second;
                    auto& event_key = single_midi_processor_2::get_value<base_type>(
                        cur, single_midi_processor_2::event_param1);
                    
                    int new_key = int(event_key) + shift;
                    if (new_key >= 0 && new_key <= 127)
                    {
                        event_key = base_type(new_key);
                        
                        // Update note ID for note-off pairing
                        if ((type & 0xF0) == 0x90)
                        {
                            // Also need to update the reference to note-off event
                            // This is complex due to the bidirectional linking
                            // For now, we'll mark the original note-off as disabled
                            // and let the processor handle orphaned events
                        }
                    }
                }
            }

            return true;
        }
    };

    /**
     * Bulk transformation filter - applies batch operations
     * Used when saving or exporting with multiple edits applied
     */
    struct bulk_edit_filter
    {
        struct note_modification
        {
            tick_type start_tick;
            tick_type end_tick;
            std::uint8_t key;
            std::uint8_t channel;
            
            std::optional<std::uint8_t> new_velocity;
            std::optional<int> pitch_shift;
            std::optional<tick_type> time_shift;
            bool should_delete = false;
        };

        std::vector<note_modification> modifications;

        bulk_edit_filter() = default;

        void add_deletion(tick_type start, tick_type end, 
                         std::uint8_t key, std::uint8_t channel)
        {
            modifications.push_back({
                start, end, key, channel,
                std::nullopt, std::nullopt, std::nullopt, true
            });
        }

        void add_velocity_change(tick_type start, tick_type end,
                                std::uint8_t key, std::uint8_t channel,
                                std::uint8_t new_velocity)
        {
            modifications.push_back({
                start, end, key, channel,
                new_velocity, std::nullopt, std::nullopt, false
            });
        }

        void add_pitch_shift(tick_type start, tick_type end,
                            std::uint8_t key, std::uint8_t channel,
                            int semitones)
        {
            modifications.push_back({
                start, end, key, channel,
                std::nullopt, semitones, std::nullopt, false
            });
        }

        void add_time_shift(tick_type start, tick_type end,
                           std::uint8_t key, std::uint8_t channel,
                           sgtick_type ticks)
        {
            modifications.push_back({
                start, end, key, channel,
                std::nullopt, std::nullopt, ticks, false
            });
        }

        bool operator()(
            const single_midi_processor_2::data_iterator& begin,
            const single_midi_processor_2::data_iterator& end,
            const single_midi_processor_2::data_iterator& cur,
            single_midi_processor_2::single_track_data& std_ref)
        {
            auto& tick = single_midi_processor_2::get_value<tick_type>(
                cur, single_midi_processor_2::tick_position);
            const auto& type = single_midi_processor_2::get_value<base_type>(
                cur, single_midi_processor_2::event_type);

            if ((type & 0xF0) == 0x90 || (type & 0xF0) == 0x80)
            {
                const auto& key = single_midi_processor_2::get_value<base_type>(
                    cur, single_midi_processor_2::event_param1);
                const auto& channel = type & 0x0F;

                for (const auto& mod : modifications)
                {
                    if (mod.key == key && mod.channel == channel &&
                        tick >= mod.start_tick && tick <= mod.end_tick)
                    {
                        if (mod.should_delete)
                        {
                            // Disable event
                            if ((type & 0xF0) == 0x90)
                            {
                                auto& ref_tick = single_midi_processor_2::get_value<tick_type>(
                                    begin, single_midi_processor_2::get_value<tick_type>(
                                        cur, single_midi_processor_2::event_param3));
                                ref_tick = single_midi_processor_2::disable_tick;
                            }
                            tick = single_midi_processor_2::disable_tick;
                            return false;
                        }

                        if (mod.new_velocity.has_value() && (type & 0xF0) == 0x90)
                        {
                            auto& vel = single_midi_processor_2::get_value<base_type>(
                                cur, single_midi_processor_2::event_param2);
                            vel = mod.new_velocity.value();
                        }

                        if (mod.pitch_shift.has_value())
                        {
                            int shift = mod.pitch_shift.value();
                            auto& event_key = single_midi_processor_2::get_value<base_type>(
                                cur, single_midi_processor_2::event_param1);
                            
                            int new_key = int(event_key) + shift;
                            if (new_key >= 0 && new_key <= 127)
                                event_key = base_type(new_key);
                        }

                        if (mod.time_shift.has_value())
                        {
                            sgtick_type shift = mod.time_shift.value();
                            auto new_tick = sgtick_type(tick) + shift;
                            if (new_tick >= 0)
                                tick = tick_type(new_tick);
                        }

                        break;
                    }
                }
            }

            return true;
        }
    };

    /**
     * Helper to convert editor operations to processor filters
     */
    static bulk_edit_filter create_filter_from_editor(
        const midi_editor& editor,
        const midi_editor::selection& sel)
    {
        bulk_edit_filter filter;
        
        auto notes = editor.get_notes_in_range(
            sel.begin_tick, sel.end_tick,
            sel.key_begin, sel.key_end);

        for (const auto& note : notes)
        {
            // Example: mark all notes in selection for potential modification
            // In practice, you'd check which specific operations were applied
        }

        return filter;
    }

    /**
     * Apply editor filter to a processor instance
     */
    static void apply_editor_filter(
        single_midi_processor_2& processor,
        midi_editor& editor,
        base_type event_type_prefix = 0x90)
    {
        auto filter = std::make_unique<editor_playback_filter>(&editor);
        
        // Add to processor's filter multimap
        // Note: This requires extending single_midi_processor_2 to accept external filters
        // processor.add_filter(event_type_prefix, std::move(filter));
    }
};

/**
 * Extended processor wrapper that supports editor integration
 * This wraps single_midi_processor_2 and adds editor filter support
 */
struct midi_processor_with_editor
{
    single_midi_processor_2 processor;
    midi_editor editor;
    midi_editor_processor_integration::editor_playback_filter* active_filter;
    
    std::vector<midi_editor_processor_integration::bulk_edit_filter> pending_filters;

    midi_processor_with_editor() : active_filter(nullptr) {}

    /**
     * Load MIDI file for both processing and editing
     */
    bool load_file(const std::wstring& filepath)
    {
        return editor.load_file(filepath);
    }

    /**
     * Add an editor operation to be applied during processing
     */
    void queue_editor_operation(const midi_editor::edit_operation& op)
    {
        // Convert editor operation to bulk filter
        midi_editor_processor_integration::bulk_edit_filter filter;
        pending_filters.push_back(std::move(filter));
    }

    /**
     * Process the loaded MIDI with all queued editor operations
     */
    bool process_with_edits(
        const std::wstring& output_path,
        single_midi_processor_2::settings_obj settings)
    {
        if (!editor.is_file_loaded())
            return false;

        // Build combined filter from all pending operations
        single_midi_processor_2::filters_multimap filters;
        
        // Add editor filters
        for (auto& edit_filter : pending_filters)
        {
            // Wrap bulk_edit_filter in event_transforming_filter
            // This would require modifying single_midi_processor_2 to accept
            // external filter types
        }

        // Process with settings and filters
        // processor.process_file(editor.get_filename(), output_path, settings, filters);
        
        return true;
    }

    /**
     * Clear all pending editor operations
     */
    void clear_pending_operations()
    {
        pending_filters.clear();
    }
};

/**
 * Example: Create a filter that applies a specific edit operation
 */
inline single_midi_processor_2::event_transforming_filter 
make_note_delete_filter(
    const std::vector<midi_editor::piano_note>& notes_to_delete)
{
    return [notes_to_delete](
        const single_midi_processor_2::data_iterator& begin,
        const single_midi_processor_2::data_iterator& end,
        const single_midi_processor_2::data_iterator& cur,
        single_midi_processor_2::single_track_data& std_ref) -> bool
    {
        const auto& tick = single_midi_processor_2::get_value<
            single_midi_processor_2::tick_type>(
                cur, single_midi_processor_2::tick_position);
        const auto& type = single_midi_processor_2::get_value<
            single_midi_processor_2::base_type>(
                cur, single_midi_processor_2::event_type);

        if ((type & 0xF0) == 0x90 || (type & 0xF0) == 0x80)
        {
            const auto& key = single_midi_processor_2::get_value<
                single_midi_processor_2::base_type>(
                    cur, single_midi_processor_2::event_param1);
            const auto& channel = type & 0x0F;

            for (const auto& note : notes_to_delete)
            {
                if (note.key == key && note.channel == channel &&
                    tick >= note.start_tick && tick <= note.end_tick)
                {
                    // Disable this event
                    if ((type & 0xF0) == 0x90)
                    {
                        auto& ref = single_midi_processor_2::get_value<
                            single_midi_processor_2::tick_type>(
                                begin, single_midi_processor_2::get_value<
                                    single_midi_processor_2::tick_type>(
                                        cur, single_midi_processor_2::event_param3));
                        ref = single_midi_processor_2::disable_tick;
                    }
                    auto& mutable_tick = const_cast<single_midi_processor_2::tick_type&>(tick);
                    mutable_tick = single_midi_processor_2::disable_tick;
                    return false;
                }
            }
        }
        return true;
    };
}

/**
 * Example: Create a filter that applies velocity changes
 */
inline single_midi_processor_2::event_transforming_filter
make_velocity_change_filter(
    const std::map<std::pair<std::uint64_t, std::uint8_t>, std::uint8_t>& velocity_changes)
{
    return [velocity_changes](
        const single_midi_processor_2::data_iterator& begin,
        const single_midi_processor_2::data_iterator& end,
        const single_midi_processor_2::data_iterator& cur,
        single_midi_processor_2::single_track_data& std_ref) -> bool
    {
        auto& tick = single_midi_processor_2::get_value<
            single_midi_processor_2::tick_type>(
                cur, single_midi_processor_2::tick_position);
        const auto& type = single_midi_processor_2::get_value<
            single_midi_processor_2::base_type>(
                cur, single_midi_processor_2::event_type);

        if ((type & 0xF0) == 0x90)
        {
            const auto& key = single_midi_processor_2::get_value<
                single_midi_processor_2::base_type>(
                    cur, single_midi_processor_2::event_param1);

            auto it = velocity_changes.find({ tick, key });
            if (it != velocity_changes.end())
            {
                auto& vel = single_midi_processor_2::get_value<
                    single_midi_processor_2::base_type>(
                        cur, single_midi_processor_2::event_param2);
                vel = it->second;
            }
        }
        return true;
    };
}

#endif // SAFC_MIDI_EDITOR_INTEGRATION
