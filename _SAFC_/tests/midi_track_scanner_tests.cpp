// Regression tests for the parallel open-time scanner against a port of the
// player's serial walk, plus checkpoint consistency. Portable: no Windows,
// Boost or GL dependencies.
#include "../SAFC_InnerModules/midi_track_scanner.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
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

struct generated_file
{
	std::vector<u8> bytes;
	std::vector<std::size_t> headers; // offset of each MTrk
};

// Black-MIDI-shaped: running status, chords at delta 0, note-off as velocity 0,
// tempo events in track 0, a few controllers, programs and bends everywhere.
generated_file make_midi(int tracks, u64 note_pairs_per_track, int tempo_events, bool explicit_offs)
{
	generated_file file;
	auto& f = file.bytes;
	f = {'M','T','h','d',0,0,0,6,0,1,0,(u8)tracks,(u8)(480 >> 8),(u8)480};
	u64 seed = 777;
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
				u32 us = 400000 + rnd() % 400000;
				d.push_back(us >> 16); d.push_back(us >> 8); d.push_back(us);
				rsb = 0; --tempos_left; next_tempo += 200;
			}
			switch (rnd() % 7)
			{
				case 0: ev(0, 0xB0 | channel, 1 + rnd() % 120, rnd() % 128); break;
				case 1: ev(0, 0xC0 | channel, rnd() % 128, -1); break;
				case 2: ev(0, 0xE0 | channel, rnd() % 128, rnd() % 128); break;
				case 3: ev(0, 0xD0 | channel, rnd() % 128, -1); break;
				default: break;
			}
			int chord = 1 + rnd() % 8;
			u8 keys[8];
			for (int i = 0; i < chord; ++i)
			{
				keys[i] = 24 + rnd() % 80;
				ev(i ? 0 : 1 + rnd() % 3, 0x90 | channel, keys[i], 64 + rnd() % 60);
			}
			tick += 1;
			// Leave some notes hanging over the next chord so checkpoints hold notes.
			for (int i = 0; i < chord; ++i)
				if (rnd() % 4)
					ev(i ? 0 : 1 + rnd() % 3, explicit_offs ? (0x80 | channel) : (0x90 | channel), keys[i], 0);
				else
					ev(i ? 0 : 1 + rnd() % 3, 0xA0 | channel, keys[i], 5); // aftertouch filler
			tick += 1;
			pairs += chord;
		}
		vlv(d, 0); d.push_back(0xFF); d.push_back(0x2F); d.push_back(0);
		file.headers.push_back(f.size());
		f.insert(f.end(), {'M','T','r','k'});
		u32 n = (u32)d.size();
		f.push_back(n >> 24); f.push_back(n >> 16); f.push_back(n >> 8); f.push_back(n);
		f.insert(f.end(), d.begin(), d.end());
	}
	return file;
}

// Port of simple_player::read_through_one_track (the serial walk).
struct legacy_walk
{
	struct track { const u8* begin; const u8* end; };
	std::vector<track> tracks;
	std::map<u64, u32> tempo;
	u64 ticks_length = 0;
	bool error = false;
	bool size_mismatch = false;

	static u8 gvi(const u8*& cur, const u8* end) { if (cur < end) return *(cur++); return 0; }
	static u64 get_vlv(const u8*& cur, const u8* end)
	{ u64 v = 0; u8 b; do { b = gvi(cur, end); v = v << 7 | (b & 0x7F); } while (b & 0x80); return v; }

	bool one_track(const u8*& cur, const u8* end)
	{
		u64 current_tick = 0; u32 header = 0, rsb = 0, expected = 0;
		bool is_good = true, is_reading = true;
		while (header != 0x4D54726Bu && cur < end) header = (header << 8) | gvi(cur, end);
		if (cur >= end) return true;
		for (int i = 0; i < 4; i++) expected = (expected << 8) | gvi(cur, end);
		const auto raw_begin = cur;
		while (cur < end && is_reading && is_good)
		{
			u8 command = 0, param = 0;
			current_tick += get_vlv(cur, end);
			command = gvi(cur, end);
			if (command < 0x80) { param = command; command = rsb; }
			else { if (command < 0xF0) rsb = command; if (command < 0xF0 || command == 0xFF) param = gvi(cur, end); else param = 0xFF; }
			if (command < 0x80 || cur >= end) { error = true; is_good = false; break; }
			switch (command >> 4)
			{
				case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: gvi(cur, end); break;
				case 0xC: case 0xD: break;
				case 0xF:
				{
					is_reading &= !(param == 0x2F && command == 0xFF);
					rsb = 0;
					if (param == 0x51 && command == 0xFF)
					{ gvi(cur, end); u32 v = gvi(cur, end) << 16; v |= gvi(cur, end) << 8; v |= gvi(cur, end); tempo[current_tick] = v; continue; }
					auto len = get_vlv(cur, end);
					for (u64 i = 0; i < len; ++i) gvi(cur, end);
					if (!is_reading) continue;
					break;
				}
				default: error = true; is_good = false; break;
			}
		}
		if (!is_good || is_reading) return false;
		if (expected != (u64)(cur - raw_begin)) size_mismatch = true;
		ticks_length = std::max(current_tick, ticks_length);
		tracks.push_back({raw_begin, cur});
		return true;
	}

	bool open(const u8* begin, const u8* end)
	{
		auto p = begin + 14;
		while (p < end) if (!one_track(p, end)) return false;
		return !tracks.empty();
	}
};

// Combined result: scanner prefix + legacy walk from resume_at, as open() does.
struct combined
{
	std::vector<std::pair<const u8*, const u8*>> tracks;
	std::map<u64, u32> tempo;
	u64 ticks_length = 0;
	bool ok = true;
};

combined combine(const u8* begin, const u8* end, const safc::midi_scan_options& options, safc::midi_scan_result* raw = nullptr)
{
	combined out;
	auto scan = safc::scan_midi_tracks(begin + 14, end, options);
	for (auto& t : scan.tracks)
	{
		for (auto [tick, tempo] : t.tempos) out.tempo[tick] = tempo;
		out.ticks_length = std::max(out.ticks_length, t.length_ticks);
		out.tracks.emplace_back(t.begin, t.end);
	}
	legacy_walk rest;
	auto p = scan.resume_at ? scan.resume_at : end;
	while (p < end) if (!rest.one_track(p, end)) { out.ok = false; break; }
	for (auto [tick, tempo] : rest.tempo) out.tempo[tick] = tempo;
	out.ticks_length = std::max(out.ticks_length, rest.ticks_length);
	for (auto& t : rest.tracks) out.tracks.emplace_back(t.begin, t.end);
	if (raw) *raw = std::move(scan);
	return out;
}

void expect_same_as_legacy(const std::vector<u8>& bytes, const safc::midi_scan_options& options, const char* name)
{
	const u8* begin = bytes.data(); const u8* end = begin + bytes.size();
	legacy_walk legacy;
	const bool legacy_ok = legacy.open(begin, end);
	auto mixed = combine(begin, end, options);
	require(mixed.ok == legacy_ok, name);
	if (!legacy_ok) return;
	require(mixed.tracks.size() == legacy.tracks.size(), name);
	for (std::size_t i = 0; i < legacy.tracks.size(); ++i)
		require(mixed.tracks[i].first == legacy.tracks[i].begin && mixed.tracks[i].second == legacy.tracks[i].end, name);
	require(mixed.tempo == legacy.tempo, name);
	require(mixed.ticks_length == legacy.ticks_length, name);
}

// Independent per-track replay used to validate checkpoints: walks a chunk from
// its start, recording the events after 'from' (or all) and the held/channel
// state at 'stop_at'.
struct replay
{
	struct event { u64 tick; u8 status, d1, d2; };
	std::vector<event> events;
	std::vector<safc::midi_scan_checkpoint::held_note> held;
	std::vector<std::pair<u64, u32>> state; // (tick, message) in arrival order
	u64 tick_at_stop = 0; u8 rsb_at_stop = 0;

	static void run(const u8* begin, const u8* end, u64 start_tick, u8 start_rsb,
		const u8* stop_at, replay& out)
	{
		const u8* cur = begin; u64 tick = start_tick; u8 rsb = start_rsb;
		std::vector<std::vector<safc::midi_scan_checkpoint::held_note>> held(16 * 128);
		std::map<std::pair<u8, u32>, std::pair<u32, std::pair<u64, u32>>> slots; // (ch, slot) -> (order, (tick, msg))
		u32 order = 0; bool stopped = false;
		while (cur < end)
		{
			if (stop_at && cur >= stop_at && !stopped)
			{
				stopped = true;
				out.tick_at_stop = tick; out.rsb_at_stop = rsb;
				for (auto& stack : held) for (auto& n : stack) out.held.push_back(n);
				std::sort(out.held.begin(), out.held.end(), [](auto& a, auto& b) { return a.order < b.order; });
				std::vector<std::pair<u32, std::pair<u64, u32>>> entries;
				for (auto& [k, v] : slots) entries.push_back(v);
				std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
				for (auto& e : entries) out.state.push_back(e.second);
			}
			u64 delta = 0; u8 b; do { b = *cur++; delta = delta << 7 | (b & 0x7F); } while (b & 0x80);
			tick += delta;
			u8 command = *cur++, d1 = 0, d2 = 0;
			if (command < 0x80) { d1 = command; command = rsb; } else { if (command < 0xF0) rsb = command; if (command < 0xF0 || command == 0xFF) d1 = *cur++; else d1 = 0xFF; }
			const u8 ch = command & 0x0F;
			switch (command >> 4)
			{
				case 0x8: case 0x9: case 0xA: case 0xB: case 0xE:
				{
					d2 = *cur++;
					const u8 kind = command >> 4;
					if (kind == 0x9 && d2) held[(ch << 7) | d1].push_back({tick, order++, d1, ch, d2});
					else if (kind == 0x9 || kind == 0x8) { auto& s = held[(ch << 7) | d1]; if (!s.empty()) s.pop_back(); }
					else if (kind == 0xB) slots[{ch, d1 & 0x7F}] = {order++, {tick, u32(command) | d1 << 8 | d2 << 16}};
					else if (kind == 0xE) slots[{ch, 129}] = {order++, {tick, u32(command) | d1 << 8 | d2 << 16}};
					break;
				}
				case 0xC: case 0xD:
					slots[{ch, u32((command >> 4) == 0xC ? 128 : 130)}] = {order++, {tick, u32(command) | d1 << 8}};
					break;
				case 0xF:
				{
					rsb = 0;
					if (command == 0xFF && d1 == 0x51) { cur += 4; if (stopped) out.events.push_back({tick, command, d1, 0}); continue; }
					u64 len = 0; do { b = *cur++; len = len << 7 | (b & 0x7F); } while (b & 0x80);
					cur += len;
					if (stopped) out.events.push_back({tick, command, d1, 0});
					if (command == 0xFF && d1 == 0x2F) return;
					continue;
				}
			}
			if (stopped) out.events.push_back({tick, command, d1, d2});
		}
	}
};

void checkpoint_consistency(const std::vector<u8>& bytes, unsigned threads)
{
	safc::midi_scan_options options;
	options.threads = threads;
	options.min_checkpoint_bytes = 4096;
	options.max_checkpoint_bytes = 4096;
	options.parallel_threshold_bytes = 0;
	auto scan = safc::scan_midi_tracks(bytes.data() + 14, bytes.data() + bytes.size(), options);
	require(scan.resume_at == nullptr && !scan.tracks.empty(), "Checkpoint fixture scans cleanly");
	std::size_t checked = 0;
	for (const auto& track : scan.tracks)
	{
		require(!track.checkpoints.empty(), "Tracks with 4 KiB spacing get checkpoints");
		u64 previous = 0;
		for (const auto& checkpoint : track.checkpoints)
		{
			require(checkpoint.position > track.begin && checkpoint.position < track.end, "Checkpoint inside the track");
			require(checkpoint.tick >= previous, "Checkpoints ascend");
			previous = checkpoint.tick;

			replay full;
			replay::run(track.begin, track.end, 0, 0, checkpoint.position, full);
			require(full.tick_at_stop == checkpoint.tick, "Checkpoint tick matches a full walk");
			require(full.rsb_at_stop == checkpoint.running_status, "Checkpoint running status matches");
			require(full.held.size() == checkpoint.held.size(), "Held note count matches");
			for (std::size_t i = 0; i < full.held.size(); ++i)
			{
				const auto& a = full.held[i]; const auto& b = checkpoint.held[i];
				require(a.start_tick == b.start_tick && a.key == b.key && a.channel == b.channel && a.velocity == b.velocity,
					"Held notes match in note-on order");
			}
			require(full.state.size() == checkpoint.state.size(), "Channel state entry count matches");
			for (std::size_t i = 0; i < full.state.size(); ++i)
				require(full.state[i].first == checkpoint.state[i].tick && full.state[i].second == checkpoint.state[i].message,
					"Channel state entries match in arrival order");

			// Resuming at the checkpoint must reproduce the full walk's suffix.
			replay resumed;
			replay::run(checkpoint.position, track.end, checkpoint.tick, checkpoint.running_status, checkpoint.position, resumed);
			require(resumed.events.size() == full.events.size(), "Resumed event count matches");
			for (std::size_t i = 0; i < full.events.size(); ++i)
			{
				const auto& a = full.events[i]; const auto& b = resumed.events[i];
				require(a.tick == b.tick && a.status == b.status && a.d1 == b.d1 && a.d2 == b.d2, "Resumed events match");
			}
			++checked;
		}
	}
	require(checked > 50, "Enough checkpoints were validated");
}
}

int main()
{
	try
	{
		auto clean = make_midi(16, 20'000, 64, false);
		auto clean_offs = make_midi(6, 20'000, 16, true);

		for (unsigned threads : {1u, 4u})
		{
			safc::midi_scan_options options;
			options.threads = threads;
			options.parallel_threshold_bytes = 0;
			expect_same_as_legacy(clean.bytes, options, "Clean file matches the serial walk");
			expect_same_as_legacy(clean_offs.bytes, options, "Clean file with explicit offs matches");

			safc::midi_scan_result raw;
			auto mixed = combine(clean.bytes.data(), clean.bytes.data() + clean.bytes.size(), options, &raw);
			require(raw.resume_at == nullptr && raw.tracks.size() == 16, "All sixteen tracks are clean");
			for (auto& t : raw.tracks) require(t.event_count > 0 && t.note_on_count > 0, "Counts recorded");

			// Declared length too large: that track and all after go to the serial walk.
			{
				auto bytes = clean.bytes;
				auto h = clean.headers[3];
				bytes[h + 7] += 10;
				safc::midi_scan_result r;
				combine(bytes.data(), bytes.data() + bytes.size(), options, &r);
				require(r.tracks.size() == 3 && r.resume_at == bytes.data() + h, "Oversized length stops the clean prefix at that header");
				expect_same_as_legacy(bytes, options, "Oversized length matches the serial walk");
			}
			// Declared length too small.
			{
				auto bytes = clean.bytes;
				auto h = clean.headers[5];
				bytes[h + 7] -= 10;
				safc::midi_scan_result r;
				combine(bytes.data(), bytes.data() + bytes.size(), options, &r);
				require(r.tracks.size() == 5 && r.resume_at == bytes.data() + h, "Undersized length stops the clean prefix at that header");
				expect_same_as_legacy(bytes, options, "Undersized length matches the serial walk");
			}
			// Junk between two tracks is skipped by both.
			{
				auto bytes = clean.bytes;
				auto h = clean.headers[2];
				bytes.insert(bytes.begin() + h, {0xDE, 0xAD, 0xBE, 0xEF, 0x4D});
				safc::midi_scan_result r;
				combine(bytes.data(), bytes.data() + bytes.size(), options, &r);
				require(r.tracks.size() == 16 && r.resume_at == nullptr, "Junk between tracks is skipped");
				expect_same_as_legacy(bytes, options, "Junk between tracks matches the serial walk");
			}
			// Truncated file.
			{
				auto bytes = clean.bytes;
				bytes.resize(clean.headers[15] + 100);
				safc::midi_scan_result r;
				combine(bytes.data(), bytes.data() + bytes.size(), options, &r);
				require(r.tracks.size() == 15 && r.resume_at == bytes.data() + clean.headers[15], "Truncated last track goes to the serial walk");
				expect_same_as_legacy(bytes, options, "Truncated file matches the serial walk");
			}
			// Corrupt status byte inside a track.
			{
				auto bytes = clean.bytes;
				auto h = clean.headers[7];
				bytes[h + 8 + 1] = 0x00; // first status byte becomes a data byte with zero running status
				safc::midi_scan_result r;
				combine(bytes.data(), bytes.data() + bytes.size(), options, &r);
				require(r.tracks.size() == 7 && r.resume_at == bytes.data() + h, "Zero running status is left to the serial walk");
				expect_same_as_legacy(bytes, options, "Zero running status matches the serial walk");
			}
			// Cancellation.
			{
				std::atomic<bool> cancel{true};
				auto o = options; o.cancel = &cancel;
				auto r = safc::scan_midi_tracks(clean.bytes.data() + 14, clean.bytes.data() + clean.bytes.size(), o);
				require(r.cancelled, "Cancellation is reported");
			}
			// Progress reaches the walked size.
			{
				std::atomic<u64> progress{0};
				auto o = options; o.scanned_bytes = &progress;
				auto r = safc::scan_midi_tracks(clean.bytes.data() + 14, clean.bytes.data() + clean.bytes.size(), o);
				u64 total = 0; for (auto& t : r.tracks) total += t.end - t.begin;
				require(progress.load() == total, "Progress counts every walked byte");
			}
		}

		checkpoint_consistency(clean.bytes, 4);
		checkpoint_consistency(clean_offs.bytes, 1);

		std::cout << "midi_track_scanner tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << '\n';
		return 1;
	}
}
