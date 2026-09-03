#pragma once
#ifndef SAFC_COMPRESSED_MIDI_EVENT_SOURCE
#define SAFC_COMPRESSED_MIDI_EVENT_SOURCE

#include "playback_event_source.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/**
 * Playback source for a MIDI hidden behind one or more archive/compression
 * layers (XZ, ZIP, 7z, and the other formats supported by libarchive).
 *
 * Opening is deliberately a preparation step: the nested input is decoded as
 * one forward-only pipeline and MIDI channel events are written directly to a
 * temporary, independently Zstandard-compressed page store. The gigantic raw
 * MIDI is never materialised. Once prepared, next() lazily merges the cached
 * track pages and simple_player can drive it through run_from_external().
 *
 * The first implementation uses a transient cache and a linear seek fallback.
 * Its page-store boundary is intentionally separate so a future indexed-XZ
 * byte source or persistent cache can replace preparation without touching the
 * player/UI integration.
 */
class compressed_midi_event_source final : public playback_event_source
{
public:
	using progress_callback = std::function<void(const std::string&)>;

	/**
	 * Prepare an archive (or a plain MIDI, useful for equivalence testing).
	 * Returns null and fills error on failure/cancellation.
	 */
	static std::shared_ptr<compressed_midi_event_source> open(
		const std::wstring& filename,
		progress_callback progress,
		const std::atomic<bool>* cancel_requested,
		std::string& error);

	~compressed_midi_event_source() override;

	compressed_midi_event_source(const compressed_midi_event_source&) = delete;
	compressed_midi_event_source& operator=(const compressed_midi_event_source&) = delete;

	std::uint64_t total_duration_us() const override;
	void rewind() override;
	bool next(generated_event& out) override;

	std::uint16_t track_count() const;
	std::uint64_t event_count() const;
	std::uint32_t archive_depth() const;

	// Create an independent cursor over the prepared page store. The cache files
	// remain alive until the final reader is destroyed.
	std::shared_ptr<compressed_midi_event_source> fork_reader() const;

private:
	struct impl;
	explicit compressed_midi_event_source(std::unique_ptr<impl> implementation);

	std::unique_ptr<impl> impl_;
};

#endif // SAFC_COMPRESSED_MIDI_EVENT_SOURCE
