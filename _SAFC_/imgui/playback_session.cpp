#define NOMINMAX
#include "playback_session.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <cwctype>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

#include "../SAFC_InnerModules/simple_player.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"

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
	std::atomic_bool preparation_cancel{false};
	std::mutex audition_mutex;
	std::array<std::uint8_t, 16 * 128> audition_keys{};
	bool audition_preparing = false;
	mutable std::mutex source_mutex;
	std::condition_variable member_changed;
	std::shared_ptr<playback_event_source> source;
	playback_session::source_factory export_factory;
	std::wstring path;
	std::vector<std::string> archive_members;
	std::size_t chosen_member = std::numeric_limits<std::size_t>::max();
	bool waiting_for_member = false;
	std::uint32_t archive_layer = 0;
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
		std::wstring sound_bank, syncore_preferences synth, bool silent, bool start_paused,
		std::shared_ptr<playback_event_source> external, double seek_fraction, bool output_only = false)
	{
		bool audition_ready = false;
		struct completion
		{
			impl& state;
			bool& audition_ready;
			~completion()
			{
				std::lock_guard lock(state.audition_mutex);
				if (audition_ready && !state.preparation_cancel.load(std::memory_order_acquire) &&
					!state.closing.load(std::memory_order_acquire))
					for (std::size_t note = 0; note < state.audition_keys.size(); ++note)
						if (const auto velocity = state.audition_keys[note])
							state.engine.preview_note(static_cast<std::uint8_t>(note / 128),
								static_cast<std::uint8_t>(note % 128), velocity, true);
				state.audition_preparing = false;
				state.busy.store(false, std::memory_order_release);
			}
		} finished{*this, audition_ready};
		std::string alerts;
		scoped_alert_sink sink(alerts);
		std::stop_callback cancellation(token, [this]
		{
			preparation_cancel.store(true, std::memory_order_release);
			member_changed.notify_all();
			engine.stop();
		});
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
			if (!external && !output_only)
			{
				auto extension = std::filesystem::path(path).extension().wstring();
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
				if (extension != L".mid" && extension != L".midi")
				{
					set_status("Inspecting archive...");
					std::string detail;
					auto prepared = compressed_midi_event_source::open(path,
						[this](const std::string& text) { set_status(text); },
						&preparation_cancel, detail,
						[this](const std::vector<std::string>& members, std::uint32_t depth)
						{
							std::unique_lock lock(source_mutex);
							archive_members = members;
							archive_layer = depth;
							chosen_member = std::numeric_limits<std::size_t>::max();
							waiting_for_member = true;
							member_changed.wait(lock, [this]
							{
								return preparation_cancel.load(std::memory_order_acquire) ||
									chosen_member != std::numeric_limits<std::size_t>::max();
							});
							waiting_for_member = false;
							archive_members.clear();
							return preparation_cancel.load(std::memory_order_acquire)
								? members.size() : chosen_member;
						});
					if (!prepared)
					{
						set_status(cancelled() ? "Stopped" : "Archive preparation failed",
							cancelled() ? std::string{} : detail);
						return;
					}
					external = prepared;
					std::lock_guard lock(source_mutex);
					source = prepared;
					export_factory = [prepared] { return prepared->fork_reader(); };
				}
			}
			if (cancelled()) { set_status("Stopped"); return; }
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
			if (output_only)
			{
				set_status("Ready for editor audition");
				audition_ready = true;
				return;
			}
			set_status("Reading MIDI...");
			if (external)
				engine.run_from_external(external.get(), seek_fraction, start_paused);
			else
				engine.simple_run(std::move(path), seek_fraction, start_paused);
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
	return start(std::move(path), {}, silent, start_paused, 0, {});
}

bool playback_session::restart(bool silent, bool start_paused)
{
	std::shared_ptr<playback_event_source> source;
	source_factory factory;
	std::wstring path;
	{
		std::lock_guard lock(impl_->source_mutex);
		source = impl_->source;
		factory = impl_->export_factory;
		path = impl_->path;
	}
	return start(std::move(path), std::move(source), silent, start_paused, 0, std::move(factory));
}

bool playback_session::open_external(std::shared_ptr<playback_event_source> source,
	bool start_paused, double seek_fraction, source_factory export_factory)
{
	if (!source || !std::isfinite(seek_fraction)) return false;
	return start({}, std::move(source), false, start_paused,
		std::clamp(seek_fraction, 0.0, 1.0), std::move(export_factory));
}

bool playback_session::start(std::wstring path, std::shared_ptr<playback_event_source> source,
	bool silent, bool start_paused, double seek_fraction, source_factory export_factory)
{
	if ((path.empty() && !source) || impl_->closing.load(std::memory_order_acquire) ||
		impl_->busy.load(std::memory_order_acquire) ||
		impl_->stop_busy.load(std::memory_order_acquire))
		return false;
	if (impl_->worker.joinable())
		impl_->worker.join();
	if (impl_->stop_worker.joinable())
		impl_->stop_worker.join();

	impl_->stopping = false;
	impl_->duration_us = 0;
	impl_->preparation_cancel.store(false, std::memory_order_release);
	{
		std::lock_guard lock(impl_->source_mutex);
		impl_->source = source;
		impl_->path = path;
		impl_->export_factory = std::move(export_factory);
		impl_->archive_members.clear();
		impl_->waiting_for_member = false;
	}
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
			 sound_bank = impl_->bank, synth = impl_->preferences, silent, start_paused,
			 source = std::move(source), seek_fraction]
			(std::stop_token token) mutable
			{
				state->run(token, std::move(path), output, std::move(sound_bank), synth, silent, start_paused,
					std::move(source), seek_fraction);
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
	std::unique_lock audition_lock(impl_->audition_mutex);
	if (impl_->stopping || !impl_->busy.load(std::memory_order_acquire))
		return;
	impl_->stopping = true;
	impl_->stop_busy.store(true, std::memory_order_release);
	impl_->worker.request_stop();
	impl_->engine.stop();
	audition_lock.unlock();
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
	if (!impl_) return;
	std::unique_lock audition_lock(impl_->audition_mutex);
	if (impl_->closing.exchange(true, std::memory_order_acq_rel)) return;
	impl_->stopping = true;
	impl_->worker.request_stop();
	audition_lock.unlock();
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
		std::lock_guard lock(impl_->source_mutex);
		result.waiting_for_member = impl_->waiting_for_member;
		result.archive_members = impl_->archive_members;
		result.archive_layer = impl_->archive_layer;
		if (auto compressed = std::dynamic_pointer_cast<compressed_midi_event_source>(impl_->source))
		{
			result.source_tracks = compressed->track_count();
			result.source_events = compressed->event_count();
			result.source_archive_depth = compressed->archive_depth();
		}
	}
	{
		std::lock_guard lock(impl_->status_mutex);
		result.message = impl_->message;
		result.error = impl_->error;
	}
	if (result.stopping)
		result.message = "Stopping...";
	else if (result.waiting_for_member)
		result.message = "Choose an archive member";
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

void playback_session::choose_archive_member(std::size_t index)
{
	std::lock_guard lock(impl_->source_mutex);
	if (!impl_->waiting_for_member || index >= impl_->archive_members.size()) return;
	impl_->chosen_member = index;
	impl_->member_changed.notify_all();
}

std::shared_ptr<playback_event_source> playback_session::current_source() const
{
	std::lock_guard lock(impl_->source_mutex);
	return impl_->source;
}

playback_session::source_factory playback_session::export_source_factory() const
{
	std::lock_guard lock(impl_->source_mutex);
	return impl_->export_factory;
}

std::wstring playback_session::current_path() const
{
	std::lock_guard lock(impl_->source_mutex);
	return impl_->path;
}

std::wstring playback_session::bank_path() const { return impl_->bank; }
syncore_preferences playback_session::synth_preferences() const { return impl_->preferences; }

bool playback_session::audition_note(std::uint8_t key, std::uint8_t velocity,
	std::uint8_t channel, bool on)
{
	std::lock_guard lock(impl_->audition_mutex);
	if (impl_->closing.load(std::memory_order_acquire) || impl_->stopping ||
		impl_->stop_busy.load(std::memory_order_acquire)) return false;
	key &= 0x7f;
	channel &= 0x0f;
	if (impl_->audition_preparing)
	{
		// Track the currently held key while a bank loads, so releasing it before
		// preparation finishes cannot leave a late note-on without its note-off.
		impl_->audition_keys[channel * 128 + key] = on ? velocity : 0;
		return true;
	}
	if (impl_->busy.load(std::memory_order_acquire) && !impl_->engine.is_playing()) return false;
	if (impl_->engine.has_output())
	{
		impl_->engine.preview_note(channel, key, velocity, on);
		return true;
	}
	if (!on || impl_->busy.load(std::memory_order_acquire)) return false;
	if (impl_->worker.joinable()) impl_->worker.join();
	if (impl_->stop_worker.joinable()) impl_->stop_worker.join();
	impl_->audition_keys.fill(0);
	impl_->audition_keys[channel * 128 + key] = velocity;
	impl_->audition_preparing = true;
	impl_->preparation_cancel.store(false, std::memory_order_release);
	impl_->busy.store(true, std::memory_order_release);
	impl_->set_status("Preparing editor audition...");
	try
	{
		const auto selected_name = impl_->device < impl_->devices.size()
			? impl_->devices[impl_->device] : std::string{};
		impl_->engine.init();
		impl_->devices = impl_->engine.get_device_names();
		const auto selected = std::find(impl_->devices.begin(), impl_->devices.end(), selected_name);
		impl_->device = selected != impl_->devices.end()
			? static_cast<std::size_t>(selected - impl_->devices.begin())
			: simple_player::syncore_available() ? impl_->engine.get_syncore_device_index()
				: impl_->engine.get_current_device();
		impl_->worker = std::jthread([self = impl_.get(), output = impl_->device,
			bank = impl_->bank, synth = impl_->preferences](std::stop_token token) mutable
		{
			self->run(token, {}, output, std::move(bank), synth, false, false, {}, 0, true);
		});
	}
	catch (const std::exception& error)
	{
		impl_->audition_preparing = false;
		impl_->busy.store(false, std::memory_order_release);
		impl_->set_status("Could not prepare editor audition", error.what());
		return false;
	}
	return true;
}

void playback_session::set_visual_options(bool simulated_lag, std::uint8_t overlap_mode)
{
	impl_->visuals.enable_simulated_lag = simulated_lag;
	impl_->visuals.remove_overlaps = overlap_mode <= 1 ? overlap_mode : 0xff;
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
	// Preserve the original viewport's 2^0..2^30 microsecond range. Round after
	// converting to double so a floating-point value near 1 us cannot become 0.
	const double window_us = std::isfinite(visible_seconds)
		? std::clamp(static_cast<double>(visible_seconds) * 1'000'000.0, 1.0, 1073741824.0)
		: 3'000'000.0;
	impl_->visuals.scroll_window_us = static_cast<std::uint64_t>(std::llround(window_us));
	std::string alerts;
	scoped_alert_sink sink(alerts);
	impl_->engine.draw_at(impl_->visuals, impl_->engine.get_visual_position_us());
	if (!alerts.empty())
		impl_->set_status("Visualization failed", std::move(alerts));
}
}
