// Behavioural tests for the player's falling-notes store (player_visuals.h):
// note-off matching, reclamation, ring growth and wrap-around, compaction and
// the key-press stacks, checked against brute-force models. Portable.
#include "../SAFC_InnerModules/player_visuals.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using visuals = simple_player_visuals;

void require(bool value, const char* message)
{
	if (!value) throw std::runtime_error(message);
}

void matching_and_counts()
{
	visuals v;
	constexpr std::uint8_t key = 60;
	v.push_note_on(key, 10, 0, 0, 100);
	v.push_note_on(key, 10, 0, 0, 100);
	require(v.buffered_note_count() == 2, "identical notes retain separate spans");
	v.push_note_off(key, 20, 0, 0);
	v.push_note_off(key, 30, 0, 0);
	auto notes = v.notes_for_key(key);
	require(notes.size() == 2 && notes[0].end_time_us == 30 && notes[1].end_time_us == 20,
		"note-offs match the latest started note first (LIFO)");
	v.push_note_off(key, 40, 0, 0);
	require(v.notes_for_key(key).size() == 2, "an unmatched note-off is ignored");
	v.push_note_off(key, 40, 7, 0);
	require(v.notes_for_key(key).size() == 2, "a note-off from another track does not match");

	// Different tracks on one key match independently.
	v.clear();
	v.push_note_on(key, 1, 1, 0, 100);
	v.push_note_on(key, 2, 2, 0, 100);
	v.push_note_off(key, 5, 1, 0);
	notes = v.notes_for_key(key);
	require(notes.size() == 2 && notes[0].track_id == (1u << 4) && notes[0].end_time_us == 5 &&
		notes[1].end_time_us == visuals::held_end, "matching is per track id");

	v.clear();
	constexpr std::size_t note_count = 2'004'096;
	for (std::size_t index = 0; index < note_count; ++index)
		v.push_note_on(key, static_cast<std::uint64_t>(index), 0, 0, 100);
	require(v.buffered_note_count() == note_count, "no artificial note cap");
	require(v.retained_note_count() == note_count, "retained equals live without culling");
}

void held_notes_do_not_block_reclamation()
{
	visuals v;
	constexpr std::uint8_t first_key = 60;
	constexpr std::size_t held_keys = 4;
	constexpr std::size_t completed_per_key = 4096;

	for (std::size_t key_offset = 0; key_offset < held_keys; ++key_offset)
	{
		const auto key = static_cast<std::uint8_t>(first_key + key_offset);
		v.push_note_on(key, 0, 0, 0, 100);
		for (std::size_t index = 0; index < completed_per_key; ++index)
		{
			const std::uint64_t start = 10 + index * 2;
			v.push_note_on(key, start, 1, 0, 100);
			v.push_note_off(key, start + 1, 1, 0);
		}
	}

	const std::int64_t cutoff = 10 + completed_per_key * 2 + 2;
	v.cull_expired(cutoff);
	require(v.buffered_note_count() == held_keys, "held notes do not pin later expired notes");
	require(v.retained_note_count() == held_keys, "expired notes behind held ones are compacted away");
	for (std::size_t key_offset = 0; key_offset < held_keys; ++key_offset)
	{
		const auto key = static_cast<std::uint8_t>(first_key + key_offset);
		auto notes = v.notes_for_key(key);
		require(notes.size() == 1 && notes[0].end_time_us == visuals::held_end, "each key keeps its held note");
		v.push_note_off(key, static_cast<std::uint64_t>(cutoff), 0, 0);
		require(v.notes_for_key(key)[0].end_time_us == static_cast<std::uint64_t>(cutoff),
			"note-off still matches after compaction renumbered the ring");
	}
	v.cull_expired(cutoff + 1);
	require(v.buffered_note_count() == 0 && v.retained_note_count() == 0, "released held notes are reclaimed");
	require(v.rings[0].capacity() == 0 && v.rings[1].capacity() == 0, "an empty ring releases its storage");
}

struct model_note
{
	std::uint64_t start, end;
	std::uint32_t track_id;
	std::uint8_t key;
};

// The parser emits note-ons and note-offs in time order. This stream models
// that: every note's on and off are generated up front and sorted by time.
struct event
{
	std::uint64_t time;
	std::uint32_t order;
	std::uint32_t track;
	std::uint8_t key;
	bool on;
};

std::vector<event> make_stream(std::mt19937_64& rng, int notes, std::uint8_t first_key, int key_span,
	int tracks, int max_length, int held_one_in)
{
	std::vector<event> stream;
	std::uint64_t time = 0;
	std::uint32_t order = 0;
	for (int i = 0; i < notes; ++i)
	{
		time += rng() % 3;
		const auto key = static_cast<std::uint8_t>(first_key + rng() % key_span);
		const auto track = static_cast<std::uint32_t>(rng() % tracks);
		stream.push_back({time, order++, track, key, true});
		if (rng() % held_one_in)
			stream.push_back({time + 1 + rng() % max_length, order++, track, key, false});
	}
	std::stable_sort(stream.begin(), stream.end(),
		[](const event& a, const event& b) { return a.time < b.time; });
	return stream;
}

// Applies one event to the plain model with the player's LIFO matching rule.
void apply_to_model(std::vector<model_note>& model, const event& e)
{
	if (e.on)
	{
		model.push_back({e.time, visuals::held_end, e.track << 4, e.key});
		return;
	}
	for (auto it = model.rbegin(); it != model.rend(); ++it)
		if (it->key == e.key && it->track_id == (e.track << 4) && it->end == visuals::held_end)
		{
			it->end = e.time;
			return;
		}
}

void apply_to_visuals(visuals& v, const event& e)
{
	if (e.on)
		v.push_note_on(e.key, e.time, e.track, 0, 100);
	else
		v.push_note_off(e.key, e.time, e.track, 0);
}

void cull_model(std::vector<model_note>& model, std::uint64_t cutoff)
{
	model.erase(std::remove_if(model.begin(), model.end(),
		[&](const model_note& n) { return n.end != visuals::held_end && n.end < cutoff; }), model.end());
}

// Random traffic through the rings with culling, compared with the model.
void wraparound_and_compaction()
{
	std::mt19937_64 rng(42);
	visuals v;
	std::vector<model_note> model;
	auto stream = make_stream(rng, 200'000, 48, 24, 3, 50, 8);
	std::size_t layout_changes = 0;
	std::uint32_t layout0 = v.rings[0].layout_generation;
	std::uint64_t last_cutoff = 0;

	for (std::size_t i = 0; i < stream.size(); ++i)
	{
		apply_to_visuals(v, stream[i]);
		apply_to_model(model, stream[i]);
		if (i % 97 == 0)
		{
			const std::uint64_t cutoff = stream[i].time > 60 ? stream[i].time - 60 : 0;
			if (cutoff > last_cutoff)
			{
				last_cutoff = cutoff;
				v.cull_expired(static_cast<std::int64_t>(cutoff));
				cull_model(model, cutoff);
			}
		}
		if (v.rings[0].layout_generation != layout0)
		{
			++layout_changes;
			layout0 = v.rings[0].layout_generation;
		}
	}
	require(v.buffered_note_count() == model.size(), "live count matches the model after random traffic");
	for (std::uint8_t key = 48; key < 72; ++key)
	{
		auto notes = v.notes_for_key(key);
		std::vector<model_note> expected;
		for (const auto& n : model) if (n.key == key) expected.push_back(n);
		require(notes.size() == expected.size(), "per-key count matches the model");
		for (std::size_t k = 0; k < notes.size(); ++k)
			require(notes[k].start_time_us == expected[k].start && notes[k].end_time_us == expected[k].end &&
				notes[k].track_id == expected[k].track_id, "per-key notes match the model in start order");
	}
	require(layout_changes > 0, "the rings grew, compacted or wrapped during the run");
}

void dirty_pages()
{
	visuals v;
	v.push_note_on(60, 1, 0, 0, 100);
	v.buffered_note_count();
	auto& ring = v.rings[visuals::ring_of_key(60)];
	require(ring.dirty_pages.size() == 1 && ring.page_dirty[0], "a new note dirties its page");
	ring.clear_dirty();
	require(ring.dirty_pages.empty() && !ring.page_dirty[0], "clear_dirty resets the page flags");
	v.push_note_off(60, 5, 0, 0);
	v.buffered_note_count();
	require(ring.dirty_pages.size() == 1, "an ended note dirties its page once");
	v.push_note_on(60, 6, 0, 0, 100);
	v.buffered_note_count();
	require(ring.dirty_pages.size() == 1, "a page is listed once while dirty");
	const auto generation = ring.layout_generation;
	for (int i = 0; i < 2000; ++i)
		v.push_note_on(60, 10 + i, 0, 0, 100);
	v.buffered_note_count();
	require(ring.layout_generation != generation, "growth changes the layout generation");
	// Growth drops the pages dirtied before it (a full re-upload follows); the
	// pages listed afterwards are exactly the flagged ones, each once.
	std::vector<std::uint32_t> listed(ring.dirty_pages.begin(), ring.dirty_pages.end());
	std::sort(listed.begin(), listed.end());
	require(std::adjacent_find(listed.begin(), listed.end()) == listed.end(), "dirty pages are listed once");
	std::size_t flagged = 0;
	for (auto flag : ring.page_dirty) flagged += flag ? 1 : 0;
	require(flagged == listed.size() && listed.size() <= 2000 / visuals::note_ring::page_notes + 2,
		"dirty flags and the dirty list agree after growth");
}

// Brute force: the latest-started note on the key that contains now.
bool brute_pressed(const std::vector<model_note>& model, std::uint8_t key, std::uint64_t now, std::uint32_t& track)
{
	bool found = false;
	std::uint64_t best_start = 0;
	std::size_t best_index = 0;
	for (std::size_t i = 0; i < model.size(); ++i)
	{
		const auto& n = model[i];
		if (n.key != key || n.start > now || (n.end != visuals::held_end && n.end < now)) continue;
		if (!found || n.start >= best_start) { found = true; best_start = n.start; best_index = i; }
	}
	if (found) track = model[best_index].track_id;
	return found;
}

void key_press_stacks()
{
	std::mt19937_64 rng(7);
	visuals v;
	std::vector<model_note> model;
	auto stream = make_stream(rng, 60'000, 60, 6, 4, 30, 10);
	std::size_t cursor = 0;
	std::uint64_t now = 0;
	std::size_t agreements = 0, pressed_total = 0;

	while (cursor < stream.size())
	{
		// The parser runs ahead of the clock by a bounded lookahead.
		while (cursor < stream.size() && stream[cursor].time <= now + 40)
		{
			apply_to_visuals(v, stream[cursor]);
			apply_to_model(model, stream[cursor]);
			++cursor;
		}
		now += rng() % 4;
		const std::uint64_t cutoff = now > 100 ? now - 100 : 0;
		v.cull_expired(static_cast<std::int64_t>(cutoff));
		cull_model(model, cutoff);
		v.advance_key_state(static_cast<std::int64_t>(now));
		for (std::uint8_t key = 60; key < 66; ++key)
		{
			std::uint32_t expected = 0, actual = 0;
			const bool expected_pressed = brute_pressed(model, key, now, expected);
			const bool actual_pressed = v.pressed_track_id(key, static_cast<std::int64_t>(now), actual);
			require(expected_pressed == actual_pressed, "pressed state matches brute force");
			if (expected_pressed)
			{
				require(expected == actual, "pressed colour comes from the latest-started sounding note");
				++pressed_total;
			}
			++agreements;
		}
	}
	require(pressed_total > 1000 && agreements > 10'000, "the key-press scenario exercised presses");

	// Time moving backwards rebuilds the stacks instead of trusting them.
	v.advance_key_state(0);
	std::uint32_t track = 0;
	for (std::uint8_t key = 60; key < 66; ++key)
		require(!v.pressed_track_id(key, 0, track) || brute_pressed(model, key, 0, track),
			"after rewinding, nothing that started later is reported pressed");
}
}

int main()
{
	try
	{
		matching_and_counts();
		held_notes_do_not_block_reclamation();
		wraparound_and_compaction();
		dirty_pages();
		key_press_stacks();
		std::cout << "player_visuals tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << '\n';
		return 1;
	}
}
