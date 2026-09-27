#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

#include "video_export_panel.h"
#include "playback_session.h"
#include "folded_theme.h"
#include "../SAFC_InnerModules/playback_event_source.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace safc::imgui_ui
{
namespace
{
std::string display_path(const std::wstring& path)
{
	if (path.empty())
		return "Unsaved editor snapshot";
	const int size =
		WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), result.data(), size, nullptr, nullptr);
	return result;
}

float fraction(std::uint64_t value, std::uint64_t total)
{
	return total ? static_cast<float>(std::clamp(static_cast<double>(value) / total, 0.0, 1.0)) : 0.f;
}

bool midi_extension(const std::wstring& path)
{
	auto extension = std::filesystem::path(path).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return extension == L".mid" || extension == L".midi";
}

// Mirrors validate_settings() in simple_player_video_export.cpp, so invalid
// settings are reported before the save dialog instead of after it.
std::string settings_problem(const simple_player_video_settings& settings, const syncore_preferences& preferences)
{
	if (settings.width < 16 || settings.width > 8192 || settings.height < 16 || settings.height > 8192 ||
		(settings.width & 1U) || (settings.height & 1U))
		return "Width and height must be even values from 16 to 8192";
	if (settings.fps < 1 || settings.fps > 240)
		return "FPS must be from 1 to 240";
	if (settings.video_bitrate_kbps < 64 || settings.video_bitrate_kbps > 250000)
		return "Video bitrate must be from 64 to 250000 kbps";
	if (settings.audio_bitrate_kbps != 96 && settings.audio_bitrate_kbps != 128 &&
		settings.audio_bitrate_kbps != 160 && settings.audio_bitrate_kbps != 192)
		return "AAC bitrate must be 96, 128, 160, or 192 kbps";
	if (settings.audio_sample_rate != 44100 && settings.audio_sample_rate != 48000)
		return "AAC sample rate must be 44100 or 48000 Hz";
	if (!std::isfinite(settings.tail_seconds) || settings.tail_seconds < 0.0 || settings.tail_seconds > 60.0)
		return "Tail seconds must be from 0 to 60";
	if (!std::isfinite(settings.visible_seconds) || settings.visible_seconds < 0.01 || settings.visible_seconds > 60.0)
		return "Visible seconds must be from 0.01 to 60";
	if (preferences.sample_rate < 8000 || preferences.sample_rate > 192000 || preferences.buffer_frames < 256 ||
		preferences.buffer_frames > 1048576 || preferences.maximum_cohorts == 0 ||
		preferences.maximum_cohorts > 1048576 || preferences.render_threads > 64)
		return "Apply valid SYNCore settings before rendering";
	return {};
}
}

struct video_export_panel::impl
{
	playback_session& playback;
	native_dialogs dialogs;
	simple_player_video_settings settings;
	std::jthread worker;
	std::atomic_bool running{false};
	std::atomic_bool cancel_requested{false};
	bool closed = false;
	mutable std::mutex mutex;
	video_export_snapshot state;
	std::vector<std::uint8_t> pixels;
	std::uint32_t frame_width = 0;
	std::uint32_t frame_height = 0;
	std::uint64_t frame_serial = 0;
	GLuint texture = 0;
	std::uint64_t uploaded_serial = 0;
	std::uint32_t texture_width = 0;
	std::uint32_t texture_height = 0;

	impl(playback_session& service, native_dialogs native) : playback(service), dialogs(std::move(native))
	{
		state.status = "Ready";
	}

	static bool progress_callback(const simple_player_video_progress& progress, void* user) noexcept
	{
		auto& self = *static_cast<impl*>(user);
		try
		{
			std::lock_guard lock(self.mutex);
			self.state.progress = progress;
			self.state.progress.preview_bgra = nullptr;
			self.state.status = progress.stage;
			if (progress.preview_bgra && progress.preview_width && progress.preview_height &&
				progress.preview_stride >= static_cast<std::size_t>(progress.preview_width) * 4)
			{
				const auto row_bytes = static_cast<std::size_t>(progress.preview_width) * 4;
				self.pixels.resize(row_bytes * progress.preview_height);
				for (std::uint32_t y = 0; y < progress.preview_height; ++y)
					std::memcpy(self.pixels.data() + row_bytes * y,
						progress.preview_bgra + static_cast<std::size_t>(progress.preview_stride) * y, row_bytes);
				self.frame_width = progress.preview_width;
				self.frame_height = progress.preview_height;
				++self.frame_serial;
			}
			return !self.cancel_requested.load(std::memory_order_acquire);
		}
		catch (...)
		{
			self.cancel_requested.store(true, std::memory_order_release);
			return false;
		}
	}

	void upload_preview()
	{
		std::vector<std::uint8_t> frame;
		std::uint32_t width{}, height{};
		std::uint64_t serial{};
		{
			std::lock_guard lock(mutex);
			if (frame_serial == uploaded_serial)
				return;
			frame = pixels;
			width = frame_width;
			height = frame_height;
			serial = frame_serial;
		}
		if (frame.empty())
		{
			if (texture)
				glDeleteTextures(1, &texture);
			texture = 0;
			texture_width = texture_height = 0;
			uploaded_serial = serial;
			return;
		}
		GLint previous_texture{}, alignment{}, row_length{};
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
		if (!texture)
			glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
		glBindTexture(GL_TEXTURE_2D, previous_texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
		texture_width = width;
		texture_height = height;
		uploaded_serial = serial;
	}
	void draw_preview(const video_export_snapshot& current)
	{
		upload_preview();
		const auto path = current.busy ? current.source_path : playback.current_path();
		const bool has_source = current.busy || !path.empty() || playback.current_source();
		ImGui::TextWrapped("Source: %s", has_source ? display_path(path).c_str() : "none");
		if (texture && texture_width && texture_height)
		{
			// The preview is at most 320 px wide; never upscale it past the UI scale.
			const float scale = std::max(0.f, std::min(ImGui::GetContentRegionAvail().x / texture_width, ui_scale()));
			ImGui::Image(static_cast<ImTextureID>(texture), {texture_width * scale, texture_height * scale});
		}
		else
		{
			ImGui::BeginChild("Preview", {0, scaled(180)}, ImGuiChildFlags_Borders);
			ImGui::TextDisabled("Rendered frames appear here");
			ImGui::EndChild();
		}
	}

	void draw_progress(const video_export_snapshot& current)
	{
		ImGui::TextWrapped("%s", current.cancelling ? "Cancelling..." : current.status.c_str());
		ImGui::ProgressBar(
			fraction(current.progress.completed_frames, current.progress.total_frames), {-1, 0}, "Video");
		ImGui::ProgressBar(
			fraction(current.progress.completed_audio_frames, current.progress.total_audio_frames), {-1, 0}, "Audio");
		if (current.progress.total_frames)
			ImGui::Text("Frames %llu / %llu  |  Events %llu  |  Voices %llu", current.progress.completed_frames,
				current.progress.total_frames, current.progress.completed_events, current.progress.active_voices);
		if (!current.result.error.empty())
			ImGui::TextWrapped("%s", current.result.error.c_str());
		if (!current.result.warning.empty())
			ImGui::TextWrapped("%s", current.result.warning.c_str());
		if (!current.output_path.empty())
			ImGui::TextWrapped("Output: %s", display_path(current.output_path).c_str());
	}

	// Why a render cannot start from the current player source, or empty.
	static std::string source_problem(const std::shared_ptr<playback_event_source>& source,
		const playback_session::source_factory& factory, const std::wstring& path, const playback_snapshot& playback)
	{
		if (!simple_player_video_export_available())
			return "This build does not include SYNCore video export";
		if (source && !factory)
			return "This event source cannot create independent export readers; save it as MIDI first";
		if (!source && path.empty())
			return "No source - open a MIDI in the player or play from the editor";
		if (playback.busy && !playback.playing && !factory)
			return "Wait for source preparation before exporting";
		// A failed or cancelled archive leaves only its path; it is not a MIDI.
		if (!source && !factory && !midi_extension(path))
			return "Prepare the archive in the player first.";
		return {};
	}

	void draw_settings()
	{
		if (ImGui::BeginTable("Render settings", 2, ImGuiTableFlags_SizingStretchSame))
		{
			// Labels get rows of their own. A table carries the text baseline of a
			// cell's last line into the next cell of the row, so a right-hand label
			// would drop to its left neighbour's field text and shift the column down.
			const auto labels = [](const char* left, const char* right)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(left);
				if (right)
				{
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(right);
				}
				ImGui::TableNextRow();
			};
			const auto integer = [](const char* id, std::uint32_t& value)
			{
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputScalar(id, ImGuiDataType_U32, &value);
			};
			// The AAC encoder accepts only these values.
			const auto choice = [](const char* id, std::uint32_t& value, std::initializer_list<std::uint32_t> options)
			{
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				const auto preview = std::to_string(value);
				if (ImGui::BeginCombo(id, preview.c_str()))
				{
					for (const auto option : options)
					{
						const auto text = std::to_string(option);
						if (ImGui::Selectable(text.c_str(), option == value))
							value = option;
						if (option == value)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			};
			// Clamp only once editing ends, so typing is not rewritten mid-value.
			const auto seconds = [](const char* id, double& value, double step, double fast, double minimum)
			{
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				ImGui::InputDouble(id, &value, step, fast, "%.3f");
				if (!ImGui::IsItemActive())
					value = std::isfinite(value) ? std::clamp(value, minimum, 60.0) : minimum;
			};
			labels("Width", "Height");
			integer("##width", settings.width);
			integer("##height", settings.height);
			labels("FPS", "Video kbps");
			integer("##fps", settings.fps);
			integer("##video_kbps", settings.video_bitrate_kbps);
			labels("AAC kbps", "AAC Hz");
			choice("##aac_kbps", settings.audio_bitrate_kbps, {96, 128, 160, 192});
			choice("##aac_hz", settings.audio_sample_rate, {44100, 48000});
			labels("Visible seconds", "Tail seconds");
			seconds("##visible_seconds", settings.visible_seconds, .01, .1, .01);
			seconds("##tail_seconds", settings.tail_seconds, .5, 1, 0);
			labels("Overlaps", nullptr);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			int overlap = settings.remove_overlaps <= 1 ? settings.remove_overlaps : 2;
			if (ImGui::Combo("##overlaps", &overlap, "Naive removal\0Realtime removal\0Draw all\0"))
				settings.remove_overlaps = overlap == 2 ? 0xff : static_cast<std::uint8_t>(overlap);
			ImGui::EndTable();
		}
	}
};

video_export_panel::video_export_panel(playback_session& playback, native_dialogs dialogs)
	: impl_(std::make_unique<impl>(playback, std::move(dialogs)))
{
}
video_export_panel::~video_export_panel()
{
	shutdown();
}

simple_player_video_settings video_export_panel::settings() const
{
	return impl_->settings;
}
void video_export_panel::set_settings(const simple_player_video_settings& value)
{
	if (!impl_->running.load(std::memory_order_acquire))
		impl_->settings = value;
}

video_export_snapshot video_export_panel::snapshot() const
{
	std::lock_guard lock(impl_->mutex);
	auto result = impl_->state;
	result.busy = impl_->running.load(std::memory_order_acquire);
	result.cancelling = result.busy && impl_->cancel_requested.load(std::memory_order_acquire);
	return result;
}

bool video_export_panel::start_export(std::wstring output_path)
{
	if (impl_->closed || output_path.empty() || impl_->running.load(std::memory_order_acquire))
		return false;
	if (impl_->worker.joinable())
		impl_->worker.join();
	const auto source = impl_->playback.current_source();
	auto factory = impl_->playback.export_source_factory();
	auto source_path = impl_->playback.current_path();
	const auto playback = impl_->playback.snapshot();
	auto unavailable = impl_->source_problem(source, factory, source_path, playback);
	if (unavailable.empty())
		unavailable = settings_problem(impl_->settings, impl_->playback.synth_preferences());
	if (!unavailable.empty())
	{
		std::lock_guard lock(impl_->mutex);
		impl_->state.status = std::move(unavailable);
		return false;
	}
	// Prepared editor readers are virtual; the renderer uses this descriptor
	// only for source/output collision checks and never reads it from disk.
	if (source_path.empty())
		source_path = L"Unsaved editor.mid";
	const auto bank = impl_->playback.bank_path();
	const auto synth = impl_->playback.synth_preferences();
	const auto settings = impl_->settings;
	{
		std::lock_guard lock(impl_->mutex);
		impl_->state = {};
		impl_->state.status = "Starting render...";
		impl_->state.source_path = source_path;
		impl_->state.output_path = output_path;
		impl_->pixels.clear();
		++impl_->frame_serial;
	}
	impl_->cancel_requested.store(false, std::memory_order_release);
	impl_->running.store(true, std::memory_order_release);
	try
	{
		impl_->worker = std::jthread(
			[self = impl_.get(), output_path = std::move(output_path), source_path = std::move(source_path),
				factory = std::move(factory), bank, synth, settings, total_events = playback.source_events]() mutable
		{
			simple_player_video_result result;
			try
			{
				if (factory)
				{
					auto audio = factory();
					auto video = factory();
					if (!audio || !video || audio.get() == video.get())
						throw std::runtime_error("Export requires two independent event readers");
					result = render_simple_player_video_events(source_path, *audio, *video, total_events, output_path,
						bank, synth, settings, &self->cancel_requested, &impl::progress_callback, self);
				}
				else
					result = render_simple_player_video(source_path, output_path, bank, synth, settings,
						&self->cancel_requested, &impl::progress_callback, self);
			}
			catch (const std::exception& error)
			{
				result.error = error.what();
			}
			catch (...)
			{
				result.error = "Unexpected export failure";
			}
			{
				std::lock_guard lock(self->mutex);
				self->state.result = result;
				if (result.ok)
					self->state.status = "MP4 saved";
				else if (result.cancelled)
					self->state.status = "Render cancelled";
				else
					self->state.status = "Render failed";
			}
			self->running.store(false, std::memory_order_release);
		});
	}
	catch (const std::exception& error)
	{
		impl_->running.store(false, std::memory_order_release);
		std::lock_guard lock(impl_->mutex);
		impl_->state.status = "Could not start render";
		impl_->state.result.error = error.what();
		return false;
	}
	return true;
}

void video_export_panel::cancel()
{
	impl_->cancel_requested.store(true, std::memory_order_release);
}

void video_export_panel::shutdown()
{
	if (!impl_ || impl_->closed)
		return;
	impl_->closed = true;
	cancel();
	if (impl_->worker.joinable())
		impl_->worker.join();
	if (impl_->texture)
		glDeleteTextures(1, &impl_->texture);
	impl_->texture = 0;
}

void video_export_panel::draw(bool* open)
{
	if (impl_->closed || (open && !*open))
		return;
	ImGui::SetNextWindowPos({scaled(90), scaled(86)}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(
		{scaled(600), std::max(scaled(420), std::min(scaled(730), ImGui::GetIO().DisplaySize.y - scaled(116)))},
		ImGuiCond_FirstUseEver);
	if (begin_folded_window("Player video render", open))
	{
		const auto current = snapshot();

		impl_->draw_preview(current);
		impl_->draw_progress(current);
		ImGui::Separator();
		ImGui::BeginDisabled(current.busy);
		impl_->draw_settings();
		ImGui::TextWrapped("Uses applied SYNCore settings. Audio and video render independently from playback.");
		// Validate before the save dialog, so it is never shown for a render that cannot start.
		std::string problem;
		if (!current.busy)
		{
			problem = impl_->source_problem(impl_->playback.current_source(),
				impl_->playback.export_source_factory(), impl_->playback.current_path(), impl_->playback.snapshot());
			if (problem.empty())
				problem = settings_problem(impl_->settings, impl_->playback.synth_preferences());
		}
		ImGui::BeginDisabled(!problem.empty());
		if (ImGui::Button("Render MP4", {scaled(150), 0}) && impl_->dialogs.save_video)
		{
			auto suggestion = std::filesystem::path(impl_->playback.current_path());
			if (suggestion.empty())
				suggestion = "Editor.mp4";
			suggestion.replace_extension(".mp4");
			try
			{
				auto destination = impl_->dialogs.save_video(suggestion.wstring());
				if (!destination.empty())
					start_export(std::move(destination));
			}
			catch (const std::exception& error)
			{
				std::lock_guard lock(impl_->mutex);
				impl_->state.status = error.what();
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!current.busy || current.cancelling);
		if (ImGui::Button("Cancel render"))
			cancel();
		ImGui::EndDisabled();
		if (!problem.empty())
		{
			ImGui::PushTextWrapPos(0);
			ImGui::TextDisabled("%s", problem.c_str());
			ImGui::PopTextWrapPos();
		}
	}
	end_folded_window();
}
}
