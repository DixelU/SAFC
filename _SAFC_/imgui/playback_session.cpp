#define NOMINMAX
#include "playback_session.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

#include "../SAFC_InnerModules/simple_player.h"

namespace
{
// The standalone ImGui target does not link app_state.cpp. Core parser alerts
// are collected on the calling worker and published as data, never as widgets.
thread_local std::string* playback_alert_sink = nullptr;

struct scoped_alert_sink
{
	std::string* previous;

	explicit scoped_alert_sink(std::string& messages)
		: previous(std::exchange(playback_alert_sink, &messages)) {}

	~scoped_alert_sink() { playback_alert_sink = previous; }
};

void record_alert(std::string message)
{
	if (playback_alert_sink)
	{
		if (!playback_alert_sink->empty())
			playback_alert_sink->append("\n");
		playback_alert_sink->append(message);
	}
	else
		std::cerr << message << '\n';
}
}

void throw_alert_error(std::string&& message)
{
	record_alert(std::move(message));
}

void throw_alert_warning(std::string&& message)
{
	record_alert(std::move(message));
}

namespace safc::imgui_ui
{
struct playback_session::impl
{
	simple_player engine;
	std::jthread worker;
	std::jthread stop_worker;
	std::atomic_bool busy{false};
	std::atomic_bool stop_busy{false};
	std::atomic_bool closing{false};
	std::mutex status_mutex;
	std::string message = "Open a MIDI file to begin";
	std::string error;

	// UI-thread-owned settings are copied into each run before it is dispatched.
	std::vector<std::string> devices;
	std::size_t device = 0;
	std::wstring bank;
	syncore_preferences preferences;
	bool stopping = false;
	std::uint64_t duration_us = 0;
	simple_player::draw_data visuals;
	float drawn_width = 0;
	float drawn_height = 0;

	impl()
	{
		engine.init();
		devices = engine.get_device_names();
		device = simple_player::syncore_available()
			? engine.get_syncore_device_index() : engine.get_current_device();
		visuals.enable_simulated_lag = false;
	}

	void set_status(std::string new_message, std::string new_error = {})
	{
		std::lock_guard lock(status_mutex);
		message = std::move(new_message);
		error = std::move(new_error);
	}

	void run(std::stop_token token, std::wstring path, std::size_t output,
		std::wstring sound_bank, syncore_preferences synth, bool silent, bool start_paused)
	{
		struct completion
		{
			std::atomic_bool& busy;
			~completion() { busy.store(false, std::memory_order_release); }
		} finished{busy};
		std::string alerts;
		scoped_alert_sink sink(alerts);
		std::stop_callback cancellation(token, [this] { engine.stop(); });
		auto cancelled = [&]
		{
			return token.stop_requested() || closing.load(std::memory_order_acquire);
		};
		try
		{
			if (cancelled())
			{
				set_status("Stopped");
				return;
			}
			set_status("Preparing MIDI output...");
			// Retire the old sink once before applying either preference draft.
			// The setters then cannot reopen the previous bank as an intermediate
			// step, and switching to an external output never loads that bank.
			if (!engine.use_silent_output())
			{
				set_status(cancelled() ? "Stopped" : "MIDI output failed",
					cancelled() ? std::string{} : "Could not detach the previous MIDI output");
				return;
			}
			if (!silent)
			{
				if (!engine.set_syncore_preferences(synth) ||
					!engine.set_syncore_bank_path(std::move(sound_bank)) ||
					!engine.set_device(output) || !engine.ensure_output({}))
				{
					if (cancelled())
						set_status("Stopped");
					else
					{
						auto detail = engine.get_last_output_error();
						if (detail.empty())
							detail = alerts.empty() ? "Could not prepare the selected MIDI output" : alerts;
						set_status("MIDI output failed", std::move(detail));
					}
					return;
				}
			}
			if (cancelled())
			{
				set_status("Stopped");
				return;
			}
			set_status("Reading MIDI...");
			engine.simple_run(std::move(path), 0.0, start_paused);
			if (!alerts.empty())
				set_status("Playback ended with a message", std::move(alerts));
			else if (cancelled())
				set_status("Stopped");
			else if (auto detail = engine.get_last_output_error(); !detail.empty() && !silent)
				set_status("Playback failed", std::move(detail));
			else
				set_status("Playback finished");
		}
		catch (const std::exception& exception)
		{
			engine.stop();
			set_status("Playback failed", exception.what());
		}
		catch (...)
		{
			engine.stop();
			set_status("Playback failed", "An unexpected error stopped playback");
		}
	}
};

playback_session::playback_session() : impl_(std::make_unique<impl>()) {}

playback_session::~playback_session()
{
	shutdown();
}

bool playback_session::open(std::wstring path, bool silent, bool start_paused)
{
	if (path.empty() || impl_->closing.load(std::memory_order_acquire) ||
		impl_->busy.load(std::memory_order_acquire) ||
		impl_->stop_busy.load(std::memory_order_acquire))
		return false;
	if (impl_->worker.joinable())
		impl_->worker.join();
	if (impl_->stop_worker.joinable())
		impl_->stop_worker.join();

	impl_->stopping = false;
	impl_->duration_us = 0;
	impl_->set_status("Opening MIDI...");
	impl_->busy.store(true, std::memory_order_release);
	try
	{
		// Both prior workers have retired, so nothing can set the terminal gate
		// concurrently with this reset. init() only enumerates devices; all
		// output opening remains on the run worker below.
		const auto selected_name = impl_->device < impl_->devices.size()
			? impl_->devices[impl_->device] : std::string{};
		impl_->engine.init();
		impl_->devices = impl_->engine.get_device_names();
		const auto selected = std::find(impl_->devices.begin(), impl_->devices.end(), selected_name);
		impl_->device = selected != impl_->devices.end()
			? static_cast<std::size_t>(selected - impl_->devices.begin())
			: simple_player::syncore_available()
				? impl_->engine.get_syncore_device_index() : impl_->engine.get_current_device();
		impl_->worker = std::jthread(
			[state = impl_.get(), path = std::move(path), output = impl_->device,
			 sound_bank = impl_->bank, synth = impl_->preferences, silent, start_paused]
			(std::stop_token token) mutable
			{
				state->run(token, std::move(path), output, std::move(sound_bank), synth, silent, start_paused);
			});
	}
	catch (const std::exception& exception)
	{
		impl_->busy.store(false, std::memory_order_release);
		impl_->set_status("Could not start playback", exception.what());
		return false;
	}
	return true;
}

void playback_session::toggle_pause()
{
	if (!impl_->stopping && impl_->busy.load(std::memory_order_acquire) &&
		impl_->engine.is_playing() && !impl_->engine.is_fast_forwarding())
		impl_->engine.toggle_pause();
}

void playback_session::seek(double fraction)
{
	if (!impl_->stopping && impl_->busy.load(std::memory_order_acquire) &&
		std::isfinite(fraction) && impl_->engine.is_playing())
		impl_->engine.seek_to(std::clamp(fraction, 0.0, 1.0));
}

void playback_session::stop()
{
	if (impl_->stopping || !impl_->busy.load(std::memory_order_acquire))
		return;
	impl_->stopping = true;
	impl_->stop_busy.store(true, std::memory_order_release);
	impl_->worker.request_stop();
	impl_->engine.stop();
	try
	{
		impl_->stop_worker = std::jthread([state = impl_.get()]
		{
			// Terminal cancellation also interrupts SYNCore bank preparation.
			// Its gate cannot be reset by simple_run's ordinary stop handling,
			// so cancellation continues even while the UI is minimized.
			state->engine.shutdown();
			state->stop_busy.store(false, std::memory_order_release);
		});
	}
	catch (...)
	{
		impl_->stop_busy.store(false, std::memory_order_release);
		throw;
	}
}

void playback_session::shutdown()
{
	if (!impl_ || impl_->closing.exchange(true, std::memory_order_acq_rel))
		return;
	impl_->stopping = true;
	impl_->worker.request_stop();
	// A pending Stop already owns terminal teardown. Otherwise retire the
	// scheduler and SYNCore here before joining a possibly paused run.
	if (impl_->stop_worker.joinable())
		impl_->stop_worker.join();
	else
		impl_->engine.shutdown();
	if (impl_->worker.joinable())
		impl_->worker.join();
	impl_->stopping = false;
}

playback_snapshot playback_session::snapshot()
{
	playback_snapshot result;
	const bool run_busy = impl_->busy.load(std::memory_order_acquire);
	result.busy = run_busy || impl_->stop_busy.load(std::memory_order_acquire);
	if (!result.busy)
		impl_->stopping = false;
	result.stopping = impl_->stopping && result.busy;

	result.playing = run_busy && impl_->engine.is_playing();
	result.paused = result.playing && impl_->engine.is_paused();
	result.seeking = result.playing && impl_->engine.is_seeking();
	const auto& info = impl_->engine.get_info();
	result.scanned_bytes = info.scanned.load(std::memory_order_acquire);
	result.total_bytes = info.size.load(std::memory_order_acquire);
	if (result.playing)
	{
		// playing is published after open() has finished metadata writes. The
		// service rejects another open until this run has completely retired.
		impl_->duration_us = info.total_duration_us;
		result.position_us = impl_->engine.get_position_us();
		result.lead_in_us = impl_->engine.get_start_lead_in_remaining_us();
	}
	result.duration_us = impl_->duration_us;
	{
		std::lock_guard lock(impl_->status_mutex);
		result.message = impl_->message;
		result.error = impl_->error;
	}
	if (result.stopping)
		result.message = "Stopping...";
	else if (result.playing)
		result.message = result.paused ? "Paused" : "Playing";
	return result;
}

std::vector<std::string> playback_session::device_names() const
{
	return impl_->devices;
}

void playback_session::select_device(std::size_t index)
{
	if (index < impl_->devices.size())
		impl_->device = index;
}

std::size_t playback_session::selected_device() const
{
	return impl_->device;
}

bool playback_session::syncore_available() const
{
	return simple_player::syncore_available();
}

void playback_session::configure_synth(std::wstring bank, const syncore_preferences& preferences)
{
	impl_->bank = std::move(bank);
	impl_->preferences = preferences;
}

void playback_session::draw_visuals(float width, float height, float visible_seconds)
{
	if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1)
		return;
	if (width != impl_->drawn_width || height != impl_->drawn_height)
	{
		const float keyboard_height = std::min(height * 0.22f, width * 0.12f);
		const float note_height = height - keyboard_height;
		impl_->visuals.reinit(width, note_height, keyboard_height, keyboard_height * 0.5625f, 0);
		impl_->visuals.move(0, note_height * 0.5f + keyboard_height);
		impl_->drawn_width = width;
		impl_->drawn_height = height;
	}
	impl_->visuals.scroll_window_us = static_cast<std::uint64_t>(
		(std::isfinite(visible_seconds) ? std::clamp(visible_seconds, 0.05f, 120.f) : 3.f) * 1'000'000.0f);
	std::string alerts;
	scoped_alert_sink sink(alerts);
	impl_->engine.draw_at(impl_->visuals, impl_->engine.get_visual_position_us());
	if (!alerts.empty())
		impl_->set_status("Visualization failed", std::move(alerts));
}
}
