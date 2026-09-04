#include "syncore_output.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <thread>

#ifdef SAFC_WITH_SYNCORE
#include <windows_synth.h>
#endif

struct syncore_output::impl
{
#ifdef SAFC_WITH_SYNCORE
	// The player sender, device-selection code, and process shutdown can all
	// touch this object.  Keep a shared local reference while calling into the
	// synth so stop() can detach the current session without invalidating a
	// concurrent sender.
	mutable std::mutex synth_mutex;
	std::mutex start_mutex;
	std::shared_ptr<safsyn::WindowsSynth> synth;
	std::uint64_t synth_generation = 0;

	struct detached_synth
	{
		std::shared_ptr<safsyn::WindowsSynth> value;
		std::uint64_t generation = 0;
	};

	detached_synth detach_synth() noexcept
	{
		std::lock_guard lock(synth_mutex);
		active.store(false, std::memory_order_release);
		return {std::move(synth), ++synth_generation};
	}

	bool owns_synth(const std::shared_ptr<safsyn::WindowsSynth>& candidate,
		std::uint64_t generation) const noexcept
	{
		std::lock_guard lock(synth_mutex);
		return synth == candidate && synth_generation == generation;
	}

	bool activate_synth(const std::shared_ptr<safsyn::WindowsSynth>& candidate,
		std::uint64_t generation) noexcept
	{
		std::lock_guard lock(synth_mutex);
		if (synth != candidate || synth_generation != generation)
			return false;
		active.store(true, std::memory_order_release);
		return true;
	}

	bool still_idle_after(std::uint64_t generation) const noexcept
	{
		std::lock_guard lock(synth_mutex);
		return !synth && synth_generation == generation;
	}
#endif
	std::atomic<bool> active{false};
	mutable std::mutex status_mutex;
	syncore_runtime_status runtime_status;

	void publish_status(syncore_runtime_status next)
	{
		std::lock_guard lock(status_mutex);
		runtime_status = std::move(next);
	}

	syncore_runtime_status status() const
	{
		std::lock_guard lock(status_mutex);
		return runtime_status;
	}
};

#ifdef SAFC_WITH_SYNCORE
namespace
{
std::string narrow_status(const std::wstring& value)
{
	std::string result;
	result.reserve(value.size());
	for (const auto character : value)
		result.push_back(character >= 32 && character < 127
			? static_cast<char>(character) : '?');
	return result;
}

syncore_runtime_status make_runtime_status(const safsyn::WindowsSynthStats& source)
{
	syncore_runtime_status result;
	result.message = narrow_status(source.status);
	result.error = source.error;
	result.preparation_completed = source.playback.preparation.completed;
	result.preparation_total = source.playback.preparation.total;
	result.cache_bytes = source.playback.preparation.cache_bytes;
	result.total_cache_bytes = source.playback.preparation.total_cache_bytes;
	result.running = source.running;
	result.ready = source.playback.ready;
	result.preparing = source.playback.preparing;
	return result;
}
}
#endif

syncore_output::syncore_output() : impl_(std::make_unique<impl>()) {}
syncore_output::~syncore_output() { stop(); }

bool syncore_output::available() noexcept
{
#ifdef SAFC_WITH_SYNCORE
	return true;
#else
	return false;
#endif
}

bool syncore_output::start(const std::wstring& bank_path,
	const syncore_preferences& preferences, std::string& error)
{
	// Serialize competing starts. stop() uses the separate session lock, so it
	// can still cancel asynchronous bank/output initialization promptly.
#ifdef SAFC_WITH_SYNCORE
	std::unique_lock start_lock(impl_->start_mutex);
#endif
	stop();
	error.clear();
	syncore_runtime_status starting;
	starting.message = "Starting SYNCore...";
	starting.running = true;
	impl_->publish_status(std::move(starting));

#ifdef SAFC_WITH_SYNCORE
	try
	{
		auto synth = std::make_shared<safsyn::WindowsSynth>();
		safsyn::WindowsSynthOptions options;
		options.bank_path = bank_path;
		options.playback.sample_rate = preferences.sample_rate;
		options.playback.buffer_frames = preferences.buffer_frames;
		options.playback.maximum_cohorts = preferences.maximum_cohorts;
		options.playback.render_threads = preferences.render_threads;
		options.playback.mastering.output_gain_db = preferences.output_gain_db;
		options.playback.mastering.limiter_enabled = preferences.limiter_enabled;
		switch (preferences.phase_mode)
		{
		case syncore_phase_mode::coherent:
			options.playback.phase.mode = safsyn::PhaseMode::Coherent;
			break;
		case syncore_phase_mode::random_polarity:
			options.playback.phase.mode = safsyn::PhaseMode::RandomPolarity;
			break;
		case syncore_phase_mode::analytic:
			options.playback.phase.mode = safsyn::PhaseMode::Analytic;
			break;
		case syncore_phase_mode::smooth_field:
			options.playback.phase.mode = safsyn::PhaseMode::SmoothField;
			break;
		case syncore_phase_mode::independent_bins:
			options.playback.phase.mode = safsyn::PhaseMode::IndependentBins;
			break;
		}
		// Publish the session only after start() has created its delivery thread.
		// A concurrent stop either sees the whole session and cancels it, or runs
		// before this block and is followed by a still-observable session.
		std::uint64_t generation = 0;
		{
			std::lock_guard lock(impl_->synth_mutex);
			synth->start(options);
			generation = ++impl_->synth_generation;
			impl_->synth = synth;
		}

		// Sound-bank loading is asynchronous. Do not let SAFC start its event
		// clock until SYNCore can accept the initial controller/program state.
		for (;;)
		{
			const auto stats = synth->stats();
			auto runtime = make_runtime_status(stats);
			if (!impl_->owns_synth(synth, generation))
			{
				error = "SYNCore startup was stopped";
				return false;
			}
			impl_->publish_status(runtime);
			if (!stats.error.empty())
			{
				error = stats.error;
				auto failed = impl_->detach_synth();
				if (failed.value)
				{
					failed.value->panic();
					failed.value->stop();
				}
				runtime.message = "Error";
				runtime.error = error;
				runtime.running = false;
				runtime.active = false;
				impl_->publish_status(std::move(runtime));
				return false;
			}
			if (stats.playback.ready)
			{
				if (!impl_->activate_synth(synth, generation))
				{
					error = "SYNCore startup was stopped";
					return false;
				}
				runtime.message = "Ready";
				runtime.ready = true;
				runtime.preparing = false;
				runtime.active = true;
				impl_->publish_status(std::move(runtime));
				return true;
			}
			if (!stats.running)
			{
				error = "SYNCore stopped before its audio output became ready";
				auto failed = impl_->detach_synth();
				if (failed.value)
				{
					failed.value->panic();
					failed.value->stop();
				}
				runtime.message = "Error";
				runtime.error = error;
				runtime.running = false;
				runtime.active = false;
				impl_->publish_status(std::move(runtime));
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		stop();
		auto failed = impl_->status();
		failed.message = "Error";
		failed.error = error;
		failed.running = false;
		failed.active = false;
		impl_->publish_status(std::move(failed));
		return false;
	}
	catch (...)
	{
		error = "unknown SYNCore startup failure";
		stop();
		auto failed = impl_->status();
		failed.message = "Error";
		failed.error = error;
		failed.running = false;
		failed.active = false;
		impl_->publish_status(std::move(failed));
		return false;
	}
#else
	(void)bank_path;
	(void)preferences;
	error = "this SAFC build does not include SYNCore";
	auto failed = impl_->status();
	failed.message = "Unavailable";
	failed.error = error;
	failed.running = false;
	impl_->publish_status(std::move(failed));
	return false;
#endif
}

void syncore_output::stop() noexcept
{
#ifdef SAFC_WITH_SYNCORE
	const auto stopped = impl_->detach_synth();
	if (stopped.value)
	{
		stopped.value->panic();
		stopped.value->stop();
	}
#endif
	try
	{
		// Do not overwrite a newer start that raced a completed old stop.
#ifdef SAFC_WITH_SYNCORE
		if (!impl_->still_idle_after(stopped.generation))
			return;
#else
		impl_->active.store(false, std::memory_order_release);
#endif
		syncore_runtime_status stopped;
		stopped.message = "Stopped";
		impl_->publish_status(std::move(stopped));
	}
	catch (...)
	{
		// stop() is part of the audio cleanup path and must remain noexcept.
	}
}

bool syncore_output::send_short_message(std::uint32_t message) noexcept
{
#ifdef SAFC_WITH_SYNCORE
	std::shared_ptr<safsyn::WindowsSynth> synth;
	{
		std::lock_guard lock(impl_->synth_mutex);
		if (!impl_->active.load(std::memory_order_acquire))
			return false;
		synth = impl_->synth;
	}
	return synth && synth->send_short_message(message);
#else
	(void)message;
	return false;
#endif
}

bool syncore_output::active() const noexcept
{
	return impl_->active.load(std::memory_order_acquire);
}

syncore_runtime_status syncore_output::status() const
{
	auto result = impl_->status();
	result.active = impl_->active.load(std::memory_order_acquire);
	return result;
}
