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
    if (path.empty()) return "Unsaved editor snapshot";
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.data(),
        static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

float fraction(std::uint64_t value, std::uint64_t total)
{
    return total ? static_cast<float>(std::clamp(static_cast<double>(value) / total, 0.0, 1.0)) : 0.f;
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

    impl(playback_session& service, native_dialogs native)
        : playback(service), dialogs(std::move(native)) { state.status = "Ready"; }

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
            if (frame_serial == uploaded_serial) return;
            frame = pixels;
            width = frame_width;
            height = frame_height;
            serial = frame_serial;
        }
        if (frame.empty())
        {
            if (texture) glDeleteTextures(1, &texture);
            texture = 0;
            texture_width = texture_height = 0;
            uploaded_serial = serial;
            return;
        }
        GLint previous_texture{}, alignment{}, row_length{};
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        if (!texture) glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
        glBindTexture(GL_TEXTURE_2D, previous_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        texture_width = width;
        texture_height = height;
        uploaded_serial = serial;
    }
};

video_export_panel::video_export_panel(playback_session& playback, native_dialogs dialogs)
    : impl_(std::make_unique<impl>(playback, std::move(dialogs))) {}
video_export_panel::~video_export_panel() { shutdown(); }

simple_player_video_settings video_export_panel::settings() const { return impl_->settings; }
void video_export_panel::set_settings(const simple_player_video_settings& value)
{
    if (!impl_->running.load(std::memory_order_acquire)) impl_->settings = value;
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
    if (impl_->closed || output_path.empty() || impl_->running.load(std::memory_order_acquire)) return false;
    if (impl_->worker.joinable()) impl_->worker.join();
    const auto source = impl_->playback.current_source();
    auto factory = impl_->playback.export_source_factory();
    auto source_path = impl_->playback.current_path();
    const auto playback = impl_->playback.snapshot();
    std::string unavailable;
    if (!simple_player_video_export_available()) unavailable = "This build does not include SYNCore video export";
    else if (source && !factory) unavailable = "This event source cannot create independent export readers; save it as MIDI first";
    else if (!source && source_path.empty()) unavailable = "Open a MIDI or prepare editor playback before exporting";
    else if (playback.busy && !playback.playing && !factory) unavailable = "Wait for source preparation before exporting";
    if (!unavailable.empty())
    {
        std::lock_guard lock(impl_->mutex);
        impl_->state.status = std::move(unavailable);
        return false;
    }
    // Prepared editor readers are virtual; the renderer uses this descriptor
    // only for source/output collision checks and never reads it from disk.
    if (source_path.empty()) source_path = L"Unsaved editor.mid";
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
        impl_->worker = std::jthread([self = impl_.get(), output_path = std::move(output_path),
            source_path = std::move(source_path), factory = std::move(factory), bank, synth, settings,
            total_events = playback.source_events]() mutable
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
                    result = render_simple_player_video_events(source_path, *audio, *video,
                        total_events, output_path, bank, synth, settings,
                        &self->cancel_requested, &impl::progress_callback, self);
                }
                else
                    result = render_simple_player_video(source_path, output_path, bank, synth, settings,
                        &self->cancel_requested, &impl::progress_callback, self);
            }
            catch (const std::exception& error) { result.error = error.what(); }
            catch (...) { result.error = "Unexpected export failure"; }
            {
                std::lock_guard lock(self->mutex);
                self->state.result = result;
                self->state.status = result.ok ? "MP4 saved" : result.cancelled ? "Render cancelled" : "Render failed";
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

void video_export_panel::cancel() { impl_->cancel_requested.store(true, std::memory_order_release); }

void video_export_panel::shutdown()
{
    if (!impl_ || impl_->closed) return;
    impl_->closed = true;
    cancel();
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->texture) glDeleteTextures(1, &impl_->texture);
    impl_->texture = 0;
}

void video_export_panel::draw(bool* open)
{
    if (impl_->closed || (open && !*open)) return;
    ImGui::SetNextWindowPos({90, 86}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({600, std::max(420.f, std::min(730.f, ImGui::GetIO().DisplaySize.y - 116.f))}, ImGuiCond_FirstUseEver);
    if (begin_folded_window("Player video render", open))
    {
        const auto current = snapshot();
        impl_->upload_preview();
        ImGui::TextWrapped("Source: %s", display_path(current.busy
            ? current.source_path : impl_->playback.current_path()).c_str());
        if (impl_->texture && impl_->texture_width && impl_->texture_height)
        {
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::Image(static_cast<ImTextureID>(impl_->texture),
                {width, width * impl_->texture_height / impl_->texture_width});
        }
        else
        {
            ImGui::BeginChild("Preview", {0, 180}, ImGuiChildFlags_Borders);
            ImGui::TextDisabled("Rendered frames appear here");
            ImGui::EndChild();
        }
        ImGui::TextWrapped("%s", current.cancelling ? "Cancelling..." : current.status.c_str());
        ImGui::ProgressBar(fraction(current.progress.completed_frames, current.progress.total_frames), {-1, 0}, "Video");
        ImGui::ProgressBar(fraction(current.progress.completed_audio_frames, current.progress.total_audio_frames), {-1, 0}, "Audio");
        if (current.progress.total_frames)
            ImGui::Text("Frames %llu / %llu  |  Events %llu  |  Voices %llu",
                current.progress.completed_frames, current.progress.total_frames,
                current.progress.completed_events, current.progress.active_voices);
        if (!current.result.error.empty())
            ImGui::TextWrapped("%s", current.result.error.c_str());
        if (!current.result.warning.empty())
            ImGui::TextWrapped("%s", current.result.warning.c_str());
        if (!current.output_path.empty())
            ImGui::TextWrapped("Output: %s", display_path(current.output_path).c_str());

        ImGui::Separator();
        ImGui::BeginDisabled(current.busy);
        auto& settings = impl_->settings;
        if (ImGui::BeginTable("Render settings", 2, ImGuiTableFlags_SizingStretchSame))
        {
            const auto integer = [](const char* label, std::uint32_t& value)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(label);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputScalar("##value", ImGuiDataType_U32, &value);
                ImGui::PopID();
            };
            integer("Width", settings.width);
            integer("Height", settings.height);
            integer("FPS", settings.fps);
            integer("Video kbps", settings.video_bitrate_kbps);
            integer("AAC kbps", settings.audio_bitrate_kbps);
            integer("AAC Hz", settings.audio_sample_rate);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Visible seconds");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputDouble("##visible_seconds", &settings.visible_seconds, .05, .25, "%.3f");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Tail seconds");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputDouble("##tail_seconds", &settings.tail_seconds, .5, 1, "%.3f");
            ImGui::EndTable();
        }
        int overlap = settings.remove_overlaps <= 1 ? settings.remove_overlaps : 2;
        if (ImGui::Combo("Overlaps", &overlap, "Naive removal\0Realtime removal\0Draw all\0"))
            settings.remove_overlaps = overlap == 2 ? 0xff : static_cast<std::uint8_t>(overlap);
        ImGui::TextWrapped("Uses applied SYNCore settings. Audio and video render independently from playback.");
        ImGui::BeginDisabled(!simple_player_video_export_available());
        if (ImGui::Button("Render MP4", {150, 0}) && impl_->dialogs.save_video)
        {
            auto suggestion = std::filesystem::path(impl_->playback.current_path());
            if (suggestion.empty()) suggestion = "Editor.mp4";
            suggestion.replace_extension(".mp4");
            try
            {
                auto destination = impl_->dialogs.save_video(suggestion.wstring());
                if (!destination.empty()) start_export(std::move(destination));
            }
            catch (const std::exception& error)
            { std::lock_guard lock(impl_->mutex); impl_->state.status = error.what(); }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!current.busy || current.cancelling);
        if (ImGui::Button("Cancel render")) cancel();
        ImGui::EndDisabled();
    }
    end_folded_window();
}
}
