#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <string>
#include <type_traits>
#include <vector>

#include "../WinRegWrappers.h"
#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"
#include "../SAFC_InnerModules/simple_player.h"
#include "../SAFC_InnerModules/simple_player_video_export.h"
#include "player_video_render_ui.h"
#include "simple_player_viewer.h"

#include <background_worker.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

extern syncore_preferences saved_syncore_preferences;
extern std::atomic<bool> compressed_player_source_selected;
std::shared_ptr<compressed_midi_event_source> current_compressed_player_source();
std::wstring current_compressed_player_filename();

namespace
{
using dixelu::background_worker_shutdown;
using dixelu::worker_singleton;

simple_player_video_settings saved_settings{};
simple_player_video_settings draft_settings{};
std::atomic_bool render_running{false};
std::atomic_bool render_cancel{false};

template<typename Part>
Part* render_ui(const char* element)
	requires std::is_base_of_v<handleable_ui_part, Part>
{
	if (!global_window_handler)
		return nullptr;
	auto window = (*global_window_handler)["PLAYER_RENDER_SETTINGS"];
	if (!window)
		return nullptr;
	handleable_ui_part* part = ((*window)[element]);
	return dynamic_cast<Part*>(part);
}

struct preview_state
{
	std::mutex lock;
	simple_player_video_progress progress;
	std::vector<std::uint8_t> pixels;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint64_t frame_serial = 0;
	std::string status_text = "Ready";
	std::uint64_t status_serial = 1;

	void reset()
	{
		std::lock_guard locker(lock);
		progress = {};
		pixels.clear();
		width = 0;
		height = 0;
		++frame_serial;
	}

	void update(const simple_player_video_progress& value)
	{
		std::lock_guard locker(lock);
		progress = value;
		progress.preview_bgra = nullptr;
		if (!value.preview_bgra || value.preview_width == 0 ||
			value.preview_height == 0 || value.preview_stride == 0)
			return;

		width = value.preview_width;
		height = value.preview_height;
		pixels.resize(static_cast<std::size_t>(width) * height * 4);
		const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
		for (std::uint32_t y = 0; y < height; ++y)
		{
			const auto* source = value.preview_bgra +
				static_cast<std::size_t>(y) * value.preview_stride;
			auto* destination = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
			std::copy(source, source + row_bytes, destination);
		}
		++frame_serial;
	}

	void set_status(std::string value)
	{
		std::lock_guard locker(lock);
		if (status_text == value)
			return;
		status_text = std::move(value);
		++status_serial;
	}
};

preview_state preview;

struct render_status_pane final : text_box
{
	using text_box::text_box;
	std::uint64_t applied_status_serial = 0;

	void draw() override
	{
		std::string pending_text;
		std::uint64_t pending_serial = 0;
		{
			std::lock_guard preview_lock(preview.lock);
			pending_serial = preview.status_serial;
			if (pending_serial != applied_status_serial)
				pending_text = preview.status_text;
		}
		if (pending_serial != applied_status_serial)
		{
			safe_string_replace(std::move(pending_text));
			applied_status_serial = pending_serial;
		}
		text_box::draw();
	}
};

struct preview_pane final : handleable_ui_part
{
	float x_pos;
	float y_pos;
	float width;
	float height;
	GLuint texture = 0;
	std::uint32_t texture_width = 0;
	std::uint32_t texture_height = 0;
	std::uint64_t uploaded_frame_serial = 0;

	preview_pane(float x, float y, float w, float h)
		: x_pos(x), y_pos(y), width(w), height(h) {}

	~preview_pane() override
	{
		if (texture)
			glDeleteTextures(1, &texture);
	}

	static void draw_rect(float left, float right, float bottom, float top,
		std::uint32_t color)
	{
		__glcolor(color);
		glBegin(GL_QUADS);
		glVertex2f(left, top);
		glVertex2f(right, top);
		glVertex2f(right, bottom);
		glVertex2f(left, bottom);
		glEnd();
	}

	static float fraction(std::uint64_t value, std::uint64_t total)
	{
		if (total == 0)
			return 0.0f;
		return std::clamp(static_cast<float>(
			static_cast<double>(value) / total), 0.0f, 1.0f);
	}

	static void draw_progress_bar(float left, float right, float y, float bar_height,
		float filled, std::uint32_t color)
	{
		draw_rect(left, right, y, y + bar_height, 0x101820BF);
		draw_rect(left, left + (right - left) * filled, y, y + bar_height, color);
		__glcolor(0xFFFFFF4F);
		glLineWidth(1.0f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(left, y + bar_height);
		glVertex2f(right, y + bar_height);
		glVertex2f(right, y);
		glVertex2f(left, y);
		glEnd();
	}

	void upload_frame(const std::vector<std::uint8_t>& frame,
		std::uint32_t frame_width, std::uint32_t frame_height,
		std::uint64_t frame_serial)
	{
		if (frame.empty())
		{
			if (texture)
			{
				glDeleteTextures(1, &texture);
				texture = 0;
			}
			texture_width = 0;
			texture_height = 0;
			uploaded_frame_serial = frame_serial;
			return;
		}

		if (!texture)
			glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		if (texture_width != frame_width || texture_height != frame_height)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height,
				0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
			texture_width = frame_width;
			texture_height = frame_height;
		}
		else
		{
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_width, frame_height,
				GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
		}
		uploaded_frame_serial = frame_serial;
	}

	void draw() override
	{
		std::vector<std::uint8_t> frame;
		std::uint32_t frame_width = 0;
		std::uint32_t frame_height = 0;
		std::uint64_t frame_serial = 0;
		simple_player_video_progress progress;
		{
			std::lock_guard preview_lock(preview.lock);
			progress = preview.progress;
			frame_serial = preview.frame_serial;
			if (frame_serial != uploaded_frame_serial)
			{
				frame = preview.pixels;
				frame_width = preview.width;
				frame_height = preview.height;
			}
		}

		std::lock_guard locker(lock);
		const float left = x_pos - width * 0.5f;
		const float right = x_pos + width * 0.5f;
		const float bottom = y_pos - height * 0.5f;
		const float top = y_pos + height * 0.5f;
		draw_rect(left, right, bottom, top, 0x07121FEF);
		if (frame_serial != uploaded_frame_serial)
			upload_frame(frame, frame_width, frame_height, frame_serial);

		if (texture && texture_width && texture_height)
		{
			const float pad = 3.0f;
			const float image_left = left + pad;
			const float image_right = right - pad;
			const float image_bottom = bottom + 10.0f;
			const float image_top = top - pad;
			const float source_aspect = static_cast<float>(texture_width) / texture_height;
			float draw_width = image_right - image_left;
			float draw_height = draw_width / source_aspect;
			if (draw_height > image_top - image_bottom)
			{
				draw_height = image_top - image_bottom;
				draw_width = draw_height * source_aspect;
			}
			const float cx = (image_left + image_right) * 0.5f;
			const float cy = (image_bottom + image_top) * 0.5f;
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, texture);
			glColor4ub(255, 255, 255, 255);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(cx - draw_width * 0.5f, cy + draw_height * 0.5f);
			glTexCoord2f(1.0f, 0.0f); glVertex2f(cx + draw_width * 0.5f, cy + draw_height * 0.5f);
			glTexCoord2f(1.0f, 1.0f); glVertex2f(cx + draw_width * 0.5f, cy - draw_height * 0.5f);
			glTexCoord2f(0.0f, 1.0f); glVertex2f(cx - draw_width * 0.5f, cy - draw_height * 0.5f);
			glEnd();
			glDisable(GL_TEXTURE_2D);
		}

		const float bar_left = left + 4.0f;
		const float bar_right = right - 4.0f;
		draw_progress_bar(bar_left, bar_right, bottom + 2.5f, 2.0f,
			fraction(progress.completed_audio_frames, progress.total_audio_frames),
			0xFFD24DFF);
		draw_progress_bar(bar_left, bar_right, bottom + 5.5f, 2.0f,
			fraction(progress.completed_frames, progress.total_frames), 0x20D0FFFF);
		__glcolor(0xFFFFFF7F);
		glLineWidth(1.0f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(left, top);
		glVertex2f(right, top);
		glVertex2f(right, bottom);
		glVertex2f(left, bottom);
		glEnd();
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
	}

	void safe_change_position(float x, float y) override
	{
		std::lock_guard locker(lock);
		x_pos = x;
		y_pos = y;
	}

	void safe_change_position_argumented(std::uint8_t argument, float x, float y) override
	{
		const float centered_width = 0.5f * (
			(static_cast<std::int32_t>(static_cast<bool>(GLOBAL_LEFT & argument)) -
			static_cast<std::int32_t>(static_cast<bool>(GLOBAL_RIGHT & argument)))) * width;
		const float centered_height = 0.5f * (
			(static_cast<std::int32_t>(static_cast<bool>(GLOBAL_BOTTOM & argument)) -
			static_cast<std::int32_t>(static_cast<bool>(GLOBAL_TOP & argument)))) * height;
		safe_change_position(x + centered_width, y + centered_height);
	}

	void safe_string_replace(std::string) override {}
	void keyboard_handler(char) override {}
	[[nodiscard]] bool mouse_handler(float, float, char, char) override { return false; }
};

void status(const std::string& message)
{
	preview.set_status(message);
}

std::wstring save_dialog()
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hWnd;
	dialog.lpstrFilter = L"MP4 video files(*.mp4)\0*.mp4\0";
	dialog.lpstrDefExt = L"mp4";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = MAX_PATH;
	dialog.lpstrTitle = L"Render MIDI player video to...";
	dialog.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOREADONLYRETURN |
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	dialog.nFilterIndex = 1;
	if (!GetSaveFileNameW(&dialog))
		return {};
	std::filesystem::path path(filename);
	if (path.extension().empty())
		path.replace_extension(L".mp4");
	return path.wstring();
}

std::string one_decimal(double value) { return std::format("{:.1f}", value); }

const char* render_phase_name(syncore_phase_mode mode)
{
	switch (mode)
	{
	case syncore_phase_mode::coherent: return "Coherent";
	case syncore_phase_mode::random_polarity: return "Random polarity";
	case syncore_phase_mode::analytic: return "Analytic";
	case syncore_phase_mode::smooth_field: return "Smooth field";
	case syncore_phase_mode::independent_bins: return "Independent bins";
	}
	return "Unknown phase";
}

std::string syncore_summary()
{
	const auto& synth = saved_syncore_preferences;
	std::string bank_name = "built-in sine";
	if (!saved_syncore_bank_path.empty())
	{
		const auto filename = std::filesystem::path(saved_syncore_bank_path).filename().wstring();
		bank_name.clear();
		bank_name.reserve(filename.size());
		for (const auto character : filename)
			bank_name.push_back(character >= 32 && character < 127
				? static_cast<char>(character) : '?');
	}
	return std::format(
		"SYNCore: {}\n{} Hz | {} buffer | {} cohorts | {} threads\n"
		"{} | {:+.1f} dB | limiter {}",
		bank_name, synth.sample_rate, synth.buffer_frames, synth.maximum_cohorts,
		synth.render_threads == 0 ? "auto" : std::to_string(synth.render_threads),
		render_phase_name(synth.phase_mode), synth.output_gain_db,
		synth.limiter_enabled ? "on" : "off");
}

void refresh_controls()
{
	if (!global_window_handler || !(*global_window_handler)["PLAYER_RENDER_SETTINGS"])
		return;
	if (auto field = render_ui<input_field>("WIDTH"))
		field->safe_string_replace(std::to_string(draft_settings.width));
	if (auto field = render_ui<input_field>("HEIGHT"))
		field->safe_string_replace(std::to_string(draft_settings.height));
	if (auto field = render_ui<input_field>("FPS"))
		field->safe_string_replace(std::to_string(draft_settings.fps));
	if (auto field = render_ui<input_field>("VIDEO_KBPS"))
		field->safe_string_replace(std::to_string(draft_settings.video_bitrate_kbps));
	if (auto field = render_ui<input_field>("AUDIO_KBPS"))
		field->safe_string_replace(std::to_string(draft_settings.audio_bitrate_kbps));
	if (auto field = render_ui<input_field>("AUDIO_RATE"))
		field->safe_string_replace(std::to_string(draft_settings.audio_sample_rate));
	if (auto field = render_ui<input_field>("TAIL_SECONDS"))
		field->safe_string_replace(one_decimal(draft_settings.tail_seconds));
	if (auto field = render_ui<input_field>("VISIBLE_SECONDS"))
		field->safe_string_replace(one_decimal(draft_settings.visible_seconds));
	if (auto summary = render_ui<text_box>("SYNCORE_SUMMARY"))
		summary->safe_string_replace(syncore_summary());
	status(render_running.load(std::memory_order_acquire) ? "Rendering..." : "Ready");
}

void persist_settings()
{
	WinReg::RegKey registry;
	registry.Open(HKEY_CURRENT_USER, default_reg_path);
	registry.SetDwordValue(L"PLAYER_RENDER_WIDTH", saved_settings.width);
	registry.SetDwordValue(L"PLAYER_RENDER_HEIGHT", saved_settings.height);
	registry.SetDwordValue(L"PLAYER_RENDER_FPS", saved_settings.fps);
	registry.SetDwordValue(L"PLAYER_RENDER_VIDEO_KBPS", saved_settings.video_bitrate_kbps);
	registry.SetDwordValue(L"PLAYER_RENDER_AUDIO_KBPS", saved_settings.audio_bitrate_kbps);
	registry.SetDwordValue(L"PLAYER_RENDER_AUDIO_RATE", saved_settings.audio_sample_rate);
	registry.SetStringValue(L"PLAYER_RENDER_TAIL_SECONDS",
		std::to_wstring(saved_settings.tail_seconds));
	registry.SetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS",
		std::to_wstring(saved_settings.visible_seconds));
	registry.Close();
}

bool read_controls(simple_player_video_settings& settings)
{
	try
	{
		settings.width = std::stoul(render_ui<input_field>("WIDTH")->get_current_input("1280"));
		settings.height = std::stoul(render_ui<input_field>("HEIGHT")->get_current_input("720"));
		settings.fps = std::stoul(render_ui<input_field>("FPS")->get_current_input("60"));
		settings.video_bitrate_kbps = std::stoul(
			render_ui<input_field>("VIDEO_KBPS")->get_current_input("12000"));
		settings.audio_bitrate_kbps = std::stoul(
			render_ui<input_field>("AUDIO_KBPS")->get_current_input("192"));
		settings.audio_sample_rate = std::stoul(
			render_ui<input_field>("AUDIO_RATE")->get_current_input("48000"));
		settings.tail_seconds = std::stod(
			render_ui<input_field>("TAIL_SECONDS")->get_current_input("2.0"));
		settings.visible_seconds = std::stod(
			render_ui<input_field>("VISIBLE_SECONDS")->get_current_input("4.2"));
	}
	catch (...)
	{
		throw_alert_warning("One or more video render settings are not valid numbers");
		return false;
	}

	const bool audio_bitrate_valid = settings.audio_bitrate_kbps == 96 ||
		settings.audio_bitrate_kbps == 128 || settings.audio_bitrate_kbps == 160 ||
		settings.audio_bitrate_kbps == 192;
	if (settings.width < 16 || settings.width > 8192 ||
		settings.height < 16 || settings.height > 8192 ||
		(settings.width & 1U) || (settings.height & 1U) ||
		settings.fps < 1 || settings.fps > 240 ||
		settings.video_bitrate_kbps < 64 || settings.video_bitrate_kbps > 250000 ||
		!audio_bitrate_valid ||
		(settings.audio_sample_rate != 44100 && settings.audio_sample_rate != 48000) ||
		settings.tail_seconds < 0.0 || settings.tail_seconds > 60.0 ||
		settings.visible_seconds < 0.25 || settings.visible_seconds > 60.0)
	{
		throw_alert_warning(
			"Render ranges: even size 16-8192, FPS 1-240, video 64-250000 kbps, "
			"AAC 96/128/160/192 kbps at 44100/48000 Hz, "
			"tail 0-60s, visible 0.25-60s");
		return false;
	}
	return true;
}

void apply_settings()
{
	simple_player_video_settings value;
	if (!read_controls(value))
		return;
	saved_settings = value;
	draft_settings = value;
	try
	{
		persist_settings();
		status("Settings saved");
	}
	catch (...)
	{
		throw_alert_warning("Unable to save video render settings");
	}
}

void cancel_render()
{
	if (!render_running.load(std::memory_order_acquire))
	{
		status("No active render");
		return;
	}
	render_cancel.store(true, std::memory_order_release);
	status("Cancelling...");
}

std::string progress_text(const simple_player_video_progress& progress)
{
	std::ostringstream text;
	text << progress.stage;
	std::ostringstream completion;
	auto append_completion = [&](const char* name, std::uint64_t value,
		std::uint64_t total)
	{
		if (total == 0)
			return;
		if (completion.tellp() > 0)
			completion << " | ";
		completion << name << ' ' << std::format("{:.1f}", 100.0 * value / total) << '%';
	};
	append_completion("Audio", progress.completed_audio_frames,
		progress.total_audio_frames);
	append_completion("Video", progress.completed_frames, progress.total_frames);
	append_completion("Events", progress.completed_events, progress.total_events);
	if (completion.tellp() > 0)
		text << '\n' << completion.str();

	std::ostringstream synthesis;
	if (progress.total_audio_frames != 0)
	{
		if (progress.audio_render_threads != 0)
		{
			synthesis << "Threads ";
			if (progress.requested_audio_render_threads == 0)
				synthesis << "auto->";
			synthesis << progress.audio_render_threads << " | Voices " <<
				progress.active_voices << '/' << progress.peak_active_voices;
		}
		if (progress.peak_active_cohorts != 0 || progress.active_cohorts != 0)
		{
			if (synthesis.tellp() > 0)
				synthesis << " | ";
			synthesis << "Cohorts " << progress.active_cohorts << '/' <<
				progress.peak_active_cohorts;
			if (progress.cohort_capacity_steals != 0)
				synthesis << " | Steals " << progress.cohort_capacity_steals;
		}
	}
	if (synthesis.tellp() > 0)
		text << '\n' << synthesis.str();
	if (progress.parallel_render_calls != 0)
	{
		text << "\nParallel " << progress.parallel_render_calls << " calls | " <<
			progress.parallel_rendered_frames << " frames";
	}
	return text.str();
}

bool on_progress(const simple_player_video_progress& value, void*) noexcept
{
	try
	{
		preview.update(value);
		status(progress_text(value));
	}
	catch (...) {}
	return !render_cancel.load(std::memory_order_acquire);
}

void start_render()
{
	if (!simple_player_video_export_available())
	{
		throw_alert_warning("This SAFC build does not include embedded SYNCore");
		return;
	}

	simple_player_video_settings settings;
	if (!read_controls(settings))
		return;
	saved_settings = settings;
	draft_settings = settings;
	try { persist_settings(); }
	catch (...) { std::cout << "Exception thrown while saving player render settings\n"; }

	std::wstring filename;
	std::shared_ptr<compressed_midi_event_source> audio_events;
	std::shared_ptr<compressed_midi_event_source> visual_events;
	if (compressed_player_source_selected.load(std::memory_order_acquire))
	{
		auto source = current_compressed_player_source();
		filename = current_compressed_player_filename();
		if (!source || filename.empty())
		{
			throw_alert_warning("The prepared MIDI source is no longer available");
			return;
		}
		try
		{
			audio_events = source->fork_reader();
			visual_events = source->fork_reader();
		}
		catch (const std::exception& error)
		{
			throw_alert_warning("Unable to open prepared MIDI render cursors: " +
				std::string(error.what()));
			return;
		}
	}
	else
	{
		filename = player ? player->get_filename() : std::wstring{};
		const auto extension = std::filesystem::path(filename).extension().wstring();
		if (filename.empty() || (_wcsicmp(extension.c_str(), L".mid") != 0 &&
			_wcsicmp(extension.c_str(), L".midi") != 0))
		{
			throw_alert_warning("Open a MIDI file in the player before rendering video");
			return;
		}
	}

	bool expected = false;
	if (!render_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		status("A render is already running");
		return;
	}
	const auto output_path = save_dialog();
	if (output_path.empty())
	{
		render_running.store(false, std::memory_order_release);
		return;
	}

	render_cancel.store(false, std::memory_order_release);
	preview.reset();
	status("Starting render...");
	const auto bank_path = saved_syncore_bank_path;
	const auto synth_preferences = saved_syncore_preferences;
	// Snapshot the player's current mode for both the preview and exported frames.
	if (auto window = (*global_window_handler)["SIMPLAYER"])
	{
		handleable_ui_part* part = (*window)["VIEW"];
		if (auto view = dynamic_cast<player_viewer*>(part); view && view->data)
			settings.remove_overlaps = view->data->remove_overlaps;
	}
	const auto accepted = worker_singleton<struct player_video_render_worker>::instance().push(
		[filename, output_path, bank_path, synth_preferences, settings,
			audio_events, visual_events](std::stop_token stop_token)
		{
			std::stop_callback stopping(stop_token, []()
			{
				render_cancel.store(true, std::memory_order_release);
			});
			struct running_guard
			{
				~running_guard() { render_running.store(false, std::memory_order_release); }
			} guard;

			simple_player_video_result result;
			if (audio_events && visual_events)
			{
				result = render_simple_player_video_events(filename, *audio_events,
					*visual_events, audio_events->event_count(), output_path, bank_path,
					synth_preferences, settings, &render_cancel, on_progress, nullptr);
			}
			else
			{
				result = render_simple_player_video(filename, output_path, bank_path,
					synth_preferences, settings, &render_cancel, on_progress, nullptr);
			}

			if (result.ok)
			{
				auto message = std::format(
					"Done\nVideo frames: {}\nAudio frames: {}",
					result.video_frames, result.audio_frames);
				if (!result.warning.empty())
					message += "\nWarning: " + result.warning;
				status(message);
			}
			else if (result.cancelled)
				status("Render cancelled");
			else
				status("Render failed\n" + result.error);
		});
	if (accepted != dixelu::background_worker_submit_result::accepted)
	{
		render_running.store(false, std::memory_order_release);
		status("Render worker is shutting down");
	}
}
}

void load_player_video_render_settings()
{
	try
	{
		WinReg::RegKey registry;
		registry.Open(HKEY_CURRENT_USER, default_reg_path);
		auto load_dword = [&](const wchar_t* name, std::uint32_t& destination,
			std::uint32_t minimum, std::uint32_t maximum)
		{
			try
			{
				const auto value = registry.GetDwordValue(name);
				if (value >= minimum && value <= maximum)
					destination = value;
			}
			catch (...) {}
		};
		load_dword(L"PLAYER_RENDER_WIDTH", saved_settings.width, 16, 8192);
		load_dword(L"PLAYER_RENDER_HEIGHT", saved_settings.height, 16, 8192);
		load_dword(L"PLAYER_RENDER_FPS", saved_settings.fps, 1, 240);
		load_dword(L"PLAYER_RENDER_VIDEO_KBPS", saved_settings.video_bitrate_kbps, 64, 250000);
		load_dword(L"PLAYER_RENDER_AUDIO_KBPS", saved_settings.audio_bitrate_kbps, 96, 192);
		load_dword(L"PLAYER_RENDER_AUDIO_RATE", saved_settings.audio_sample_rate, 44100, 48000);
		try
		{
			const auto value = std::stod(registry.GetStringValue(L"PLAYER_RENDER_TAIL_SECONDS"));
			if (value >= 0.0 && value <= 60.0)
				saved_settings.tail_seconds = value;
		}
		catch (...) {}
		try
		{
			const auto value = std::stod(registry.GetStringValue(L"PLAYER_RENDER_VISIBLE_SECONDS"));
			if (value >= 0.25 && value <= 60.0)
				saved_settings.visible_seconds = value;
		}
		catch (...) {}
		registry.Close();
	}
	catch (...) {}

	if ((saved_settings.width & 1U) != 0)
		++saved_settings.width;
	if ((saved_settings.height & 1U) != 0)
		++saved_settings.height;
	if (saved_settings.audio_bitrate_kbps != 96 &&
		saved_settings.audio_bitrate_kbps != 128 &&
		saved_settings.audio_bitrate_kbps != 160 &&
		saved_settings.audio_bitrate_kbps != 192)
		saved_settings.audio_bitrate_kbps = 192;
	if (saved_settings.audio_sample_rate != 44100 && saved_settings.audio_sample_rate != 48000)
		saved_settings.audio_sample_rate = 48000;
	draft_settings = saved_settings;
}

void initialize_player_video_render_window(
	unsigned background_color, unsigned header_color, unsigned border_color)
{
	auto window = new moveable_fui_window("Player video render", system_white,
		-170, 175 + moveable_window::window_header_size,
		340, 340, 200, 2.5, 45, 45, 2.5,
		background_color, header_color, border_color);
	window->on_close = []() { cancel_render(); };
	(*window)["PREVIEW"] = new preview_pane(0, 120, 310, 100);
	(*window)["STATUS"] = new render_status_pane(
		"Ready", system_white, 0, 48, 36, 310, 7, 0xFFFFFF0A, 0x007FFF5F, 1,
		_Align(center | top), text_box::VerticalOverflow::recalibrate);

	auto add_field = [&](const char* key, const char* label, const char* value,
		float label_x, float input_x, float y, input_field::Type type,
		std::uint32_t max_chars)
	{
		(*window)[std::string(key) + "_LABEL"] = new text_box(
			label, system_white, label_x, y, 10, 70, 7, 0, 0, 0,
			_Align::right, text_box::VerticalOverflow::cut);
		(*window)[key] = new input_field(value, input_x, y, 10, 55, system_white,
			nullptr, 0x007FFFFF, nullptr, " ", max_chars,
			_Align::center, _Align::center, type);
	};
	add_field("WIDTH", "Width", "1280", -125, -60, 22,
		input_field::Type::NaturalNumbers, 4);
	add_field("HEIGHT", "Height", "720", -125, -60, 5,
		input_field::Type::NaturalNumbers, 4);
	add_field("FPS", "FPS", "60", -125, -60, -12,
		input_field::Type::NaturalNumbers, 3);
	add_field("VISIBLE_SECONDS", "Visible sec", "4.2", -125, -60, -29,
		input_field::Type::FP_PositiveNumbers, 5);
	add_field("VIDEO_KBPS", "Video kbps", "12000", 45, 110, 22,
		input_field::Type::NaturalNumbers, 6);
	add_field("AUDIO_KBPS", "AAC kbps", "192", 45, 110, 5,
		input_field::Type::NaturalNumbers, 3);
	add_field("AUDIO_RATE", "AAC Hz", "48000", 45, 110, -12,
		input_field::Type::NaturalNumbers, 5);
	add_field("TAIL_SECONDS", "Tail sec", "2.0", 45, 110, -29,
		input_field::Type::FP_PositiveNumbers, 5);
	(*window)["SYNCORE_SUMMARY"] = new text_box(
		syncore_summary(), legacy_white, 0, -60, 30, 310, 7, 0, 0, 0,
		_Align::center, text_box::VerticalOverflow::cut);
	(*window)["RENDER"] = new button(
		"Render MP4", system_white, start_render, -82.5, -98, 75, 10, 1,
		0x7F3FFF3F, 0x7F3FFFFF, 0xFFFFFFFF, 0x7F3FFFFF, 0xFFFFFFFF,
		nullptr, "Render the current MIDI or prepared archive to MP4");
	(*window)["APPLY"] = new button(
		"Apply", system_white, apply_settings, 7.5, -98, 55, 10, 1,
		0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF,
		nullptr, "Save render settings");
	(*window)["CANCEL"] = new button(
		"Cancel", system_white, cancel_render, 77.5, -98, 55, 10, 1,
		0x5F5F5F7F, 0xFFFFFFFF, 0x7F7F7FFF, 0x7F7F7FFF, 0xFFFFFFFF,
		nullptr, "Cancel active render");
	(*global_window_handler)["PLAYER_RENDER_SETTINGS"] = window;
	refresh_controls();
}

void open_player_video_render_settings()
{
	draft_settings = saved_settings;
	refresh_controls();
	global_window_handler->enable_window("PLAYER_RENDER_SETTINGS");
}

void shutdown_player_video_render()
{
	render_cancel.store(true, std::memory_order_release);
	worker_singleton<struct player_video_render_worker>::instance().shutdown(
		background_worker_shutdown::cancel);
	render_running.store(false, std::memory_order_release);
}
