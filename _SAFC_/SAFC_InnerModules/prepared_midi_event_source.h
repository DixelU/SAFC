#pragma once
#ifndef SAFC_PREPARED_MIDI_EVENT_SOURCE
#define SAFC_PREPARED_MIDI_EVENT_SOURCE

#include "playback_event_source.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/**
 * Playback source for a plain MIDI file that decodes its tracks on a thread
 * pool instead of in the player's single parser thread.
 *
 * Opening maps the file and runs the parallel track scanner (tempo map, track
 * bounds, seek checkpoints). Playback then decodes every track into compact
 * event pages a bounded distance ahead of the consumer; next() merges the
 * pages by tick, so the serial work per event is a heap step and a few loads
 * rather than byte decoding. Memory stays bounded by the pages in flight, not
 * by the file: multi-gigabyte files keep streaming.
 *
 * A file the scanner cannot verify end to end (damaged chunk lengths and the
 * like) is refused, so callers can fall back to simple_player::simple_run and
 * its repair-tolerant serial parser.
 *
 * seek() positions every track at its latest checkpoint at or before the
 * target and yields the checkpoint's channel state and held notes at their
 * original ticks, so the player's fast-forward reconstructs the same state it
 * would have from the start of the file.
 */
struct prepared_midi_source_options
{
	unsigned scan_threads = 0;		// 0: hardware concurrency
	unsigned decode_threads = 0;	// 0: about half the hardware threads, at most six
	std::size_t page_events = 0;	// 0: automatic from the track count
	std::size_t pages_ahead = 2;	// decoded pages kept ready per track
	std::uint64_t checkpoint_bytes = 0;	// seek checkpoint spacing per track; 0: automatic
};

class prepared_midi_event_source final : public playback_event_source
{
public:
	using progress_callback = std::function<void(const std::string&)>;
	using options = prepared_midi_source_options;

	static std::shared_ptr<prepared_midi_event_source> open(
		const std::wstring& filename,
		progress_callback progress,
		const std::atomic<bool>* cancel_requested,
		std::string& error,
		options settings = {});

	~prepared_midi_event_source() override;

	prepared_midi_event_source(const prepared_midi_event_source&) = delete;
	prepared_midi_event_source& operator=(const prepared_midi_event_source&) = delete;

	std::uint64_t total_duration_us() const override;
	void rewind() override;
	void seek(std::uint64_t target_us) override;
	bool next(generated_event& out) override;

	std::uint16_t track_count() const;
	std::uint64_t event_count() const;
	std::uint64_t note_count() const;
	std::size_t checkpoint_count() const;

	// Independent cursor over the same mapping and scan (for the video exporter).
	std::shared_ptr<prepared_midi_event_source> fork_reader() const;

private:
	struct shared;
	struct impl;
	prepared_midi_event_source(std::shared_ptr<const shared> data, options settings);

	std::unique_ptr<impl> impl_;
};

#endif // SAFC_PREPARED_MIDI_EVENT_SOURCE
