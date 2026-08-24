#define NOMINMAX
#include <Windows.h>

#include "midi_editor.h"

std::vector<midi_editor::piano_note> midi_editor::get_tool_target_notes() const
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	std::vector<piano_note> result;
	if (!selected_notes.empty())
	{
		result.reserve(selected_notes.size());
		for (const auto id : selected_notes)
			if (const auto* note = find_note_by_id(id))
				result.push_back(*note);
	}
	else
	{
		for_each_logical_note([&](const piano_note& note)
		{
			if (note.track_index == active_track)
				result.push_back(note);
		});
	}
	std::sort(result.begin(), result.end());
	return result;
}

void midi_editor::prepare_tool_preview(bool preview)
{
	if (tool_preview)
	{
		tool_preview->undo(*this);
		tool_preview.reset();
		selected_notes = tool_preview_original_selection;
	}
	if (preview && !tool_preview_session)
	{
		tool_preview_session = true;
		tool_preview_was_dirty = is_dirty.load();
		tool_preview_original_selection = selected_notes;
	}
	else if (!preview && tool_preview_session)
	{
		is_dirty = tool_preview_was_dirty;
		tool_preview_session = false;
	}
}

void midi_editor::accept_tool_preview()
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	if (tool_preview)
	{
		push_undo(std::move(tool_preview));
		is_dirty = true;
	}
	else if (tool_preview_session)
		is_dirty = tool_preview_was_dirty;
	tool_preview_session = false;
}

void midi_editor::cancel_tool_preview()
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	if (tool_preview)
		tool_preview->undo(*this);
	tool_preview.reset();
	if (tool_preview_session)
	{
		is_dirty = tool_preview_was_dirty;
		selected_notes = tool_preview_original_selection;
	}
	tool_preview_session = false;
}

void midi_editor::commit_note_tool(std::vector<piano_note> before,
	std::vector<piano_note> after, const std::string& label, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	if (before.empty())
		return;
	auto op = std::make_unique<note_tool_op>(std::move(before), std::move(after), label);
	op->execute(*this);
	if (preview)
		tool_preview = std::move(op);
	else
		push_undo(std::move(op));
}

std::size_t midi_editor::chop_tool(unsigned divisions_per_beat, double time_multiplier,
	double gap_percent, bool absolute_pattern, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	prepare_tool_preview(preview);
	auto before = get_tool_target_notes();
	if (before.empty())
		return 0;

	divisions_per_beat = std::clamp(divisions_per_beat, 1u, 64u);
	time_multiplier = std::clamp(time_multiplier, 0.0625, 16.0);
	gap_percent = std::clamp(gap_percent, 0.0, 99.0);
	const tick_type slice = std::max<tick_type>(1,
		tick_type(std::llround(double(ppqn) * time_multiplier / divisions_per_beat)));
	std::vector<piano_note> after;

	for (const auto& source : before)
	{
		tick_type cursor = source.start_tick;
		while (cursor < source.end_tick)
		{
			tick_type boundary = absolute_pattern
				? ((cursor / slice) + 1) * slice
				: source.start_tick + ((cursor - source.start_tick) / slice + 1) * slice;
			const tick_type piece_end = std::min(source.end_tick, boundary);
			piano_note piece = source;
			piece.start_tick = cursor;
			const auto length = std::max<tick_type>(1, piece_end - cursor);
			piece.end_tick = cursor + std::max<tick_type>(1,
				tick_type(std::llround(double(length) * (1.0 - gap_percent / 100.0))));
			piece.end_tick = std::min(piece.end_tick, piece_end);
			piece.id = after.empty() ? source.id : next_note_id++;
			after.push_back(piece);
			cursor = piece_end;
		}
	}

	const auto count = after.size();
	commit_note_tool(std::move(before), std::move(after), "Chopper", preview);
	return count;
}

void midi_editor::flip_tool(bool horizontal, bool preserve_start_times,
	bool vertical, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	prepare_tool_preview(preview);
	auto before = get_tool_target_notes();
	if (before.empty() || (!horizontal && !vertical))
		return;
	auto after = before;

	tick_type min_start = before.front().start_tick, max_end = before.front().end_tick;
	tick_type max_start = before.front().start_tick;
	std::uint8_t min_key = before.front().key, max_key = before.front().key;
	for (const auto& note : before)
	{
		min_start = std::min(min_start, note.start_tick);
		max_start = std::max(max_start, note.start_tick);
		max_end = std::max(max_end, note.end_tick);
		min_key = std::min(min_key, note.key);
		max_key = std::max(max_key, note.key);
	}

	for (auto& note : after)
	{
		if (horizontal)
		{
			if (preserve_start_times)
			{
				const auto length = note.length();
				note.start_tick = min_start + (max_start - note.start_tick);
				note.end_tick = note.start_tick + length;
			}
			else
			{
				const auto old_start = note.start_tick;
				note.start_tick = min_start + (max_end - note.end_tick);
				note.end_tick = min_start + (max_end - old_start);
			}
		}
		if (vertical)
			note.key = std::uint8_t(unsigned(min_key) + unsigned(max_key) - note.key);
	}
	commit_note_tool(std::move(before), std::move(after), "Flip Score", preview);
}

std::size_t midi_editor::claw_tool(double period_beats, unsigned trash_every,
	double time_distortion, bool remove_short, bool stretch_to_compensate, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	prepare_tool_preview(preview);
	auto before = get_tool_target_notes();
	if (before.empty())
		return 0;
	period_beats = std::clamp(period_beats, 0.0625, 64.0);
	trash_every = std::clamp(trash_every, 2u, 64u);
	time_distortion = std::clamp(time_distortion, 0.0, 1.0);
	const tick_type period = std::max<tick_type>(1,
		tick_type(std::llround(double(ppqn) * period_beats)));
	const tick_type slice = std::max<tick_type>(1, period / trash_every);
	const tick_type origin = std::min_element(before.begin(), before.end(),
		[](const piano_note& a, const piano_note& b) { return a.start_tick < b.start_tick; })->start_tick;
	const tick_type original_end = std::max_element(before.begin(), before.end(),
		[](const piano_note& a, const piano_note& b) { return a.end_tick < b.end_tick; })->end_tick;
	const double exponent = std::pow(2.0, (0.5 - time_distortion) * 3.0);
	auto warp = [&](tick_type tick)
	{
		const tick_type rel = tick - origin;
		const tick_type base = (rel / period) * period;
		const double phase = double(rel % period) / double(period);
		return origin + base + tick_type(std::llround(std::pow(phase, exponent) * period));
	};

	std::vector<piano_note> after;
	for (const auto& source : before)
	{
		tick_type cursor = source.start_tick;
		bool first_piece = true;
		while (cursor < source.end_tick)
		{
			const auto rel = cursor - origin;
			const auto boundary = origin + ((rel / slice) + 1) * slice;
			const auto piece_end = std::min(source.end_tick, boundary);
			const auto slice_index = (rel / slice) % trash_every;
			if (slice_index + 1 != trash_every)
			{
				piano_note piece = source;
				piece.start_tick = warp(cursor);
				piece.end_tick = std::max(piece.start_tick + 1, warp(piece_end));
				piece.id = first_piece ? source.id : next_note_id++;
				if (!remove_short || piece.length() >= std::max<tick_type>(1, ppqn / 64))
				{
					after.push_back(piece);
					first_piece = false;
				}
			}
			cursor = piece_end;
		}
	}

	if (stretch_to_compensate && !after.empty())
	{
		const auto made_end = std::max_element(after.begin(), after.end(),
			[](const piano_note& a, const piano_note& b) { return a.end_tick < b.end_tick; })->end_tick;
		if (made_end > origin && original_end > origin)
		{
			const double scale = double(original_end - origin) / double(made_end - origin);
			for (auto& note : after)
			{
				note.start_tick = origin + tick_type(std::llround(double(note.start_tick - origin) * scale));
				note.end_tick = std::max(note.start_tick + 1,
					origin + tick_type(std::llround(double(note.end_tick - origin) * scale)));
			}
		}
	}
	const auto count = after.size();
	commit_note_tool(std::move(before), std::move(after), "Claw Machine", preview);
	return count;
}

double midi_editor::lfo_sample(lfo_shape shape, double phase_cycles)
{
	phase_cycles -= std::floor(phase_cycles);
	switch (shape)
	{
		case lfo_shape::triangle: return 1.0 - 4.0 * std::abs(phase_cycles - 0.5);
		case lfo_shape::square: return phase_cycles < 0.5 ? 1.0 : -1.0;
		default: return std::sin(phase_cycles * 6.2831853071795864769);
	}
}

std::size_t midi_editor::lfo_velocity_tool(double center, double range, double cycles,
	double phase, lfo_shape shape, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	prepare_tool_preview(preview);
	auto before = get_tool_target_notes();
	if (before.empty())
		return 0;
	auto after = before;
	const auto first = std::min_element(before.begin(), before.end(),
		[](const piano_note& a, const piano_note& b) { return a.start_tick < b.start_tick; })->start_tick;
	const auto last = std::max_element(before.begin(), before.end(),
		[](const piano_note& a, const piano_note& b) { return a.end_tick < b.end_tick; })->end_tick;
	const double span = double(std::max<tick_type>(1, last - first));
	for (auto& note : after)
	{
		const double x = double(note.start_tick - first) / span;
		const int value = int(std::lround(center + range * lfo_sample(shape, phase + cycles * x)));
		note.velocity = std::uint8_t(std::clamp(value, 1, 127));
	}
	const auto count = before.size();
	commit_note_tool(std::move(before), std::move(after), "Velocity LFO", preview);
	return count;
}

std::size_t midi_editor::lfo_control_tool(control_lane lane, std::uint8_t channel,
	tick_type begin, tick_type end, tick_type step, double center,
	double range, double cycles, double phase, lfo_shape shape, bool preview)
{
	std::lock_guard<std::recursive_mutex> lock(editor_mutex);
	prepare_tool_preview(preview);
	if (end <= begin)
		return 0;
	step = std::max<tick_type>(1, step);
	const double span = double(end - begin);
	const int maximum = lane == control_lane::pitch_bend ? 16383 : 127;
	std::vector<std::pair<tick_type, std::uint16_t>> points;
	for (tick_type tick = begin; tick <= end; tick += step)
	{
		const double x = double(tick - begin) / span;
		const int value = int(std::lround(center + range * lfo_sample(shape, phase + cycles * x)));
		points.emplace_back(tick, std::uint16_t(std::clamp(value, 0, maximum)));
		if (end - tick < step)
			break;
	}
	if (points.empty() || points.back().first != end)
	{
		const int value = int(std::lround(center + range * lfo_sample(shape, phase + cycles)));
		points.emplace_back(end, std::uint16_t(std::clamp(value, 0, maximum)));
	}
	auto op = std::make_unique<raw_control_range_op>(active_track, lane,
		std::uint8_t(channel & 0x0F), begin, end, std::move(points));
	const auto count = op->after.size();
	op->execute(*this);
	if (preview)
		tool_preview = std::move(op);
	else
		push_undo(std::move(op));
	return count;
}
