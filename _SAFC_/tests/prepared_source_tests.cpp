// Equivalence tests for prepared_midi_event_source against a port of the
// player's serial file parser: whole-file streams, tempo timing, rewind, and
// seeks that must reconstruct held notes and channel state. Portable.
#include "../SAFC_InnerModules/prepared_midi_event_source.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using u8 = std::uint8_t; using u32 = std::uint32_t; using u64 = std::uint64_t;

void require(bool value, const char* message)
{
	if (!value) throw std::runtime_error(message);
}

void vlv(std::vector<u8>& out, u64 value)
{
	u8 buffer[10]; int count = 0;
	do { buffer[count++] = value & 0x7F; value >>= 7; } while (value);
	while (count--) out.push_back(buffer[count] | (count ? 0x80 : 0));
}

std::vector<u8> make_midi(int tracks, u64 note_pairs_per_track, int tempo_events)
{
	std::vector<u8> f = {'M','T','h','d',0,0,0,6,0,1,0,(u8)tracks,(u8)(480 >> 8),(u8)480};
	u64 seed = 99;
	auto rnd = [&]{ seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (u32)(seed >> 33); };
	for (int t = 0; t < tracks; ++t)
	{
		std::vector<u8> d;
		u8 rsb = 0;
		auto ev = [&](u64 delta, u8 status, u8 a, int b)
		{
			vlv(d, delta);
			if (status != rsb) { d.push_back(status); rsb = status; }
			d.push_back(a);
			if (b >= 0) d.push_back((u8)b);
		};
		u64 pairs = 0, tick = 0;
		const u8 channel = t & 0x0F;
		int tempos_left = (t == 0) ? tempo_events : 0;
		u64 next_tempo = 0;
		while (pairs < note_pairs_per_track)
		{
			if (tempos_left && tick >= next_tempo)
			{
				vlv(d, 0); d.push_back(0xFF); d.push_back(0x51); d.push_back(3);
				u32 us = 300000 + rnd() % 500000;
				d.push_back(us >> 16); d.push_back(us >> 8); d.push_back(us);
				rsb = 0; --tempos_left; next_tempo += 150;
			}
			switch (rnd() % 6)
			{
				case 0: ev(0, 0xB0 | channel, 1 + rnd() % 120, rnd() % 128); break;
				case 1: ev(0, 0xC0 | channel, rnd() % 128, -1); break;
				case 2: ev(0, 0xE0 | channel, rnd() % 128, rnd() % 128); break;
				default: break;
			}
			int chord = 1 + rnd() % 6;
			u8 keys[8];
			for (int i = 0; i < chord; ++i)
			{
				keys[i] = 24 + rnd() % 80;
				ev(i ? 0 : 1 + rnd() % 4, 0x90 | channel, keys[i], 64 + rnd() % 60);
			}
			tick += 1;
			for (int i = 0; i < chord; ++i)
				if (rnd() % 5)
					ev(i ? 0 : 1 + rnd() % 4, 0x90 | channel, keys[i], 0);
				else
					ev(i ? 0 : 1 + rnd() % 4, 0xA0 | channel, keys[i], 3);
			tick += 1;
			pairs += chord;
		}
		vlv(d, 0); d.push_back(0xFF); d.push_back(0x2F); d.push_back(0);
		f.insert(f.end(), {'M','T','r','k'});
		u32 n = (u32)d.size();
		f.push_back(n >> 24); f.push_back(n >> 16); f.push_back(n >> 8); f.push_back(n);
		f.insert(f.end(), d.begin(), d.end());
	}
	return f;
}

struct ref_event
{
	u64 tick; u32 msg; u8 kind, key, velocity, channel; std::size_t track;
	auto key_tuple() const { return std::tie(msg, kind, key, velocity, channel, track); }
};

// Port of simple_player::parser_thread_func over all tracks: every channel
// event in tick order (events sharing a tick: any order).
std::vector<ref_event> reference_stream(const std::vector<u8>& file)
{
	struct cursor { const u8* p; const u8* e; u64 tick; u8 rsb; };
	std::vector<cursor> tracks;
	const u8* begin = file.data(); const u8* end = begin + file.size();
	const u8* p = begin + 14;
	while (p + 8 <= end)
	{
		u32 n = (u32)p[4] << 24 | (u32)p[5] << 16 | (u32)p[6] << 8 | p[7];
		tracks.push_back({p + 8, p + 8 + n, 0, 0});
		p += 8 + n;
	}
	std::vector<ref_event> out;
	for (std::size_t t = 0; t < tracks.size(); ++t)
	{
		auto& c = tracks[t];
		while (c.p < c.e)
		{
			u64 delta = 0; u8 b; do { b = *c.p++; delta = delta << 7 | (b & 0x7F); } while (b & 0x80);
			c.tick += delta;
			u8 command = *c.p++, d1 = 0, d2 = 0;
			if (command < 0x80) { d1 = command; command = c.rsb; } else { if (command < 0xF0) c.rsb = command; if (command < 0xF0 || command == 0xFF) d1 = *c.p++; else d1 = 0xFF; }
			if (command < 0x80) break;
			switch (command >> 4)
			{
				case 0x8: case 0x9: case 0xA: case 0xB: case 0xE:
				{
					d2 = *c.p++;
					const u8 kind = (command >> 4) == 0x8 || ((command >> 4) == 0x9 && d2 == 0) ? 1 : (command >> 4) == 0x9 ? 0 : 2;
					out.push_back({c.tick, u32(command) | d1 << 8 | d2 << 16, kind, d1, d2, u8(command & 0x0F), t});
					break;
				}
				case 0xC: case 0xD:
					out.push_back({c.tick, u32(command) | d1 << 8, 2, d1, 0, u8(command & 0x0F), t});
					break;
				case 0xF:
				{
					if (command == 0xFF) c.rsb = 0;
					if (command == 0xFF && d1 == 0x2F) { c.p = c.e; break; }
					u64 len = 0; do { b = *c.p++; len = len << 7 | (b & 0x7F); } while (b & 0x80);
					c.p += len;
					break;
				}
			}
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const ref_event& a, const ref_event& b) { return a.tick < b.tick; });
	return out;
}

std::vector<ref_event> drain(prepared_midi_event_source& source, std::vector<u64>* times = nullptr)
{
	std::vector<ref_event> out;
	generated_event event;
	while (source.next(event))
	{
		require(event.tick.has_value(), "prepared events carry their tick");
		out.push_back({*event.tick, event.short_msg, u8(event.k), event.key, event.velocity, event.channel, event.track_index});
		if (times) times->push_back(event.time_us);
	}
	return out;
}

// Same events per tick, ignoring the order inside a tick.
void expect_same_stream(std::vector<ref_event> a, std::vector<ref_event> b, const char* message)
{
	require(a.size() == b.size(), message);
	auto by_tick_then_key = [](const ref_event& x, const ref_event& y)
	{
		return x.tick != y.tick ? x.tick < y.tick : x.key_tuple() < y.key_tuple();
	};
	std::sort(a.begin(), a.end(), by_tick_then_key);
	std::sort(b.begin(), b.end(), by_tick_then_key);
	for (std::size_t i = 0; i < a.size(); ++i)
		require(a[i].tick == b[i].tick && a[i].key_tuple() == b[i].key_tuple(), message);
}

// What simple_player's fast-forward keeps while skipping: held notes (LIFO per
// key/track/channel) and the last channel-state message per slot.
struct ff_state
{
	std::map<std::tuple<u8, std::size_t, u8>, std::vector<std::pair<u64, u8>>> held; // (key, track, channel) -> [(start tick, velocity)]
	std::map<std::tuple<std::size_t, u8, u32>, std::pair<u64, u32>> state;			  // (track, channel, slot) -> (tick, msg)

	void apply(const ref_event& e)
	{
		const u8 family = e.msg & 0xF0;
		if (e.kind == 0) { held[{e.key, e.track, e.channel}].push_back({e.tick, e.velocity}); return; }
		if (e.kind == 1) { auto& s = held[{e.key, e.track, e.channel}]; if (!s.empty()) s.pop_back(); return; }
		if (family == 0xA0) return;
		const u32 slot = family == 0xB0 ? (e.msg >> 8) & 0x7F : family == 0xC0 ? 128 : family == 0xE0 ? 129 : 130;
		state[{e.track, e.channel, slot}] = {e.tick, e.msg};
	}

	std::vector<std::tuple<u8, std::size_t, u8, u64, u8>> held_list() const
	{
		std::vector<std::tuple<u8, std::size_t, u8, u64, u8>> out;
		for (const auto& [k, stack] : held)
			for (const auto& [tick, velocity] : stack)
				out.emplace_back(std::get<0>(k), std::get<1>(k), std::get<2>(k), tick, velocity);
		std::sort(out.begin(), out.end());
		return out;
	}
};

void stream_matches_reference(const std::filesystem::path& path, const std::vector<u8>& file, unsigned decode_threads)
{
	std::string error;
	prepared_midi_event_source::options options;
	options.decode_threads = decode_threads;
	options.page_events = 777; // small pages: many refills and reorders
	auto source = prepared_midi_event_source::open(path.wstring(), {}, nullptr, error, options);
	require(source != nullptr, "the prepared source opens a clean file");
	require(source->track_count() == 6, "track count");
	auto reference = reference_stream(file);
	require(source->event_count() == reference.size(), "scanner event count equals the parsed event count");

	std::vector<u64> times;
	auto events = drain(*source, &times);
	expect_same_stream(events, reference, "prepared stream equals the serial parser's stream");
	for (std::size_t i = 1; i < times.size(); ++i)
		require(times[i] >= times[i - 1], "event times are non-decreasing");
	require(times.back() <= source->total_duration_us(), "the last event is within the duration");

	source->rewind();
	auto again = drain(*source);
	expect_same_stream(again, reference, "rewind replays the same stream");

	auto fork = source->fork_reader();
	expect_same_stream(drain(*fork), reference, "a forked reader yields the same stream");
}

void seek_reconstructs_state(const std::filesystem::path& path, const std::vector<u8>& file)
{
	std::string error;
	prepared_midi_event_source::options options;
	options.page_events = 1000;
	options.checkpoint_bytes = 16 << 10;
	auto source = prepared_midi_event_source::open(path.wstring(), {}, nullptr, error, options);
	require(source != nullptr && source->checkpoint_count() > 0, "the fixture has seek checkpoints");
	auto reference = reference_stream(file);
	std::vector<u64> reference_times;
	auto full = drain(*source, &reference_times);
	expect_same_stream(full, reference, "baseline stream");

	int seeks_with_checkpoints = 0;
	for (double fraction : {0.1, 0.33, 0.5, 0.77, 0.95, 1.0})
	{
		const u64 target_us = static_cast<u64>(fraction * source->total_duration_us());
		source->seek(target_us);
		std::vector<u64> times;
		auto seeked = drain(*source, &times);

		// The prefix below the target reconstructs state; the rest must be the
		// full stream's suffix from the first event at or after the target.
		std::size_t split = 0;
		while (split < times.size() && times[split] < target_us) ++split;
		std::size_t full_split = 0;
		while (full_split < reference_times.size() && reference_times[full_split] < target_us) ++full_split;
		expect_same_stream(std::vector<ref_event>(seeked.begin() + split, seeked.end()),
			std::vector<ref_event>(full.begin() + full_split, full.end()),
			"events from the seek target on equal the full stream's suffix");

		ff_state from_start, from_seek;
		for (std::size_t i = 0; i < full_split; ++i) from_start.apply(full[i]);
		for (std::size_t i = 0; i < split; ++i) from_seek.apply(seeked[i]);
		require(from_start.held_list() == from_seek.held_list(), "held notes at the seek target match a replay from the start");
		require(from_start.state.size() == from_seek.state.size(), "channel state slot count matches");
		for (const auto& [slot, value] : from_start.state)
		{
			auto found = from_seek.state.find(slot);
			require(found != from_seek.state.end() && found->second.second == value.second,
				"channel state at the seek target matches a replay from the start");
		}
		if (split < full_split) ++seeks_with_checkpoints;
	}
	require(seeks_with_checkpoints >= 3, "later seeks started from checkpoints instead of tick zero");
}

void damaged_file_is_refused(const std::filesystem::path& path, std::vector<u8> file)
{
	file[14 + 7] += 5; // first track's declared length overruns its data
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), (std::streamsize)file.size());
	std::string error;
	auto source = prepared_midi_event_source::open(path.wstring(), {}, nullptr, error, {});
	require(source == nullptr && !error.empty(), "a file the scanner cannot verify is refused with a message");
}
}

int main()
{
	try
	{
		const auto dir = std::filesystem::temp_directory_path() / "safc-prepared-source-tests";
		std::filesystem::create_directories(dir);
		const auto path = dir / "fixture.mid";
		auto file = make_midi(6, 30'000, 40);
		std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), (std::streamsize)file.size());

		stream_matches_reference(path, file, 0);
		stream_matches_reference(path, file, 1);
		stream_matches_reference(path, file, 4);
		seek_reconstructs_state(path, file);
		damaged_file_is_refused(dir / "damaged.mid", file);

		std::filesystem::remove_all(dir);
		std::cout << "prepared_midi_event_source tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << '\n';
		return 1;
	}
}
