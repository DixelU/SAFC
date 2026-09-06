#pragma once

#include <cstdint>
#include <memory>
#include <string>

enum class syncore_phase_mode : std::uint32_t
{
	coherent,
	random_polarity,
	analytic,
	smooth_field,
	independent_bins,
};

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
	bool send_short_message(std::uint32_t message) noexcept;
	syncore_send_result try_send_short_message(std::uint32_t message) noexcept;
	bool active() const noexcept;
	syncore_runtime_status status() const;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};
