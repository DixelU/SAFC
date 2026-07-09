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
		std::uint16_t track_index;  // Track identifier
		// Stable identity minted by the editor on load/insert; selection and
		// undo target notes through it, so it survives moves and edits
		std::uint32_t id;

		piano_note() : start_tick(0), end_tick(0), key(60), velocity(100), channel(0), track_index(0), id(0) {}

		piano_note(tick_type start, tick_type end, std::uint8_t k, std::uint8_t vel,
			std::uint8_t ch = 0, std::uint16_t track = 0)
			: start_tick(start), end_tick(end), key(k), velocity(vel), channel(ch), track_index(track), id(0)
		{
		}

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

	static constexpr std::uint8_t all_tracks = 0xFF;

	/**
	 * How a selection gesture combines with the current note set:
	 * replace it, add the hit notes, or remove them (Shift+Alt).
	 */
	enum class select_mode : std::uint8_t { replace, add, remove };

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
	 * Functor for inserting a note (id preassigned by the editor)
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
			editor.remove_note_by_id(note.id);
			editor.mark_dirty();
		}

		std::string description() const override { return "Insert Note"; }
	};

	/**
	 * Functor for deleting a set of notes by id
	 */
	struct delete_notes_op : edit_operation
	{
		std::vector<std::uint32_t> ids;
		std::vector<piano_note> removed_notes;

		delete_notes_op(std::vector<std::uint32_t>&& target_ids) : ids(std::move(target_ids)) {}

		void execute(midi_editor& editor) override
		{
			removed_notes.clear();
			const std::set<std::uint32_t> id_set(ids.begin(), ids.end());
			auto& notes = editor.notes;
			notes.erase(
				std::remove_if(notes.begin(), notes.end(),
					[this, &id_set](const piano_note& note)
			{
				if (id_set.count(note.id))
				{
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
		std::vector<std::uint32_t> ids;
		std::vector<std::pair<std::uint32_t, piano_note>> before; // id, prior geometry
		sgtick_type delta_ticks;
		int delta_keys;

		move_notes_op(std::vector<std::uint32_t>&& target_ids, sgtick_type dt = 0, int dk = 0)
			: ids(std::move(target_ids)), delta_ticks(dt), delta_keys(dk)
		{
		}

		void execute(midi_editor& editor) override
		{
			before.clear();
			const std::set<std::uint32_t> id_set(ids.begin(), ids.end());
			for (auto& note : editor.notes)
			{
				if (!id_set.count(note.id))
					continue;

				before.emplace_back(note.id, note);

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
			}
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior] : before)
			{
				if (auto* note = editor.find_note_by_id(id))
				{
					note->start_tick = prior.start_tick;
					note->end_tick = prior.end_tick;
					note->key = prior.key;
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
		std::uint32_t target_id;
		tick_type new_length;
		tick_type old_length = 0;
		bool applied = false;

		resize_note_op(std::uint32_t id, tick_type len) : target_id(id), new_length(len) {}

		void execute(midi_editor& editor) override
		{
			applied = false;
			if (auto* note = editor.find_note_by_id(target_id))
			{
				old_length = note->length();
				note->end_tick = note->start_tick + new_length;
				applied = true;
			}
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			if (!applied)
				return;
			if (auto* note = editor.find_note_by_id(target_id))
				note->end_tick = note->start_tick + old_length;
			editor.mark_dirty();
		}

		std::string description() const override { return "Resize Note"; }
	};

	/**
	 * Functor for changing note velocity
	 */
	struct velocity_change_op : edit_operation
	{
		std::vector<std::uint32_t> ids;
		std::uint8_t new_velocity;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> changes; // id, old velocity

		velocity_change_op(std::vector<std::uint32_t>&& target_ids, std::uint8_t vel)
			: ids(std::move(target_ids)), new_velocity(vel)
		{
		}

		void execute(midi_editor& editor) override
		{
			changes.clear();
			const std::set<std::uint32_t> id_set(ids.begin(), ids.end());
			for (auto& note : editor.notes)
			{
				if (id_set.count(note.id))
				{
					changes.emplace_back(note.id, note.velocity);
					note.velocity = new_velocity;
				}
			}
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, old_vel] : changes)
			{
				if (auto* note = editor.find_note_by_id(id))
					note->velocity = old_vel;
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
		std::vector<std::uint32_t> ids;
		int delta;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> changes; // id, old velocity

		velocity_adjust_op(std::vector<std::uint32_t>&& target_ids, int d)
			: ids(std::move(target_ids)), delta(d)
		{
		}

		void execute(midi_editor& editor) override
		{
			changes.clear();
			const std::set<std::uint32_t> id_set(ids.begin(), ids.end());
			for (auto& note : editor.notes)
			{
				if (id_set.count(note.id))
				{
					changes.emplace_back(note.id, note.velocity);
					note.velocity = std::uint8_t(std::clamp(int(note.velocity) + delta, 1, 127));
				}
			}
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, old_vel] : changes)
			{
				if (auto* note = editor.find_note_by_id(id))
					note->velocity = old_vel;
			}
			editor.mark_dirty();
		}

		std::string description() const override
		{
			return "Adjust Velocity (" + std::to_string(delta) + ")";
		}
	};

	/**
	 * Functor for deleting one exact note (right-click removal)
	 */
	struct delete_single_note_op : edit_operation
	{
		piano_note target; // full copy, id included

		delete_single_note_op(piano_note n) : target(n) {}

		void execute(midi_editor& editor) override
		{
			editor.remove_note_by_id(target.id);
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			editor.notes.push_back(target);
			editor.mark_dirty();
		}

		std::string description() const override { return "Erase Note"; }
	};

	/**
	 * Functor for inserting a batch of notes at once (paste, duplicate);
	 * ids are preassigned by the editor
	 */
	struct insert_notes_op : edit_operation
	{
		std::vector<piano_note> inserted;

		insert_notes_op(std::vector<piano_note>&& n) : inserted(std::move(n)) {}

		void execute(midi_editor& editor) override
		{
			for (const auto& note : inserted)
				editor.notes.push_back(note);
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			for (const auto& note : inserted)
				editor.remove_note_by_id(note.id);
			editor.mark_dirty();
		}

		std::string description() const override { return "Insert Notes"; }
	};

	/**
	 * Undo entry for a velocity-lane gesture: per-note old/new velocities recorded
	 * while the drag was applied transiently, committed as one operation.
	 */
	struct recorded_velocity_op : edit_operation
	{
		struct entry
		{
			piano_note note; // identity (matched by note.id)
			std::uint8_t old_velocity;
			std::uint8_t new_velocity;
		};
		std::vector<entry> entries;

		recorded_velocity_op(std::vector<entry>&& e) : entries(std::move(e)) {}

		void apply(midi_editor& editor, bool use_new)
		{
			for (const auto& en : entries)
			{
				if (auto* note = editor.find_note_by_id(en.note.id))
					note->velocity = use_new ? en.new_velocity : en.old_velocity;
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
		std::vector<std::uint32_t> ids;
		tick_type grid_resolution;
		std::vector<std::pair<std::uint32_t, piano_note>> changes; // id, prior geometry

		quantize_op(std::vector<std::uint32_t>&& target_ids, tick_type grid)
			: ids(std::move(target_ids)), grid_resolution(grid)
		{
		}

		void execute(midi_editor& editor) override
		{
			changes.clear();
			const std::set<std::uint32_t> id_set(ids.begin(), ids.end());
			for (auto& note : editor.notes)
			{
				if (!id_set.count(note.id))
					continue;

				piano_note before = note;

				// Quantize start
				auto remainder = note.start_tick % grid_resolution;
				if (remainder < grid_resolution / 2)
					note.start_tick -= remainder;
				else
					note.start_tick += (grid_resolution - remainder);

				// Adjust end to maintain relative length
				auto length_before = before.length();
				// Optionally quantize length too
				auto end_remainder = note.end_tick % grid_resolution;
				if (end_remainder < grid_resolution / 2)
					note.end_tick = note.start_tick + (length_before - end_remainder);
				else
					note.end_tick = note.start_tick + (length_before + (grid_resolution - end_remainder));

				if (before.start_tick != note.start_tick)
					changes.emplace_back(note.id, before);
			}
			editor.mark_dirty();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior] : changes)
			{
				if (auto* note = editor.find_note_by_id(id))
				{
					note->start_tick = prior.start_tick;
					note->end_tick = prior.end_tick;
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

	// Note-set selection: ids of the selected notes. Ids are stable across
	// moves and velocity edits, so the selection follows the notes.
	std::set<std::uint32_t> selected_notes;

	// Monotonic source for piano_note::id; never reused within a session
	std::uint32_t next_note_id = 1;

	// Copy/paste buffer; notes keep their original ticks and channels,
	// paste retargets them onto the active track
	std::vector<piano_note> clipboard;

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
		view_key_low(0), view_key_high(127), zoom_level(1.0f)
	{
	}

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
		selected_notes.clear();
		next_note_id = 1;
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
			}
			while (byte & 0x80 && cur < track_end);

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
						active_notes[note_key] = {current_tick, data1, data2, static_cast<std::uint8_t>(command & 0x0F)};
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
							notes.back().id = next_note_id++;
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
						notes.back().id = next_note_id++;
						active_notes.erase(it);
					}
					break;
				}
				case 0xA: case 0xB: case 0xE:
				{
					data2 = *(cur++);
					captured_events.push_back({current_tick, { command, data1, data2 }});
					break;
				}
				case 0xC: case 0xD:
				{
					captured_events.push_back({current_tick, { command, data1 }});
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
					}
					while (byte & 0x80 && cur < track_end);

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
						raw_event ev{current_tick, {}};
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
			notes.back().id = next_note_id++;
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

	/**
	 * Insert a note; returns its freshly minted id
	 */
	std::uint32_t insert_note(tick_type start, tick_type end, std::uint8_t key,
		std::uint8_t velocity, std::uint8_t channel = 0, std::uint8_t track = 0)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		piano_note note(start, end, key, velocity, channel, track);
		note.id = next_note_id++;
		auto op = std::make_unique<insert_note_op>(note);
		op->execute(*this);
		push_undo(std::move(op));
		return note.id;
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
		if (auto* note = find_note_by_id(ident.id))
		{
			old_velocity = note->velocity;
			note->velocity = std::clamp<std::uint8_t>(velocity, 1, 127);
			mark_dirty();
			return true;
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
		if (selected_notes.empty() || !delta)
			return;

		auto op = std::make_unique<velocity_adjust_op>(selected_ids_vector(), delta);
		op->execute(*this);
		push_undo(std::move(op));
	}

	/**
	 * Erase the topmost note at (tick, key). track_filter = all_tracks
	 * erases from any track, otherwise only from the given one.
	 */
	bool erase_note_at(tick_type tick, std::uint8_t key,
		std::uint8_t track_filter = all_tracks)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);

		const piano_note* found = nullptr;
		for (const auto& note : notes)
		{
			if (note.key == key && tick >= note.start_tick && tick < note.end_tick &&
				(track_filter == all_tracks || note.track_index == track_filter))
				found = &note; // last one wins: it is drawn on top
		}
		if (!found)
			return false;

		const auto id = found->id;
		auto op = std::make_unique<delete_single_note_op>(*found);
		op->execute(*this);
		push_undo(std::move(op));
		selected_notes.erase(id);
		return true;
	}

	/**
	 * Find the topmost note at (tick, key), optionally limited to one track.
	 * With tolerances > 0, falls back to the nearest note within
	 * ±key_tolerance semitones and ±tick_tolerance ticks of the point.
	 */
	bool find_note_at(tick_type tick, std::uint8_t key, piano_note& out,
		tick_type tick_tolerance = 0, std::uint8_t key_tolerance = 0,
		std::uint8_t track_filter = all_tracks) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);

		bool found = false;
		for (const auto& note : notes)
		{
			if (track_filter != all_tracks && note.track_index != track_filter)
				continue;
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
			if (track_filter != all_tracks && note.track_index != track_filter)
				continue;
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
		if (selected_notes.empty())
			return;

		auto op = std::make_unique<delete_notes_op>(selected_ids_vector());
		op->execute(*this);
		push_undo(std::move(op));
		selected_notes.clear();
	}

	/**
	 * Copy the selected notes into the clipboard.
	 * The clipboard survives track switches, so notes can be pasted across tracks.
	 * Returns the number of notes copied.
	 */
	std::size_t copy_selected_notes()
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (selected_notes.empty())
			return 0;

		std::vector<piano_note> copied;
		for (const auto& note : notes)
			if (selected_notes.count(note.id))
				copied.push_back(note);

		if (copied.empty())
			return 0;
		clipboard = std::move(copied);
		return clipboard.size();
	}

	bool has_clipboard() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return !clipboard.empty();
	}

	/**
	 * Paste the clipboard into the active track at the original position.
	 * Notes take the target track's primary channel (kept as copied when the
	 * target is multi-channel) and fresh ids. The pasted notes become the new
	 * selection, so they can be moved right away. Returns the number pasted.
	 */
	std::size_t paste_clipboard()
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (clipboard.empty())
			return 0;

		std::uint8_t channel_override = 0xFF;
		auto it = tracks.find(active_track);
		if (it != tracks.end() && it->second.channel != 0xFF)
			channel_override = it->second.channel;

		auto pasted = clipboard;
		std::set<std::uint32_t> new_ids;
		for (auto& note : pasted)
		{
			note.id = next_note_id++;
			note.track_index = active_track;
			if (channel_override != 0xFF)
				note.channel = channel_override;
			new_ids.insert(note.id);
		}

		const auto count = pasted.size();
		auto op = std::make_unique<insert_notes_op>(std::move(pasted));
		op->execute(*this);
		push_undo(std::move(op));

		selected_notes = std::move(new_ids);
		return count;
	}

	/**
	 * Duplicate the selected notes right after the group ends (Ctrl+B):
	 * copies shifted right by the group's tick extent. The selection moves
	 * onto the duplicate, so repeated presses chain. Returns the copy count.
	 */
	std::size_t duplicate_selected()
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);

		tick_type begin = 0, end = 0;
		std::uint8_t key_low = 0, key_high = 0;
		if (!get_selection_bounds(begin, end, key_low, key_high))
			return 0;
		const auto shift = end - begin;

		std::vector<piano_note> copies;
		std::set<std::uint32_t> new_ids;
		for (const auto& note : notes)
		{
			if (!selected_notes.count(note.id))
				continue;
			auto copy = note;
			copy.id = next_note_id++;
			copy.start_tick += shift;
			copy.end_tick += shift;
			new_ids.insert(copy.id);
			copies.push_back(copy);
		}
		if (copies.empty())
			return 0;

		const auto count = copies.size();
		auto op = std::make_unique<insert_notes_op>(std::move(copies));
		op->execute(*this);
		push_undo(std::move(op));

		selected_notes = std::move(new_ids);
		return count;
	}

	void move_selected_notes(sgtick_type delta_ticks = 0, int delta_keys = 0)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (selected_notes.empty())
			return;

		auto op = std::make_unique<move_notes_op>(selected_ids_vector(), delta_ticks, delta_keys);
		op->execute(*this);
		push_undo(std::move(op));
		// Ids are stable, so the selection follows the moved notes by itself
	}

	void resize_note(std::uint32_t note_id, tick_type new_length)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		auto op = std::make_unique<resize_note_op>(note_id, new_length);
		op->execute(*this);
		push_undo(std::move(op));
	}

	void change_velocity_selected(std::uint8_t velocity)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (selected_notes.empty())
			return;

		auto op = std::make_unique<velocity_change_op>(selected_ids_vector(), velocity);
		op->execute(*this);
		push_undo(std::move(op));
	}

	void quantize_selected(tick_type grid_resolution)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (selected_notes.empty() || !grid_resolution)
			return;

		auto op = std::make_unique<quantize_op>(selected_ids_vector(), grid_resolution);
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
		prune_selection();
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
		prune_selection();
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

	piano_note* find_note_by_id(std::uint32_t id)
	{
		for (auto& note : notes)
			if (note.id == id)
				return &note;
		return nullptr;
	}

	bool remove_note_by_id(std::uint32_t id)
	{
		for (auto it = notes.begin(); it != notes.end(); ++it)
		{
			if (it->id == id)
			{
				notes.erase(it);
				return true;
			}
		}
		return false;
	}

	std::vector<std::uint32_t> selected_ids_vector() const
	{
		return std::vector<std::uint32_t>(selected_notes.begin(), selected_notes.end());
	}

	// Drop selected ids whose notes no longer exist (after undo of an insert etc.)
	void prune_selection()
	{
		if (selected_notes.empty())
			return;
		std::set<std::uint32_t> alive;
		for (const auto& note : notes)
			if (selected_notes.count(note.id))
				alive.insert(note.id);
		selected_notes.swap(alive);
	}

	void mark_dirty() { is_dirty = true; }

public:
	// ========================================================================
	// Selection Management (note-id set)
	// ========================================================================

	/**
	 * Combine all notes intersecting the rectangle with the selection
	 * according to the mode. Returns the resulting selection size.
	 */
	std::size_t select_rect(tick_type begin, tick_type end,
		std::uint8_t key_begin, std::uint8_t key_end,
		std::uint8_t track_filter = all_tracks,
		select_mode mode = select_mode::replace)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (mode == select_mode::replace)
			selected_notes.clear();

		for (const auto& note : notes)
		{
			if (track_filter != all_tracks && note.track_index != track_filter)
				continue;
			if (note.start_tick >= end || note.end_tick <= begin ||
				note.key < key_begin || note.key > key_end)
				continue;

			if (mode == select_mode::remove)
				selected_notes.erase(note.id);
			else
				selected_notes.insert(note.id);
		}
		return selected_notes.size();
	}

	void select_note(std::uint32_t id, select_mode mode = select_mode::replace)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (mode == select_mode::replace)
			selected_notes.clear();
		if (mode == select_mode::remove)
			selected_notes.erase(id);
		else
			selected_notes.insert(id);
	}

	void set_selected_ids(std::set<std::uint32_t> ids)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		selected_notes = std::move(ids);
	}

	std::set<std::uint32_t> get_selected_ids() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return selected_notes;
	}

	std::vector<piano_note> get_selected_notes() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<piano_note> result;
		for (const auto& note : notes)
			if (selected_notes.count(note.id))
				result.push_back(note);
		return result;
	}

	bool has_selection() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return !selected_notes.empty();
	}

	std::size_t selection_count() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return selected_notes.size();
	}

	bool is_note_selected(std::uint32_t id) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return selected_notes.count(id) != 0;
	}

	/**
	 * Tick/key bounding box of the selected notes; false when nothing is selected
	 */
	bool get_selection_bounds(tick_type& begin, tick_type& end,
		std::uint8_t& key_low, std::uint8_t& key_high) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		bool any = false;
		for (const auto& note : notes)
		{
			if (!selected_notes.count(note.id))
				continue;
			if (!any)
			{
				begin = note.start_tick;
				end = note.end_tick;
				key_low = key_high = note.key;
				any = true;
			}
			else
			{
				begin = std::min(begin, note.start_tick);
				end = std::max(end, note.end_tick);
				key_low = std::min(key_low, note.key);
				key_high = std::max(key_high, note.key);
			}
		}
		return any;
	}

	// ========================================================================
	// Active Track
	// ========================================================================

	void set_active_track(std::uint8_t track)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		active_track = track;
		selected_notes.clear(); // selection gestures are per-track
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
		selected_notes.clear();
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
		const auto max_duration = std::max({get_total_ticks(), tick_type(ppqn) * 4, min_view_duration}) * 2;
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
		const char bytes[2] = {char(v >> 8), char(v)};
		out.write(bytes, 2);
	}

	static void write_be32(std::ostream& out, std::uint32_t v)
	{
		const char bytes[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
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
			events.push_back({note.start_tick, 2,
			    { base_type(0x90 | note.channel), note.key, std::max<base_type>(note.velocity, 1) }});
			events.push_back({note.end_tick, 0,
			    { base_type(0x80 | note.channel), note.key, 0x40 }});
		}
		for (const auto& raw : raw_events)
			events.push_back({raw.tick, 1, raw.bytes});

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
