#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Values are persisted as SYNCORE_PHASE_MODE and must stay stable.
enum class syncore_phase_mode : std::uint32_t
{
	coherent = 0,
	analytic = 2,
};

// 1, 3 and 4 were retired polarity/FFT decorrelation modes; they continue as
// analytic. Unknown values fall back to direct sampling.
inline syncore_phase_mode syncore_phase_mode_from_stored(std::uint32_t value) noexcept
{
	return value >= 1 && value <= 4 ? syncore_phase_mode::analytic : syncore_phase_mode::coherent;
}

enum class syncore_send_result { queued, full, unavailable };

struct syncore_preferences
{
	std::uint32_t sample_rate = 48000;
	std::uint32_t buffer_frames = 4096;
	std::uint32_t maximum_cohorts = 4096;
	std::uint32_t render_threads = 0;
	syncore_phase_mode phase_mode = syncore_phase_mode::coherent;
	double output_gain_db = -12.0;
	bool limiter_enabled = true;
	// Realtime playback only: drop the quietest notes instead of falling behind.
	bool shed_quiet_notes = false;

	bool operator==(const syncore_preferences&) const = default;
};

struct syncore_runtime_status
{
	std::string message = "Stopped";
	std::string error;
	std::uint64_t preparation_completed = 0;
	std::uint64_t preparation_total = 0;
	std::uint64_t cache_bytes = 0;
	std::uint64_t total_cache_bytes = 0;
	bool running = false;
	bool ready = false;
	bool preparing = false;
	bool active = false;
};

// Keeps SAFSYNCore headers out of simple_player.h and provides a no-op build
// when the optional submodule is disabled.
class syncore_output
{
public:
	syncore_output();
	~syncore_output();
	syncore_output(const syncore_output&) = delete;
	syncore_output& operator=(const syncore_output&) = delete;

	static bool available() noexcept;
	bool start(const std::wstring& bank_path, const syncore_preferences& preferences,
		std::string& error);
	void stop() noexcept;
	void panic() noexcept;
	bool send_short_message(std::uint32_t message) noexcept;
	syncore_send_result try_send_short_message(std::uint32_t message,
		std::optional<std::uint64_t> tick = {}) noexcept;
	bool active() const noexcept;
	syncore_runtime_status status() const;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};
