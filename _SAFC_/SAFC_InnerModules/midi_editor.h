#pragma once
#ifndef SAFC_MIDI_EDITOR
#define SAFC_MIDI_EDITOR

#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <optional>
#include <algorithm>
#include <limits>
#include <fstream>
#include <cstring>
#include <set>
#include <queue>

#include <memory_mapped_file_reader.h>

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"
#include "playback_event_source.h"

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
			if (track_index != other.track_index) return track_index < other.track_index;
			return id < other.id;
		}

		bool operator==(const piano_note& other) const
		{
			return start_tick == other.start_tick && end_tick == other.end_tick &&
				key == other.key && velocity == other.velocity &&
				channel == other.channel && track_index == other.track_index &&
				id == other.id;
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

	enum class control_lane : std::uint8_t { pitch_bend, pan, channel_volume };
	enum class lfo_shape : std::uint8_t { sine, triangle, square };

	// Compact score-wide selection. A tree node per selected note is prohibitively
	// expensive at black-MIDI scale; membership is a bitset and the dense id list
	// keeps iteration proportional to the selection. Erases are lazy and compacted
	// when stale entries outnumber live ones.
	struct note_selection
	{
		std::vector<std::uint64_t> bits;
		std::vector<std::uint32_t> ids;
		std::unordered_set<std::uint32_t> stale_ids;
		std::size_t live_count = 0;

		bool contains(std::uint32_t id) const
		{
			const auto word = std::size_t(id) >> 6;
			return word < bits.size() && (bits[word] & (std::uint64_t(1) << (id & 63))) != 0;
		}

		std::size_t count(std::uint32_t id) const { return contains(id) ? 1 : 0; }
		bool empty() const { return live_count == 0; }
		std::size_t size() const { return live_count; }

		void insert(std::uint32_t id)
		{
			const auto word = std::size_t(id) >> 6;
			if (word >= bits.size())
				bits.resize(word + 1);
			const auto mask = std::uint64_t(1) << (id & 63);
			if (bits[word] & mask)
				return;
			bits[word] |= mask;
			if (!stale_ids.erase(id))
				ids.push_back(id);
			++live_count;
		}

		std::size_t erase(std::uint32_t id)
		{
			const auto word = std::size_t(id) >> 6;
			if (word >= bits.size())
				return 0;
			const auto mask = std::uint64_t(1) << (id & 63);
			if (!(bits[word] & mask))
				return 0;
			bits[word] &= ~mask;
			--live_count;
			stale_ids.insert(id);
			return 1;
		}

		void clear()
		{
			bits.clear();
			ids.clear();
			stale_ids.clear();
			live_count = 0;
		}

		void compact()
		{
			if (ids.size() <= live_count * 2 + 64)
				return;
			ids.erase(std::remove_if(ids.begin(), ids.end(),
				[&](std::uint32_t id) { return !contains(id); }), ids.end());
			stale_ids.clear();
		}

		struct iterator
		{
			const note_selection* owner = nullptr;
			std::size_t index = 0;

			void skip_stale()
			{
				while (index < owner->ids.size() && !owner->contains(owner->ids[index]))
					++index;
			}
			std::uint32_t operator*() const { return owner->ids[index]; }
			iterator& operator++() { ++index; skip_stale(); return *this; }
			bool operator==(const iterator& other) const { return index == other.index; }
			bool operator!=(const iterator& other) const { return !(*this == other); }
		};

		iterator begin() const { iterator it{this, 0}; it.skip_stale(); return it; }
		iterator end() const { return iterator{this, ids.size()}; }
		void swap(note_selection& other)
		{
			bits.swap(other.bits);
			ids.swap(other.ids);
			stale_ids.swap(other.stale_ids);
			std::swap(live_count, other.live_count);
		}
	};

	struct channel_control_point
	{
		tick_type tick = 0;
		std::uint8_t channel = 0;
		std::uint16_t value = 0; // pitch bend: 0..16383, CC lanes: 0..127
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
	 * Functor for inserting a note (id preassigned by the editor)
	 */
	struct insert_note_op : edit_operation
	{
		piano_note note;

		insert_note_op(piano_note n) : note(n) {}

		void execute(midi_editor& editor) override
		{
			editor.add_note(note);
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			editor.remove_note_by_id(note.id);
			editor.mark_dirty_keep_order();
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
			removed_notes.reserve(ids.size());
			for (const auto id : ids)
				if (const auto* note = editor.find_note_by_id(id))
				{
					removed_notes.push_back(*note);
					editor.remove_note_by_id(id);
				}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (const auto& note : removed_notes)
				editor.add_note(note);
			editor.mark_dirty_keep_order();
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
			before.reserve(ids.size());
			for (const auto id : ids)
			{
				const auto* current = editor.find_note_by_id(id);
				if (!current)
					continue;
				auto note = *current;

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
				editor.replace_note(std::move(note));
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior] : before)
			{
				if (editor.find_note_by_id(id))
					editor.replace_note(prior);
			}
			editor.mark_dirty_keep_order();
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
			if (const auto* current = editor.find_note_by_id(target_id))
			{
				auto note = *current;
				old_length = note.length();
				note.end_tick = note.start_tick + new_length;
				editor.replace_note(std::move(note));
				applied = true;
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			if (!applied)
				return;
			if (const auto* current = editor.find_note_by_id(target_id))
			{
				auto note = *current;
				note.end_tick = note.start_tick + old_length;
				editor.replace_note(std::move(note));
			}
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Resize Note"; }
	};

	/**
	 * Functor for changing the length of a set of notes by a common delta.
	 * Each note squashes to a 1-tick minimum instead of vanishing.
	 */
	struct resize_notes_op : edit_operation
	{
		std::vector<std::uint32_t> ids;
		sgtick_type delta_length;
		std::vector<std::pair<std::uint32_t, tick_type>> before; // id, prior end_tick

		resize_notes_op(std::vector<std::uint32_t>&& target_ids, sgtick_type delta)
			: ids(std::move(target_ids)), delta_length(delta)
		{
		}

		void execute(midi_editor& editor) override
		{
			before.clear();
			before.reserve(ids.size());
			for (const auto id : ids)
			{
				const auto* current = editor.find_note_by_id(id);
				if (!current)
					continue;
				auto note = *current;

				before.emplace_back(note.id, note.end_tick);
				const auto new_length = std::max<sgtick_type>(1,
					sgtick_type(note.length()) + delta_length);
				note.end_tick = note.start_tick + tick_type(new_length);
				editor.replace_note(std::move(note));
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior_end] : before)
			{
				if (const auto* current = editor.find_note_by_id(id))
				{
					auto note = *current;
					note.end_tick = prior_end;
					editor.replace_note(std::move(note));
				}
			}
			editor.mark_dirty_keep_order();
		}

		std::string description() const override
		{
			return "Resize Notes (" + std::to_string(delta_length) + " ticks)";
		}
	};

	/**
	 * Functor for scaling notes in time about a pivot tick (FL-style selection
	 * scale handle). Both edges move proportionally; a note that would collapse
	 * squashes to a 1-tick minimum instead.
	 */
	struct stretch_notes_op : edit_operation
	{
		std::vector<std::uint32_t> ids;
		tick_type pivot;
		double factor;
		std::vector<std::pair<std::uint32_t, piano_note>> before; // id, prior geometry

		stretch_notes_op(std::vector<std::uint32_t>&& target_ids, tick_type pivot_tick, double f)
			: ids(std::move(target_ids)), pivot(pivot_tick), factor(f)
		{
		}

		void execute(midi_editor& editor) override
		{
			before.clear();
			before.reserve(ids.size());
			for (const auto id : ids)
			{
				const auto* current = editor.find_note_by_id(id);
				if (!current)
					continue;
				auto note = *current;

				before.emplace_back(note.id, note);

				auto new_start = std::llround(double(pivot) +
					(double(note.start_tick) - double(pivot)) * factor);
				auto new_end = std::llround(double(pivot) +
					(double(note.end_tick) - double(pivot)) * factor);
				if (new_start < 0)
					new_start = 0;
				if (new_end <= new_start)
					new_end = new_start + 1;

				note.start_tick = tick_type(new_start);
				note.end_tick = tick_type(new_end);
				editor.replace_note(std::move(note));
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior] : before)
			{
				if (editor.find_note_by_id(id))
					editor.replace_note(prior);
			}
			editor.mark_dirty_keep_order();
		}

		std::string description() const override
		{
			return "Scale Notes (" + std::to_string(int(std::lround(factor * 100.0))) + "%)";
		}
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
			changes.reserve(ids.size());
			for (const auto id : ids)
			{
				if (auto* note = editor.find_note_by_id(id))
				{
					changes.emplace_back(note->id, note->velocity);
					note->velocity = new_velocity;
				}
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, old_vel] : changes)
			{
				if (auto* note = editor.find_note_by_id(id))
					note->velocity = old_vel;
			}
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Change Velocity"; }
	};

	/** Functor for assigning a MIDI channel to a set of notes. */
	struct channel_change_op : edit_operation
	{
		std::vector<std::uint32_t> ids;
		std::uint8_t new_channel;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> changes; // id, old channel

		channel_change_op(std::vector<std::uint32_t>&& target_ids, std::uint8_t channel)
			: ids(std::move(target_ids)), new_channel(channel & 0x0F)
		{
		}

		void execute(midi_editor& editor) override
		{
			changes.clear();
			changes.reserve(ids.size());
			for (const auto id : ids)
			{
				auto* note = editor.find_note_by_id(id);
				if (!note)
					continue;
				changes.emplace_back(note->id, note->channel);
				note->channel = new_channel;
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, old_channel] : changes)
				if (auto* note = editor.find_note_by_id(id))
					note->channel = old_channel;
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Change Channel"; }
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
			changes.reserve(ids.size());
			for (const auto id : ids)
			{
				if (auto* note = editor.find_note_by_id(id))
				{
					changes.emplace_back(note->id, note->velocity);
					note->velocity = std::uint8_t(std::clamp(int(note->velocity) + delta, 1, 127));
				}
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, old_vel] : changes)
			{
				if (auto* note = editor.find_note_by_id(id))
					note->velocity = old_vel;
			}
			editor.mark_dirty_keep_order();
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
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			editor.add_note(target);
			editor.mark_dirty_keep_order();
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
				editor.add_note(note);
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (const auto& note : inserted)
				editor.remove_note_by_id(note.id);
			editor.mark_dirty_keep_order();
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
			editor.mark_dirty_keep_order();
		}

		void execute(midi_editor& editor) override { apply(editor, true); }
		void undo(midi_editor& editor) override { apply(editor, false); }

		std::string description() const override { return "Edit Velocities"; }
	};

	/** One undo entry for tools which can replace/split a group of notes. */
	struct note_tool_op : edit_operation
	{
		std::vector<piano_note> before;
		std::vector<piano_note> after;
		std::string label;

		note_tool_op(std::vector<piano_note> old_notes,
			std::vector<piano_note> new_notes, std::string description)
			: before(std::move(old_notes)), after(std::move(new_notes)),
			label(std::move(description)) {}

		static void remove(midi_editor& editor, const std::vector<piano_note>& values)
		{
			for (const auto& note : values)
				editor.remove_note_by_id(note.id);
		}

		void apply(midi_editor& editor, const std::vector<piano_note>& remove_values,
			const std::vector<piano_note>& add_values)
		{
			remove(editor, remove_values);
			for (const auto& note : add_values)
				editor.add_note(note);
			editor.selected_notes.clear();
			for (const auto& note : add_values)
				editor.selected_notes.insert(note.id);
			editor.mark_dirty_keep_order();
		}

		void execute(midi_editor& editor) override { apply(editor, before, after); }
		void undo(midi_editor& editor) override { apply(editor, after, before); }
		std::string description() const override { return label; }
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
			changes.reserve(ids.size());
			for (const auto id : ids)
			{
				const auto* current = editor.find_note_by_id(id);
				if (!current)
					continue;
				auto note = *current;
				const auto before = note;

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

				if (!(before == note))
				{
					changes.emplace_back(note.id, before);
					editor.replace_note(std::move(note));
				}
			}
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (auto& [id, prior] : changes)
			{
				if (editor.find_note_by_id(id))
					editor.replace_note(prior);
			}
			editor.mark_dirty_keep_order();
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

public:
	// Load progress feedback: (bytes parsed, bytes total). Called from inside
	// load_file on the loading thread, throttled to ~200 reports per file.
	std::function<void(std::uint64_t, std::uint64_t)> on_load_progress;

private:
	std::wstring filename;
	std::unique_ptr<dixelu::memory_mapped_file_reader> mmap_file;

	// Load progress state (only touched while load_file runs)
	const std::uint8_t* progress_begin = nullptr;
	const std::uint8_t* progress_next_report = nullptr;
	std::uint64_t progress_total = 0;
	std::uint64_t progress_chunk = 0;

	void report_load_progress(const std::uint8_t* cur)
	{
		if (!on_load_progress || cur < progress_next_report)
			return;
		progress_next_report = cur + progress_chunk;
		on_load_progress(std::uint64_t(cur - progress_begin), progress_total);
	}

	// Editor-friendly base note list, built once while loading and kept sorted by
	// start_tick. Interactive edits do not rewrite this potentially enormous
	// vector: changed/inserted notes live in a sparse overlay and base notes that
	// they replace (or delete) are suppressed by id. This keeps a one-note edit
	// independent of the total MIDI size.
	mutable std::vector<piano_note> notes;
	std::unordered_map<std::uint32_t, piano_note> edited_notes;
	std::unordered_set<std::uint32_t> suppressed_base_note_ids;
	mutable std::vector<std::uint32_t> base_note_index_by_id;
	std::size_t logical_note_count = 0;
	std::map<std::uint8_t, track_info> tracks;

	// Sorted-base query acceleration (rebuilt after loading):
	// any note intersecting [a, b) must start in [a - max_typical_length, b),
	// except the few outliers longer than that, kept in a side list.
	// The dense id->base-index table costs four bytes per loaded note. Sparse
	// edit state costs memory only for notes touched in this session.
	mutable bool notes_order_dirty = true;
	mutable tick_type max_typical_length = 0;
	mutable tick_type cached_max_end_tick = 0;
	mutable std::vector<std::size_t> long_note_indices;

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

	struct tempo_change_op : edit_operation
	{
		tick_type tick;
		std::uint32_t tempo_us;
		std::vector<std::pair<std::uint8_t, raw_event>> previous;

		tempo_change_op(tick_type tk, std::uint32_t us) : tick(tk), tempo_us(us) {}

		void execute(midi_editor& editor) override
		{
			previous = editor.remove_tempo_events_at(tick);
			editor.raw_track_events[0].push_back({tick, editor.make_tempo_event_bytes(tempo_us)});
			editor.sort_raw_track(0);
			editor.rebuild_tempo_events();
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			editor.remove_tempo_events_at(tick);
			for (const auto& [track, event] : previous)
			{
				editor.raw_track_events[track].push_back(event);
				editor.sort_raw_track(track);
			}
			editor.rebuild_tempo_events();
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Edit Tempo Map"; }
	};

	struct tempo_batch_op : edit_operation
	{
		std::vector<tempo_change_op> changes;

		explicit tempo_batch_op(std::vector<std::pair<tick_type, std::uint32_t>> points)
		{
			changes.reserve(points.size());
			for (const auto& [tick, tempo_us] : points)
				changes.emplace_back(tick, tempo_us);
		}

		void execute(midi_editor& editor) override
		{
			for (auto& change : changes)
				change.execute(editor);
		}

		void undo(midi_editor& editor) override
		{
			for (auto it = changes.rbegin(); it != changes.rend(); ++it)
				it->undo(editor);
		}

		std::string description() const override { return "Edit Tempo Map"; }
	};

	struct raw_control_change_op : edit_operation
	{
		std::uint8_t track;
		control_lane lane;
		std::uint8_t channel;
		tick_type tick;
		std::uint16_t value;
		std::optional<raw_event> previous;

		raw_control_change_op(std::uint8_t tr, control_lane ln, std::uint8_t ch,
			tick_type tk, std::uint16_t val)
			: track(tr), lane(ln), channel(ch), tick(tk), value(val)
		{
		}

		void execute(midi_editor& editor) override
		{
			previous = editor.remove_control_event_at(track, lane, channel, tick);
			editor.raw_track_events[track].push_back({tick,
				editor.make_control_event_bytes(lane, channel, value)});
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			editor.remove_control_event_at(track, lane, channel, tick);
			if (previous)
				editor.raw_track_events[track].push_back(*previous);
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Edit Controller Lane"; }
	};

	struct raw_control_change_batch_op : edit_operation
	{
		struct entry
		{
			tick_type tick;
			std::uint16_t value;
			std::optional<raw_event> previous;
		};

		std::uint8_t track;
		control_lane lane;
		std::uint8_t channel;
		std::vector<entry> entries;

		raw_control_change_batch_op(std::uint8_t tr, control_lane ln, std::uint8_t ch,
			std::vector<std::pair<tick_type, std::uint16_t>> points)
			: track(tr), lane(ln), channel(ch)
		{
			entries.reserve(points.size());
			for (const auto& [tick, value] : points)
				entries.push_back({tick, value, std::nullopt});
		}

		void execute(midi_editor& editor) override
		{
			for (auto& en : entries)
			{
				en.previous = editor.remove_control_event_at(track, lane, channel, en.tick);
				editor.raw_track_events[track].push_back({en.tick,
					editor.make_control_event_bytes(lane, channel, en.value)});
			}
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			for (const auto& en : entries)
			{
				editor.remove_control_event_at(track, lane, channel, en.tick);
				if (en.previous)
					editor.raw_track_events[track].push_back(*en.previous);
			}
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "Edit Controller Lane"; }
	};

	/** Replaces a controller range, rather than leaving old points between LFO samples. */
	struct raw_control_range_op : edit_operation
	{
		std::uint8_t track;
		control_lane lane;
		std::uint8_t channel;
		tick_type begin, end;
		std::vector<raw_event> before;
		std::vector<raw_event> after;

		raw_control_range_op(std::uint8_t tr, control_lane ln, std::uint8_t ch,
			tick_type first, tick_type last,
			std::vector<std::pair<tick_type, std::uint16_t>> points)
			: track(tr), lane(ln), channel(ch), begin(first), end(last)
		{
			for (const auto& [tick, value] : points)
				after.push_back({tick, midi_editor::make_control_event_bytes(lane, channel, value)});
		}

		void remove_range(midi_editor& editor, bool capture)
		{
			auto& events = editor.raw_track_events[track];
			if (capture)
				before.clear();
			events.erase(std::remove_if(events.begin(), events.end(), [&](const raw_event& ev)
			{
				std::uint16_t ignored = 0;
				const bool match = ev.tick >= begin && ev.tick <= end &&
					editor.decode_control_event(ev, lane, channel, ignored);
				if (match && capture)
					before.push_back(ev);
				return match;
			}), events.end());
		}

		void execute(midi_editor& editor) override
		{
			remove_range(editor, true);
			auto& events = editor.raw_track_events[track];
			events.insert(events.end(), after.begin(), after.end());
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		void undo(midi_editor& editor) override
		{
			remove_range(editor, false);
			auto& events = editor.raw_track_events[track];
			events.insert(events.end(), before.begin(), before.end());
			editor.sort_raw_track(track);
			editor.mark_dirty_keep_order();
		}

		std::string description() const override { return "LFO Controller"; }
	};

	// Track edits apply to (piano roll focus); notes of other tracks are shown dimmed
	std::uint8_t active_track = 0;

	// Note-set selection: ids of the selected notes. Ids are stable across
	// moves and velocity edits, so the selection follows the notes.
	note_selection selected_notes;

	// Monotonic source for piano_note::id; never reused within a session
	std::uint32_t next_note_id = 1;

	// Copy/paste buffer; notes keep their original ticks and channels,
	// paste retargets them onto the active track
	std::vector<piano_note> clipboard;

	// Undo/Redo stacks
	std::vector<std::unique_ptr<edit_operation>> undo_stack;
	std::vector<std::unique_ptr<edit_operation>> redo_stack;
	std::unique_ptr<edit_operation> tool_preview;
	bool tool_preview_session = false;
	bool tool_preview_was_dirty = false;
	note_selection tool_preview_original_selection;
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
		edited_notes.clear();
		suppressed_base_note_ids.clear();
		base_note_index_by_id.clear();
		logical_note_count = 0;
		tracks.clear();
		tempo_events.clear();
		raw_track_events.clear();
		undo_stack.clear();
		redo_stack.clear();
		tool_preview.reset();
		tool_preview_session = false;
		selected_notes.clear();
		next_note_id = 1;
		active_track = 0;

		mmap_file = std::make_unique<dixelu::memory_mapped_file_reader>(filepath);
		if (!mmap_file || !mmap_file->good())
			return false;

		filename = filepath;

		const auto begin = reinterpret_cast<const std::uint8_t*>(mmap_file->data());
		const auto size = mmap_file->size();
		const auto end = begin + size;

		if (size < 18 || std::memcmp(begin, "MThd", 4) != 0)
			return false;

		// Read division; SMPTE division is unsupported, fall back to a sane default
		ppqn = (begin[12] << 8) | (begin[13]);
		if (ppqn == 0 || (ppqn & 0x8000))
			ppqn = 480;
		ticks_per_beat = ppqn;

		// Progress reporting: at most ~200 updates, at least 1 MB apart
		progress_begin = begin;
		progress_total = size;
		progress_chunk = std::max<std::uint64_t>(size / 200, std::uint64_t(1) << 20);
		progress_next_report = begin;

		// Parse tracks and extract notes
		auto ptr = begin + 14;
		std::uint8_t track_index = 0;

		while (ptr < end && track_index < 0xFF)
		{
			if (!parse_track_mmap(ptr, end, track_index++))
				break;
		}

		// The final sorts can take a moment too on huge files; signal parse done
		if (on_load_progress)
			on_load_progress(size, size);

		std::sort(tempo_events.begin(), tempo_events.end());
		std::sort(notes.begin(), notes.end());
		notes_order_dirty = true; // recompute scan bounds for the new file
		rebuild_base_note_index();
		logical_note_count = notes.size();

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
		std::unordered_map<std::uint16_t, std::deque<pending_note>> active_notes; // key=(channel<<8)|key

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
			report_load_progress(cur);

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
						active_notes[note_key].push_back(
							{current_tick, data1, data2, static_cast<std::uint8_t>(command & 0x0F)});
						if (primary_channel == 0xFF)
							primary_channel = command & 0x0F;
					}
					else
					{
						// Note On with velocity 0 = Note Off
						std::uint16_t note_key = (std::uint16_t(command & 0x0F) << 8) | data1;
						auto it = active_notes.find(note_key);
						if (it != active_notes.end() && !it->second.empty())
						{
							auto note = it->second.front();
							it->second.pop_front();
							notes.emplace_back(note.start, current_tick,
								note.key, note.velocity,
								note.channel, track_index);
							notes.back().id = next_note_id++;
							if (it->second.empty())
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
					if (it != active_notes.end() && !it->second.empty())
					{
						auto note = it->second.front();
						it->second.pop_front();
						notes.emplace_back(note.start, current_tick,
							note.key, note.velocity,
							note.channel, track_index);
						notes.back().id = next_note_id++;
						if (it->second.empty())
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
		for (auto& [key, pending] : active_notes)
		{
			for (const auto& note : pending)
			{
				notes.emplace_back(note.start, current_tick + 1,
					note.key, note.velocity, note.channel, track_index);
				notes.back().id = next_note_id++;
			}
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
		for_each_note_intersecting(start_tick, end_tick,
			[&](const piano_note& note)
		{
			if (note.key >= key_low && note.key <= key_high)
				result.push_back(note);
		});
		if (!edited_notes.empty())
			std::sort(result.begin(), result.end());
		return result;
	}

	/**
	 * Get notes at a specific time slice (for vertical selection)
	 */
	std::vector<piano_note> get_notes_at_tick(tick_type tick) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<piano_note> result;
		for_each_note_intersecting(tick, tick + 1,
			[&](const piano_note& note)
		{
			result.push_back(note);
		});
		if (!edited_notes.empty())
			std::sort(result.begin(), result.end());
		return result;
	}

	/**
	 * Get notes on a specific key/pitch (for horizontal selection)
	 */
	std::vector<piano_note> get_notes_on_key(std::uint8_t key) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<piano_note> result;
		for_each_logical_note([&](const piano_note& note)
		{
			if (note.key == key)
				result.push_back(note);
		});
		std::sort(result.begin(), result.end());
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
			mark_dirty_keep_order();
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
		mark_dirty_keep_order();
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

	/** Notes affected by an FL-style tool: selection, or all notes on the active track. */
	std::vector<piano_note> get_tool_target_notes() const;
	void prepare_tool_preview(bool preview);
	void accept_tool_preview();
	void cancel_tool_preview();
	void commit_note_tool(std::vector<piano_note> before,
		std::vector<piano_note> after, const std::string& label, bool preview = false);

	/** Chop notes against a repeated beat subdivision, optionally aligned to the score grid. */
	std::size_t chop_tool(unsigned divisions_per_beat, double time_multiplier,
		double gap_percent, bool absolute_pattern, bool preview = false);

	void flip_tool(bool horizontal, bool preserve_start_times, bool vertical, bool preview = false);

	/** Rhythmic slice/removal and nonlinear timing transform inspired by FL's Claw. */
	std::size_t claw_tool(double period_beats, unsigned trash_every,
		double time_distortion, bool remove_short, bool stretch_to_compensate,
		bool preview = false);

	static double lfo_sample(lfo_shape shape, double phase_cycles);

	std::size_t lfo_velocity_tool(double center, double range, double cycles,
		double phase, lfo_shape shape, bool preview = false);

	std::size_t lfo_control_tool(control_lane lane, std::uint8_t channel,
		tick_type begin, tick_type end, tick_type step, double center,
		double range, double cycles, double phase, lfo_shape shape, bool preview = false);

	/**
	 * Erase the topmost note at (tick, key). track_filter = all_tracks
	 * erases from any track, otherwise only from the given one.
	 */
	bool erase_note_at(tick_type tick, std::uint8_t key,
		std::uint8_t track_filter = all_tracks)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);

		const piano_note* found = nullptr;
		for_each_note_intersecting(tick, tick + 1,
			[&](const piano_note& note)
		{
			if (note.key == key &&
				(track_filter == all_tracks || note.track_index == track_filter) &&
				(!found || *found < note))
				found = &note;
		});
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
		for_each_note_intersecting(tick, tick + 1,
			[&](const piano_note& note)
		{
			if (track_filter != all_tracks && note.track_index != track_filter)
				return;
			if (note.key == key)
			{
				if (!found || out < note)
				{
					out = note;
					found = true;
				}
			}
		});
		if (found || (!tick_tolerance && !key_tolerance))
			return found;

		const auto lo_tick = tick > tick_tolerance ? tick - tick_tolerance : 0;
		const auto hi_tick = tick + tick_tolerance;
		for_each_note_intersecting(lo_tick, hi_tick + 1,
			[&](const piano_note& note)
		{
			if (track_filter != all_tracks && note.track_index != track_filter)
				return;
			if (std::abs(int(note.key) - int(key)) <= int(key_tolerance))
			{
				if (!found || out < note)
				{
					out = note;
					found = true;
				}
			}
		});
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
		for_each_logical_note([&](const piano_note& note)
		{
			populated.insert(note.track_index);
		});
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
		copied.reserve(selected_notes.size());
		for (const auto id : selected_notes)
			if (const auto* note = find_note_by_id(id))
				copied.push_back(*note);
		std::sort(copied.begin(), copied.end());

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
		note_selection new_ids;
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
		note_selection new_ids;
		copies.reserve(selected_notes.size());
		for (const auto id : selected_notes)
		{
			const auto* source = find_note_by_id(id);
			if (!source)
				continue;
			auto copy = *source;
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

	/**
	 * Change the length of the given notes by a common delta (tail drag);
	 * notes squash to a 1-tick minimum instead of vanishing
	 */
	void resize_notes_by(std::vector<std::uint32_t> ids, sgtick_type delta_length)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (ids.empty() || !delta_length)
			return;

		auto op = std::make_unique<resize_notes_op>(std::move(ids), delta_length);
		op->execute(*this);
		push_undo(std::move(op));
	}

	/**
	 * Scale the selected notes in time about the pivot tick (selection scale handle)
	 */
	void stretch_selected_notes(double factor, tick_type pivot)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (selected_notes.empty() || factor <= 0.0 || factor == 1.0)
			return;

		auto op = std::make_unique<stretch_notes_op>(selected_ids_vector(), pivot, factor);
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

	/** Assign the selected notes to one MIDI channel. Returns the changed count. */
	std::size_t change_channel_selected(std::uint8_t channel)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		channel &= 0x0F;
		std::vector<std::uint32_t> ids;
		ids.reserve(selected_notes.size());
		for (const auto id : selected_notes)
			if (const auto* note = find_note_by_id(id); note && note->channel != channel)
				ids.push_back(id);
		if (ids.empty())
			return 0;

		const auto count = ids.size();
		auto op = std::make_unique<channel_change_op>(std::move(ids), channel);
		op->execute(*this);
		push_undo(std::move(op));
		return count;
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
		tool_preview.reset();
		tool_preview_session = false;
	}

private:
	void push_undo(std::unique_ptr<edit_operation> op)
	{
		if (undo_stack.size() >= max_undo_depth)
			undo_stack.erase(undo_stack.begin());
		undo_stack.push_back(std::move(op));
		redo_stack.clear();
	}

	static constexpr std::uint32_t invalid_note_index =
		std::numeric_limits<std::uint32_t>::max();

	void rebuild_base_note_index() const
	{
		base_note_index_by_id.assign(std::size_t(next_note_id), invalid_note_index);
		for (std::size_t index = 0; index < notes.size(); ++index)
		{
			const auto id = notes[index].id;
			if (id >= base_note_index_by_id.size())
				base_note_index_by_id.resize(std::size_t(id) + 1, invalid_note_index);
			base_note_index_by_id[id] = static_cast<std::uint32_t>(index);
		}
	}

	piano_note* find_base_note_by_id(std::uint32_t id)
	{
		if (id >= base_note_index_by_id.size())
			return nullptr;
		const auto index = base_note_index_by_id[id];
		if (index == invalid_note_index || index >= notes.size() || notes[index].id != id)
			return nullptr;
		return &notes[index];
	}

	const piano_note* find_base_note_by_id(std::uint32_t id) const
	{
		return const_cast<midi_editor*>(this)->find_base_note_by_id(id);
	}

	piano_note* find_note_by_id(std::uint32_t id)
	{
		if (auto changed = edited_notes.find(id); changed != edited_notes.end())
			return &changed->second;
		if (suppressed_base_note_ids.count(id))
			return nullptr;
		return find_base_note_by_id(id);
	}

	const piano_note* find_note_by_id(std::uint32_t id) const
	{
		return const_cast<midi_editor*>(this)->find_note_by_id(id);
	}

	void update_cached_bounds(const piano_note& note)
	{
		cached_max_end_tick = std::max(cached_max_end_tick, note.end_tick);
		const auto threshold = tick_type(ppqn) * 16;
		if (note.length() <= threshold)
			max_typical_length = std::max(max_typical_length, note.length());
	}

	// Store a new value for an existing logical note. If it has returned exactly
	// to its loaded value (normally through undo), discard the overlay entry.
	void replace_note(piano_note value)
	{
		const auto id = value.id;
		if (auto* base = find_base_note_by_id(id))
		{
			if (*base == value)
			{
				edited_notes.erase(id);
				suppressed_base_note_ids.erase(id);
				update_cached_bounds(value);
				return;
			}
			suppressed_base_note_ids.insert(id);
		}
		edited_notes[id] = value;
		update_cached_bounds(value);
	}

	void add_note(piano_note value)
	{
		if (find_note_by_id(value.id))
		{
			replace_note(std::move(value));
			return;
		}
		++logical_note_count;
		replace_note(std::move(value));
	}

	bool remove_note_by_id(std::uint32_t id)
	{
		if (!find_note_by_id(id))
			return false;
		edited_notes.erase(id);
		if (find_base_note_by_id(id))
			suppressed_base_note_ids.insert(id);
		else
			suppressed_base_note_ids.erase(id);
		--logical_note_count;
		return true;
	}

	template<typename func_type>
	void for_each_logical_note(func_type&& fn) const
	{
		for (const auto& note : notes)
			if (!suppressed_base_note_ids.count(note.id))
				fn(note);
		for (const auto& [_, note] : edited_notes)
			fn(note);
	}

	std::vector<piano_note> materialize_logical_notes() const
	{
		std::vector<piano_note> result;
		result.reserve(logical_note_count);
		for_each_logical_note([&](const piano_note& note) { result.push_back(note); });
		std::sort(result.begin(), result.end());
		return result;
	}

	std::vector<std::uint32_t> selected_ids_vector() const
	{
		std::vector<std::uint32_t> result;
		result.reserve(selected_notes.size());
		for (const auto id : selected_notes)
			result.push_back(id);
		return result;
	}

	// Drop selected ids whose notes no longer exist (after undo of an insert etc.)
	void prune_selection()
	{
		if (selected_notes.empty())
			return;
		note_selection alive;
		for (const auto id : selected_notes)
			if (find_note_by_id(id))
				alive.insert(id);
		selected_notes.swap(alive);
	}

	void mark_dirty()
	{
		is_dirty = true;
	}

	// For edits that cannot change note geometry (velocity, raw/meta events):
	// the sorted-scan index stays valid, so skip the O(N) re-index
	void mark_dirty_keep_order() { is_dirty = true; }

	/**
	 * Restore the sorted-scan invariants after a geometry edit. Lazy: runs on
	 * the next query, not per edit. Requires editor_mutex to be held.
	 */
	void ensure_note_order() const
	{
		if (!notes_order_dirty)
			return;

		// Edits leave the vector nearly sorted; the is_sorted fast path makes
		// the common "only velocities changed via undo" case O(N)
		if (!std::is_sorted(notes.begin(), notes.end()))
		{
			std::sort(notes.begin(), notes.end());
			rebuild_base_note_index();
		}

		// Notes longer than the threshold go to a side list so that one long
		// pad does not widen every scan window. If long notes are not rare in
		// this file, fold them back in and accept the wider window instead of
		// scanning a huge side list on every query.
		const auto threshold = tick_type(ppqn) * 16;
		long_note_indices.clear();
		max_typical_length = 0;
		cached_max_end_tick = 0;
		for (std::size_t i = 0; i < notes.size(); ++i)
		{
			const auto length = notes[i].length();
			cached_max_end_tick = std::max(cached_max_end_tick, notes[i].end_tick);
			if (length > threshold)
				long_note_indices.push_back(i);
			else
				max_typical_length = std::max(max_typical_length, length);
		}
		if (long_note_indices.size() > notes.size() / 16 + 64)
		{
			long_note_indices.clear();
			for (const auto& note : notes)
				max_typical_length = std::max(max_typical_length, note.length());
		}

		notes_order_dirty = false;
	}

	/**
	 * Visit every note intersecting [start, end), in draw order (long outliers
	 * first — they started earlier — then start-tick order). Cost is
	 * O(log N + notes near the range), not O(N). Requires editor_mutex.
	 */
	template<typename func_type>
	void for_each_note_intersecting(tick_type start, tick_type end, func_type&& fn) const
	{
		ensure_note_order();

		const auto scan_from = start > max_typical_length ? start - max_typical_length : 0;

		// Outliers starting inside the scan window are reported by the main
		// loop below, so only take the ones starting before it
		for (const auto index : long_note_indices)
		{
			const auto& note = notes[index];
			if (note.start_tick >= end)
				break; // side list is start-sorted too
			if (!suppressed_base_note_ids.count(note.id) &&
				note.start_tick < scan_from && note.end_tick > start)
				fn(note);
		}

		auto it = std::lower_bound(notes.begin(), notes.end(), scan_from,
			[](const piano_note& note, tick_type t) { return note.start_tick < t; });
		for (; it != notes.end() && it->start_tick < end; ++it)
		{
			if (!suppressed_base_note_ids.count(it->id) && it->end_tick > start)
				fn(*it);
		}

		// Interactive edits are sparse and deliberately kept out of the giant
		// sorted base vector. Scanning this overlay is proportional to edits in
		// the current session, not to notes in the source file.
		for (const auto& [_, note] : edited_notes)
			if (note.start_tick < end && note.end_tick > start)
				fn(note);
	}

	static std::uint16_t clamp_control_value(control_lane lane, std::uint16_t value)
	{
		return std::min<std::uint16_t>(value,
			lane == control_lane::pitch_bend ? 0x3FFF : 127);
	}

	static bool is_tempo_event(const raw_event& event)
	{
		return event.bytes.size() >= 6 && event.bytes[0] == 0xFF &&
			event.bytes[1] == 0x51 && event.bytes[2] == 0x03;
	}

	static std::vector<base_type> make_tempo_event_bytes(std::uint32_t tempo_us)
	{
		tempo_us = std::clamp<std::uint32_t>(tempo_us, 1, 0xFFFFFF);
		return {0xFF, 0x51, 0x03, base_type(tempo_us >> 16),
			base_type(tempo_us >> 8), base_type(tempo_us)};
	}

	std::vector<std::pair<std::uint8_t, raw_event>> remove_tempo_events_at(tick_type tick)
	{
		std::vector<std::pair<std::uint8_t, raw_event>> removed;
		for (auto& [track, events] : raw_track_events)
		{
			events.erase(std::remove_if(events.begin(), events.end(), [&](const raw_event& event)
			{
				if (event.tick != tick || !is_tempo_event(event))
					return false;
				removed.push_back({track, event});
				return true;
			}), events.end());
		}
		return removed;
	}

	void rebuild_tempo_events()
	{
		tempo_events.clear();
		for (const auto& [_, events] : raw_track_events)
			for (const auto& event : events)
				if (is_tempo_event(event))
				{
					const auto us = (std::uint32_t(event.bytes[3]) << 16) |
						(std::uint32_t(event.bytes[4]) << 8) | event.bytes[5];
					tempo_events.push_back({event.tick, us});
				}
		std::stable_sort(tempo_events.begin(), tempo_events.end());
	}

	static std::uint8_t control_cc_number(control_lane lane)
	{
		return lane == control_lane::pan ? 10 : 7;
	}

	static std::vector<base_type> make_control_event_bytes(control_lane lane,
		std::uint8_t channel, std::uint16_t value)
	{
		value = clamp_control_value(lane, value);
		if (lane == control_lane::pitch_bend)
			return {
				base_type(0xE0 | (channel & 0x0F)),
				base_type(value & 0x7F),
				base_type((value >> 7) & 0x7F)
			};

		return {
			base_type(0xB0 | (channel & 0x0F)),
			control_cc_number(lane),
			base_type(value & 0x7F)
		};
	}

	static std::vector<base_type> make_track_name_event_bytes(const std::string& name)
	{
		std::vector<base_type> bytes{0xFF, 0x03};
		single_midi_processor_2::push_vlv(std::uint32_t(name.size()), bytes);
		bytes.insert(bytes.end(), name.begin(), name.end());
		return bytes;
	}

	static bool is_track_name_event(const raw_event& ev)
	{
		return ev.bytes.size() >= 3 && ev.bytes[0] == 0xFF && ev.bytes[1] == 0x03;
	}

	static bool decode_control_event(const raw_event& ev, control_lane lane,
		std::uint8_t channel, std::uint16_t& value)
	{
		if (ev.bytes.size() < 3)
			return false;

		const auto status = ev.bytes[0];
		if ((status & 0x0F) != (channel & 0x0F))
			return false;

		if (lane == control_lane::pitch_bend)
		{
			if ((status & 0xF0) != 0xE0)
				return false;
			value = std::uint16_t(ev.bytes[1] & 0x7F) |
				(std::uint16_t(ev.bytes[2] & 0x7F) << 7);
			return true;
		}

		if ((status & 0xF0) != 0xB0 || ev.bytes[1] != control_cc_number(lane))
			return false;

		value = ev.bytes[2] & 0x7F;
		return true;
	}

	std::optional<raw_event> remove_control_event_at(std::uint8_t track,
		control_lane lane, std::uint8_t channel, tick_type tick)
	{
		auto it = raw_track_events.find(track);
		if (it == raw_track_events.end())
			return std::nullopt;

		auto& events = it->second;
		for (auto ev_it = events.begin(); ev_it != events.end(); ++ev_it)
		{
			std::uint16_t ignored = 0;
			if (ev_it->tick == tick && decode_control_event(*ev_it, lane, channel, ignored))
			{
				auto old = *ev_it;
				events.erase(ev_it);
				return old;
			}
		}
		return std::nullopt;
	}

	void sort_raw_track(std::uint8_t track)
	{
		auto it = raw_track_events.find(track);
		if (it == raw_track_events.end())
			return;

		std::stable_sort(it->second.begin(), it->second.end(),
			[](const raw_event& a, const raw_event& b)
		{
			return a.tick < b.tick;
		});
	}

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

		for_each_note_intersecting(begin, end, [&](const piano_note& note)
		{
			if (track_filter != all_tracks && note.track_index != track_filter)
				return;
			if (note.key < key_begin || note.key > key_end)
				return;

			if (mode == select_mode::remove)
				selected_notes.erase(note.id);
			else
				selected_notes.insert(note.id);
		});
		selected_notes.compact();
		return selected_notes.size();
	}

	/** Select notes on a channel, optionally limited to one track. */
	std::size_t select_channel(std::uint8_t channel,
		std::uint8_t track_filter = all_tracks,
		select_mode mode = select_mode::replace)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (mode == select_mode::replace)
			selected_notes.clear();
		channel &= 0x0F;

		for_each_logical_note([&](const piano_note& note)
		{
			if (note.channel != channel ||
				(track_filter != all_tracks && note.track_index != track_filter))
				return;
			if (mode == select_mode::remove)
				selected_notes.erase(note.id);
			else
				selected_notes.insert(note.id);
		});
		selected_notes.compact();
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
		selected_notes.clear();
		for (const auto id : ids)
			selected_notes.insert(id);
	}

	std::set<std::uint32_t> get_selected_ids() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		const auto ids = selected_ids_vector();
		return std::set<std::uint32_t>(ids.begin(), ids.end());
	}

	// Rendering only needs membership for notes in the viewport. Returning this
	// compact subset avoids copying a massive score-wide selection every frame.
	std::set<std::uint32_t> get_selected_ids_in_range(tick_type begin, tick_type end,
		std::uint8_t key_low = 0, std::uint8_t key_high = 127) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::set<std::uint32_t> result;
		for_each_note_intersecting(begin, end, [&](const piano_note& note)
		{
			if (note.key >= key_low && note.key <= key_high && selected_notes.count(note.id))
				result.insert(note.id);
		});
		return result;
	}

	std::vector<piano_note> get_selected_notes() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<piano_note> result;
		result.reserve(selected_notes.size());
		for (const auto id : selected_notes)
			if (const auto* note = find_note_by_id(id))
				result.push_back(*note);
		std::sort(result.begin(), result.end());
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
		for (const auto id : selected_notes)
		{
			const auto* note = find_note_by_id(id);
			if (!note)
				continue;
			if (!any)
			{
				begin = note->start_tick;
				end = note->end_tick;
				key_low = key_high = note->key;
				any = true;
			}
			else
			{
				begin = std::min(begin, note->start_tick);
				end = std::max(end, note->end_tick);
				key_low = std::min(key_low, note->key);
				key_high = std::max(key_high, note->key);
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

	void set_track_name(std::uint8_t track, const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		auto& info = tracks[track];
		info.name = name;

		auto& events = raw_track_events[track];
		const auto bytes = make_track_name_event_bytes(name);
		bool replaced = false;
		for (auto& ev : events)
		{
			if (is_track_name_event(ev))
			{
				ev.tick = 0;
				ev.bytes = bytes;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			events.push_back({0, bytes});
		sort_raw_track(track);
		mark_dirty_keep_order();
	}

	std::vector<channel_control_point> get_channel_control_points(
		std::uint8_t track, std::uint8_t channel, control_lane lane,
		tick_type start, tick_type end) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<channel_control_point> result;

		auto it = raw_track_events.find(track);
		if (it == raw_track_events.end())
			return result;

		for (const auto& ev : it->second)
		{
			if (ev.tick < start || ev.tick >= end)
				continue;

			std::uint16_t value = 0;
			if (decode_control_event(ev, lane, channel, value))
				result.push_back({ev.tick, std::uint8_t(channel & 0x0F), value});
		}
		return result;
	}

	void set_channel_control_point(std::uint8_t track, std::uint8_t channel,
		control_lane lane, tick_type tick, std::uint16_t value)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		auto op = std::make_unique<raw_control_change_op>(track, lane,
			std::uint8_t(channel & 0x0F), tick, clamp_control_value(lane, value));
		op->execute(*this);
		push_undo(std::move(op));
	}

	void set_channel_control_points(std::uint8_t track, std::uint8_t channel,
		control_lane lane, std::vector<std::pair<tick_type, std::uint16_t>> points)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (points.empty())
			return;

		for (auto& [_, value] : points)
			value = clamp_control_value(lane, value);

		std::sort(points.begin(), points.end(),
			[](const auto& a, const auto& b)
		{
			return a.first < b.first;
		});
		points.erase(std::unique(points.begin(), points.end(),
			[](const auto& a, const auto& b)
		{
			return a.first == b.first;
		}), points.end());

		auto op = std::make_unique<raw_control_change_batch_op>(track, lane,
			std::uint8_t(channel & 0x0F), std::move(points));
		op->execute(*this);
		push_undo(std::move(op));
	}

	std::vector<std::pair<tick_type, double>> get_tempo_points(
		tick_type start, tick_type end) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		std::vector<std::pair<tick_type, double>> result;
		for (const auto& [tick, tempo_us] : tempo_events)
			if (tick >= start && tick < end && tempo_us)
				result.push_back({tick, 60000000.0 / double(tempo_us)});
		return result;
	}

	void set_tempo_point(tick_type tick, double bpm)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		bpm = std::clamp(bpm, 60000000.0 / 16777215.0, 60000000.0);
		const auto tempo_us = std::uint32_t(std::lround(60000000.0 / bpm));
		auto op = std::make_unique<tempo_change_op>(tick, tempo_us);
		op->execute(*this);
		push_undo(std::move(op));
	}

	void set_tempo_points(std::vector<std::pair<tick_type, double>> points)
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (points.empty())
			return;
		std::sort(points.begin(), points.end());
		points.erase(std::unique(points.begin(), points.end(),
			[](const auto& a, const auto& b) { return a.first == b.first; }), points.end());
		std::vector<std::pair<tick_type, std::uint32_t>> encoded;
		encoded.reserve(points.size());
		for (const auto& [tick, bpm_value] : points)
		{
			const double bpm = std::clamp(bpm_value, 60000000.0 / 16777215.0, 60000000.0);
			encoded.push_back({tick, std::uint32_t(std::lround(60000000.0 / bpm))});
		}
		auto op = std::make_unique<tempo_batch_op>(std::move(encoded));
		op->execute(*this);
		push_undo(std::move(op));
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

	/**
	 * Pitch-bend sensitivity for a channel, in semitones, read from the file's
	 * RPN 00,00 -> Data Entry (CC6 = whole semitones, optional CC38 = cents).
	 * Needed to convert a raw 14-bit bend into musical semitones/cents, since the
	 * same wheel value means different pitch under a different range. Defaults to
	 * the GM default of 2.0 when the file never sets it; if the range is set more
	 * than once, the latest (highest-tick) value wins. RPN selection is tracked
	 * per track, matching how selection + data entry are authored together.
	 */
	double get_pitch_bend_range_semitones(std::uint8_t channel) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		channel &= 0x0F;

		double range = 2.0;
		tick_type best_tick = 0;
		bool found = false;

		for (const auto& [track, events] : raw_track_events)
		{
			std::uint8_t sel_msb = 0x7F, sel_lsb = 0x7F, data_msb = 0, data_lsb = 0;
			bool is_nrpn = false;
			for (const auto& ev : events)
			{
				if (ev.bytes.size() < 3 || (ev.bytes[0] & 0xF0) != 0xB0 ||
					(ev.bytes[0] & 0x0F) != channel)
					continue;

				const std::uint8_t cc = ev.bytes[1], val = ev.bytes[2];
				switch (cc)
				{
					case 0x65: sel_msb = val; is_nrpn = false; break; // RPN MSB
					case 0x64: sel_lsb = val; is_nrpn = false; break; // RPN LSB
					case 0x63: sel_msb = val; is_nrpn = true;  break; // NRPN MSB
					case 0x62: sel_lsb = val; is_nrpn = true;  break; // NRPN LSB
					case 0x06: case 0x26: // Data Entry MSB / LSB
						if (!is_nrpn && sel_msb == 0 && sel_lsb == 0) // RPN 00,00 = pitch-bend range
						{
							if (cc == 0x06) data_msb = val;
							else            data_lsb = val;
							if (!found || ev.tick >= best_tick)
							{
								best_tick = ev.tick;
								range = double(data_msb) + double(data_lsb) / 100.0;
								found = true;
							}
						}
						break;
					default: break;
				}
			}
		}
		return range;
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
		for_each_logical_note([&](const piano_note& note)
		{
			notes_by_track[note.track_index].push_back(note);
		});

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

	std::vector<piano_note> get_all_notes() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return materialize_logical_notes();
	}
	size_t get_note_count() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return logical_note_count;
	}

	std::uint16_t get_ppqn() const { return ppqn; }
	tick_type get_ticks_per_beat() const { return ticks_per_beat; }

	tick_type get_total_ticks() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		ensure_note_order();
		auto total = cached_max_end_tick;
		for (const auto& [_, note] : edited_notes)
			total = std::max(total, note.end_tick);
		return total;
	}

	double get_total_seconds() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return seconds_at_tick_unlocked(get_total_ticks());
	}

	double get_seconds_at_tick(tick_type target_tick) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		return seconds_at_tick_unlocked(target_tick);
	}

	// Inverse of get_seconds_at_tick: walks the same tempo map
	tick_type get_tick_at_seconds(double target_seconds) const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);
		if (target_seconds <= 0.0 || !ppqn)
			return 0;

		constexpr double default_tempo_us = 500000.0; // 120 BPM
		const double us_per_tick_divisor = double(ppqn) * 1000000.0;

		double seconds = 0;
		tick_type last_tick = 0;
		double current_tempo_us = default_tempo_us;

		// tempo_events is sorted on load
		for (const auto& [tick, tempo] : tempo_events)
		{
			const double segment = double(tick - last_tick) * current_tempo_us / us_per_tick_divisor;
			if (seconds + segment >= target_seconds)
				break;
			seconds += segment;
			last_tick = tick;
			current_tempo_us = double(tempo);
		}
		return last_tick + tick_type((target_seconds - seconds) * us_per_tick_divisor / current_tempo_us);
	}

private:
	double seconds_at_tick_unlocked(tick_type target_tick) const
	{
		constexpr double default_tempo_us = 500000.0; // 120 BPM
		const auto total = target_tick;
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

public:

	const std::map<std::uint8_t, track_info>& get_tracks() const { return tracks; }
	const std::wstring& get_filename() const { return filename; }

	// ========================================================================
	// Live Playback Source (stream editor state into simple_player without
	// dumping to disk — see playback_event_source.h)
	// ========================================================================

	/**
	 * Time-ordered event source over a *snapshot* of the editor's notes, raw
	 * controller events and tempo map, taken under editor_mutex so playback is
	 * isolated from later edits. Produces the same stream a saved file would
	 * (note-on/off pairs + controller/program/pitch events), merged lazily:
	 * O(polyphony) extra state, no full second copy of the event list.
	 */
	struct editor_event_source : playback_event_source
	{
		// A channel-voice controller/program/pitch event, ready to send.
		struct control_ev
		{
			tick_type tick;
			std::uint32_t short_msg;
		};

		// Pending note-off, awaiting its turn in the end_tick-ordered min-heap.
		struct off_entry
		{
			tick_type end_tick;
			std::uint8_t key;
			std::uint8_t channel;
			std::uint16_t track_index;
			// std::priority_queue is a max-heap; invert so top() is the earliest end.
			bool operator<(const off_entry& o) const { return end_tick > o.end_tick; }
		};

		editor_event_source(std::vector<piano_note>&& notes,
			std::vector<control_ev>&& controls,
			std::vector<std::pair<tick_type, std::uint32_t>>&& tempo,
			std::uint16_t ppqn)
			: notes_(std::move(notes)), controls_(std::move(controls)),
			tempo_(std::move(tempo)), ppqn_(ppqn ? ppqn : 480), total_us_(0)
		{
			std::sort(notes_.begin(), notes_.end());
			std::stable_sort(controls_.begin(), controls_.end(),
				[](const control_ev& a, const control_ev& b) { return a.tick < b.tick; });

			// Total duration: latest note end or controller tick, in microseconds
			tick_type max_tick = 0;
			for (const auto& note : notes_)
				max_tick = std::max(max_tick, note.end_tick);
			if (!controls_.empty())
				max_tick = std::max(max_tick, controls_.back().tick);
			total_us_ = us_at_tick_scan(max_tick);

			rewind();
		}

		std::uint64_t total_duration_us() const override { return total_us_; }

		void rewind() override
		{
			on_idx_ = 0;
			raw_idx_ = 0;
			off_heap_ = {};
			tempo_idx_ = 0;
			seg_tick_ = 0;
			seg_us_ = 0;
			cur_tempo_ = 500000; // 120 BPM until the first tempo event
			seek_state_.clear();
			seek_state_idx_ = 0;
			seek_held_notes_.clear();
			seek_held_idx_ = 0;
			seek_anchor_pending_ = false;
			seek_target_us_ = 0;
			seek_target_tick_ = 0;
		}

		void seek(std::uint64_t target_us) override
		{
			rewind();

			// First tick whose time is at/after the target (ceiling). Positioning
			// here means every event next() yields has time >= target_us, so the
			// parser's fast-forward completes on the very first one — no note is
			// ever pulled-and-skipped, even if the boundary tick holds millions.
			tick_type cross_tick = tick_at_us(target_us);
			if (us_at_tick_scan(cross_tick) < target_us)
				++cross_tick;
			seek_target_tick_ = cross_tick;

			on_idx_ = std::size_t(std::lower_bound(notes_.begin(), notes_.end(), cross_tick,
				[](const piano_note& note, tick_type t) { return note.start_tick < t; })
				- notes_.begin());

			// Notes crossing the boundary must be re-struck. Without this, a view
			// beginning inside a long chord can be silent until the next note-on (or
			// forever). Only active polyphony is copied into this side buffer.
			for (std::size_t i = 0; i < on_idx_; ++i)
				if (notes_[i].end_tick > cross_tick)
					seek_held_notes_.push_back(notes_[i]);
			seek_held_idx_ = 0;
			seek_target_us_ = target_us;
			seek_anchor_pending_ = true;

			const std::size_t raw_target = std::size_t(std::lower_bound(
				controls_.begin(), controls_.end(), cross_tick,
				[](const control_ev& c, tick_type t) { return c.tick < t; })
				- controls_.begin());
			raw_idx_ = raw_target;

			// Reconstruct channel state at the seek point, emitted first by next()
			// during fast-forward. Same final synth state as replaying every event
			// below the target, without touching the dense note prefix; held notes
			// are not re-struck.
			//
			// Absolute controllers (volume, pan, pitch bend, program, pressure,
			// aftertouch) collapse to their last value per channel/lane. RPN/NRPN
			// is different: Data Entry (CC6/38) has no meaning on its own — it acts
			// on the parameter currently selected by CC101/100 (RPN) or CC99/98
			// (NRPN). Latching those CC numbers independently and re-sorting by time
			// scrambles the select->data-entry order, so e.g. this file's ±12
			// semitone pitch-bend range (RPN 00,00 + CC6=12) never gets established
			// and every bend plays at the default ±2. Instead we track the selected
			// parameter as we scan, remember the last Data Entry written to *each*
			// parameter, and re-emit a correctly ordered block per parameter.
			auto is_rpn_cc = [](std::uint8_t cc)
			{
				return cc == 0x06 || cc == 0x26 || cc == 0x60 || cc == 0x61 ||
					cc == 0x62 || cc == 0x63 || cc == 0x64 || cc == 0x65;
			};

			struct rpn_param
			{
				std::uint8_t msb = 0, lsb = 0, data_msb = 0, data_lsb = 0;
				bool is_nrpn = false, has_msb = false, has_lsb = false;
				tick_type tick = 0;
			};
			struct chan_rpn
			{
				std::uint8_t sel_msb = 0x7F, sel_lsb = 0x7F; // RPN null until selected
				bool is_nrpn = false, had_selection = false;
				tick_type sel_tick = 0;
				std::unordered_map<std::uint32_t, rpn_param> params; // is_nrpn<<16 | msb<<8 | lsb
			};
			chan_rpn rpn[16];

			std::unordered_map<std::uint32_t, control_ev> latest;
			for (std::size_t i = 0; i < raw_target; ++i)
			{
				const std::uint32_t msg = controls_[i].short_msg;
				const std::uint8_t status = std::uint8_t(msg & 0xFF);
				const std::uint8_t d1 = std::uint8_t((msg >> 8) & 0xFF);
				const std::uint8_t d2 = std::uint8_t((msg >> 16) & 0xFF);
				const std::uint8_t hi = status & 0xF0;
				const std::uint8_t ch = status & 0x0F;

				if (hi == 0xB0 && is_rpn_cc(d1))
				{
					chan_rpn& cs = rpn[ch];
					switch (d1)
					{
						case 0x65: cs.sel_msb = d2; cs.is_nrpn = false; cs.had_selection = true; cs.sel_tick = controls_[i].tick; break; // RPN MSB
						case 0x64: cs.sel_lsb = d2; cs.is_nrpn = false; cs.had_selection = true; cs.sel_tick = controls_[i].tick; break; // RPN LSB
						case 0x63: cs.sel_msb = d2; cs.is_nrpn = true;  cs.had_selection = true; cs.sel_tick = controls_[i].tick; break; // NRPN MSB
						case 0x62: cs.sel_lsb = d2; cs.is_nrpn = true;  cs.had_selection = true; cs.sel_tick = controls_[i].tick; break; // NRPN LSB
						case 0x06: case 0x26: // Data Entry MSB / LSB -> current parameter
						{
							const std::uint32_t key = (std::uint32_t(cs.is_nrpn ? 1 : 0) << 16) |
								(std::uint32_t(cs.sel_msb) << 8) | cs.sel_lsb;
							rpn_param& p = cs.params[key];
							p.msb = cs.sel_msb; p.lsb = cs.sel_lsb; p.is_nrpn = cs.is_nrpn;
							p.tick = controls_[i].tick;
							if (d1 == 0x06) { p.data_msb = d2; p.has_msb = true; }
							else            { p.data_lsb = d2; p.has_lsb = true; }
							break;
						}
						// Data Increment/Decrement (0x60/0x61) are relative to the
						// synth's internal value and can't be reconstructed; skipped.
						default: break;
					}
					continue;
				}

				// Absolute controller/program/pitch/pressure: last value per lane.
				// Include the data byte for per-key/per-CC lanes (poly aftertouch, CC);
				// program/pressure/pitch are one value per channel.
				std::uint32_t id = std::uint32_t(status) << 8;
				if (hi == 0xA0 || hi == 0xB0)
					id |= d1;
				latest[id] = controls_[i];
			}

			seek_state_.reserve(latest.size() + 64);
			for (const auto& [id, ev] : latest)
			{
				generated_event ge;
				ge.time_us = us_at_tick_scan(ev.tick);
				ge.tick = ev.tick;
				ge.short_msg = ev.short_msg;
				ge.k = generated_event::kind::control;
				seek_state_.push_back(ge);
			}

			// Correctly ordered RPN/NRPN block per channel: select the parameter,
			// then its Data Entry. Finally restore whichever parameter the channel
			// had selected at the seek point, so any Data Entry that arrives after
			// the seek acts on the right parameter. stable_sort keeps this order for
			// events sharing a tick (select before data entry).
			auto push_cc = [&](std::uint8_t ch, std::uint8_t cc, std::uint8_t val, tick_type tick)
			{
				generated_event ge;
				ge.time_us = us_at_tick_scan(tick);
				ge.tick = tick;
				ge.short_msg = smsg(0xB0 | (ch & 0x0F), cc, val);
				ge.k = generated_event::kind::control;
				seek_state_.push_back(ge);
			};
			for (std::uint8_t ch = 0; ch < 16; ++ch)
			{
				chan_rpn& cs = rpn[ch];
				for (const auto& [key, p] : cs.params)
				{
					if (p.is_nrpn) { push_cc(ch, 0x63, p.msb, p.tick); push_cc(ch, 0x62, p.lsb, p.tick); }
					else           { push_cc(ch, 0x65, p.msb, p.tick); push_cc(ch, 0x64, p.lsb, p.tick); }
					if (p.has_msb) push_cc(ch, 0x06, p.data_msb, p.tick);
					if (p.has_lsb) push_cc(ch, 0x26, p.data_lsb, p.tick);
				}
				if (cs.had_selection)
				{
					if (cs.is_nrpn) { push_cc(ch, 0x63, cs.sel_msb, cs.sel_tick); push_cc(ch, 0x62, cs.sel_lsb, cs.sel_tick); }
					else            { push_cc(ch, 0x65, cs.sel_msb, cs.sel_tick); push_cc(ch, 0x64, cs.sel_lsb, cs.sel_tick); }
				}
			}

			std::stable_sort(seek_state_.begin(), seek_state_.end(),
				[](const generated_event& a, const generated_event& b) { return a.time_us < b.time_us; });

			// These are a reconstructed snapshot, not historical playback events.
			// Keeping their original pre-seek times lets the parser cross the seek
			// boundary while some are still buffered; the timed sender then computes
			// old_time - seek_offset as uint64_t and waits almost forever. Preserve
			// the established ordering, but apply the whole snapshot at the boundary.
			for (auto& event : seek_state_)
				event.time_us = target_us;
		}

		bool next(generated_event& out) override
		{
			// Emit reconstructed channel state first (fast-forward drains it),
			// then the merged stream, which seek() positioned at/after the target.
			if (seek_state_idx_ < seek_state_.size())
			{
				out = seek_state_[seek_state_idx_++];
				return true;
			}

			// Always provide an event at the requested time. It closes fast-forward
			// cleanly when the view starts after the last real event; short_msg == 0
			// makes it a timing marker only.
			if (seek_anchor_pending_)
			{
				seek_anchor_pending_ = false;
				out = generated_event{};
				out.time_us = seek_target_us_;
				out.tick = seek_target_tick_;
				return true;
			}

			if (seek_held_idx_ < seek_held_notes_.size())
			{
				const auto& note = seek_held_notes_[seek_held_idx_++];
				const std::uint8_t vel = std::max<std::uint8_t>(note.velocity, 1);
				out.time_us = seek_target_us_;
				out.tick = note.start_tick;
				out.short_msg = smsg(0x90 | (note.channel & 0x0F), note.key, vel);
				out.k = generated_event::kind::note_on;
				out.key = note.key;
				out.velocity = vel;
				out.channel = note.channel;
				out.track_index = note.track_index;
				off_heap_.push({note.end_tick, note.key, note.channel, note.track_index});
				return true;
			}

			constexpr tick_type inf = ~tick_type(0);

			const tick_type on_tick = on_idx_ < notes_.size() ? notes_[on_idx_].start_tick : inf;
			const tick_type off_tick = off_heap_.empty() ? inf : off_heap_.top().end_tick;
			const tick_type raw_tick = raw_idx_ < controls_.size() ? controls_[raw_idx_].tick : inf;

			if (on_tick == inf && off_tick == inf && raw_tick == inf)
				return false;

			// Tie-break at equal ticks: note-off, then controller, then note-on
			// (matches write_midi_from_notes ordering so playback == the saved file).
			tick_type tick;
			enum { pick_off, pick_raw, pick_on } which;
			if (off_tick <= raw_tick && off_tick <= on_tick) { which = pick_off; tick = off_tick; }
			else if (raw_tick <= on_tick)                    { which = pick_raw; tick = raw_tick; }
			else                                             { which = pick_on;  tick = on_tick; }

			const std::uint64_t time_us = advance_us_to(tick);

			if (which == pick_off)
			{
				const auto entry = off_heap_.top();
				off_heap_.pop();
				out.time_us = time_us;
				out.tick = entry.end_tick;
				out.short_msg = smsg(0x80 | (entry.channel & 0x0F), entry.key, 0x40);
				out.k = generated_event::kind::note_off;
				out.key = entry.key;
				out.velocity = 0;
				out.channel = entry.channel;
				out.track_index = entry.track_index;
			}
			else if (which == pick_raw)
			{
				out.time_us = time_us;
				out.tick = tick;
				out.short_msg = controls_[raw_idx_].short_msg;
				out.k = generated_event::kind::control;
				out.key = out.velocity = out.channel = 0;
				out.track_index = 0;
				++raw_idx_;
			}
			else // pick_on
			{
				const auto& note = notes_[on_idx_];
				const std::uint8_t vel = std::max<std::uint8_t>(note.velocity, 1);
				out.time_us = time_us;
				out.tick = note.start_tick;
				out.short_msg = smsg(0x90 | (note.channel & 0x0F), note.key, vel);
				out.k = generated_event::kind::note_on;
				out.key = note.key;
				out.velocity = vel;
				out.channel = note.channel;
				out.track_index = note.track_index;
				off_heap_.push({note.end_tick, note.key, note.channel, note.track_index});
				++on_idx_;
			}

			return true;
		}

	private:
		static std::uint32_t smsg(std::uint8_t status, std::uint8_t d1, std::uint8_t d2 = 0)
		{
			return std::uint32_t(status) | (std::uint32_t(d1) << 8) | (std::uint32_t(d2) << 16);
		}

		// Overflow-safe (delta_ticks * tempo) / ppqn. Forming the full product
		// wraps uint64 once a single tempo span covers a large tick range (common:
		// most files have one tempo, so the span is the whole file). Split into
		// whole quarters + remainder so no intermediate exceeds the real duration.
		static std::uint64_t ticks_to_us(std::uint64_t delta_ticks,
			std::uint32_t tempo, std::uint16_t ppqn)
		{
			return (delta_ticks / ppqn) * tempo + (delta_ticks % ppqn) * tempo / ppqn;
		}

		// Inverse, equally overflow-safe: (delta_us * ppqn) / tempo.
		static tick_type us_to_ticks(std::uint64_t delta_us,
			std::uint32_t tempo, std::uint16_t ppqn)
		{
			return (delta_us / tempo) * ppqn + (delta_us % tempo) * ppqn / tempo;
		}

		// Monotonic tick->us walker; ticks arrive non-decreasing across next() calls.
		std::uint64_t advance_us_to(tick_type tick)
		{
			while (tempo_idx_ < tempo_.size() && tempo_[tempo_idx_].first <= tick)
			{
				const auto& [change_tick, tempo] = tempo_[tempo_idx_];
				seg_us_ += ticks_to_us(change_tick - seg_tick_, cur_tempo_, ppqn_);
				seg_tick_ = change_tick;
				cur_tempo_ = tempo;
				++tempo_idx_;
			}
			return seg_us_ + ticks_to_us(tick - seg_tick_, cur_tempo_, ppqn_);
		}

		// Inverse of the tempo walk: the tick whose time is target_us. Used by
		// seek() to convert the parser's target time back into a note cursor.
		tick_type tick_at_us(std::uint64_t target_us) const
		{
			std::uint64_t us = 0;
			tick_type last = 0;
			std::uint32_t tempo = 500000;
			for (const auto& [tick, tempo_value] : tempo_)
			{
				const std::uint64_t seg = ticks_to_us(tick - last, tempo, ppqn_);
				if (us + seg >= target_us)
					break;
				us += seg;
				last = tick;
				tempo = tempo_value;
			}
			return last + us_to_ticks(target_us - us, tempo, ppqn_);
		}

		// One-shot scan for total duration (independent of the streaming walker).
		std::uint64_t us_at_tick_scan(tick_type target) const
		{
			std::uint64_t us = 0;
			tick_type last = 0;
			std::uint32_t tempo = 500000;
			for (const auto& [tick, tempo_value] : tempo_)
			{
				if (tick >= target)
					break;
				us += ticks_to_us(tick - last, tempo, ppqn_);
				last = tick;
				tempo = tempo_value;
			}
			us += ticks_to_us(target - last, tempo, ppqn_);
			return us;
		}

		std::vector<piano_note> notes_;    // sorted by start_tick
		std::vector<control_ev> controls_; // sorted by tick
		std::vector<std::pair<tick_type, std::uint32_t>> tempo_; // (tick, us/qn), sorted
		std::uint16_t ppqn_;
		std::uint64_t total_us_;

		// Iteration state (reset by rewind)
		std::size_t on_idx_ = 0;
		std::size_t raw_idx_ = 0;
		std::priority_queue<off_entry> off_heap_;

		// Reconstructed channel state emitted first after a seek (bounded set)
		std::vector<generated_event> seek_state_;
		std::size_t seek_state_idx_ = 0;
		std::vector<piano_note> seek_held_notes_;
		std::size_t seek_held_idx_ = 0;
		std::uint64_t seek_target_us_ = 0;
		tick_type seek_target_tick_ = 0;
		bool seek_anchor_pending_ = false;

		// Monotonic tick->us walker state
		std::size_t tempo_idx_ = 0;
		tick_type seg_tick_ = 0;
		std::uint64_t seg_us_ = 0;
		std::uint32_t cur_tempo_ = 500000;
	};

	/**
	 * Build a live playback source over a snapshot of the current notes, tempo
	 * map and channel controllers. simple_player::run_from_external streams it
	 * directly — nothing is written to disk. The snapshot is copied under lock,
	 * so later edits do not disturb an in-flight playback.
	 */
	std::unique_ptr<playback_event_source> make_playback_source() const
	{
		std::lock_guard<std::recursive_mutex> lock(editor_mutex);

		auto notes_copy = materialize_logical_notes();

		std::vector<editor_event_source::control_ev> controls;
		for (const auto& [track, events] : raw_track_events)
		{
			for (const auto& ev : events)
			{
				if (ev.bytes.empty())
					continue;

				const std::uint8_t status = ev.bytes[0];
				if (status >= 0xF0) // meta/sysex: not a sendable short message
					continue;

				const std::uint8_t hi = status & 0xF0;
				std::uint32_t msg = status;
				if (hi == 0xC0 || hi == 0xD0) // program change / channel pressure: 2 bytes
				{
					if (ev.bytes.size() >= 2)
						msg |= std::uint32_t(ev.bytes[1]) << 8;
					else
						continue;
				}
				else // CC / pitch bend / aftertouch: 3 bytes
				{
					if (ev.bytes.size() >= 3)
						msg |= (std::uint32_t(ev.bytes[1]) << 8) | (std::uint32_t(ev.bytes[2]) << 16);
					else
						continue;
				}
				controls.push_back({ev.tick, msg});
			}
		}

		auto tempo_copy = tempo_events;

		return std::make_unique<editor_event_source>(
			std::move(notes_copy), std::move(controls), std::move(tempo_copy), ppqn);
	}
};

#endif // SAFC_MIDI_EDITOR
