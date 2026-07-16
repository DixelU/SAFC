#pragma once
#ifndef SAFC_PLAYBACK_EVENT_SOURCE
#define SAFC_PLAYBACK_EVENT_SOURCE

#include <cstdint>

/**
 * Abstract, time-ordered source of playback events for simple_player.
 *
 * Both playback paths implement this: the file path (mmap + parse) and the
 * editor path (live in-memory notes). simple_player::run_from_external drives a
 * source exactly like simple_run drives the file parser — the player keeps
 * ownership of the send buffer, timing, throttle, pause and seek, so a source
 * only answers "what is the next event?".
 *
 * Events must be yielded in non-decreasing time order.
 */
struct generated_event
{
	enum class kind : std::uint8_t { note_on, note_off, control };

	std::uint64_t time_us;      // absolute time from start, microseconds
	std::uint32_t short_msg;    // packed MIDI short message; 0 = nothing to send
	kind          k;            // drives falling-notes visuals + fast-forward skip
	std::uint8_t  key;          // note events: 0-127 (visuals)
	std::uint8_t  velocity;     // note_on velocity (visuals)
	std::uint8_t  channel;      // 0-15 (visuals)
	std::uint16_t track_index;  // visuals colour id

	generated_event()
		: time_us(0), short_msg(0), k(kind::control),
		key(0), velocity(0), channel(0), track_index(0) {}
};

struct playback_event_source
{
	virtual ~playback_event_source() = default;

	// Total playback length in microseconds (for seek-fraction mapping + UI bar).
	virtual std::uint64_t total_duration_us() const = 0;

	// Reset iteration to time 0. Called at the start of every parser run,
	// including each seek restart, mirroring the file parser rebuilding its
	// track cursors from the track starts.
	virtual void rewind() = 0;

	// Pull the next event in non-decreasing time order. Returns false when the
	// source is exhausted; 'out' is only valid when the call returns true.
	virtual bool next(generated_event& out) = 0;
};

#endif // SAFC_PLAYBACK_EVENT_SOURCE
