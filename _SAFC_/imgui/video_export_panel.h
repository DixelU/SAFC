#pragma once

#include "platform_dialogs.h"
#include "../SAFC_InnerModules/simple_player_video_export.h"
#include <memory>
#include <string>

namespace safc::imgui_ui
{
class playback_session;

struct video_export_snapshot
{
    bool busy = false;
    bool cancelling = false;
    simple_player_video_progress progress;
    simple_player_video_result result;
    std::string status;
    std::wstring source_path;
    std::wstring output_path;
};

// Owns an offline renderer and UI-context preview texture. shutdown() must be
// called before destroying the UI OpenGL context. Hiding the panel keeps its job alive.
class video_export_panel
{
public:
    video_export_panel(playback_session& playback, native_dialogs dialogs);
    ~video_export_panel();
    video_export_panel(const video_export_panel&) = delete;
    video_export_panel& operator=(const video_export_panel&) = delete;
    void draw(bool* open);
    void shutdown();
    bool start_export(std::wstring output_path);
    void cancel();
    video_export_snapshot snapshot() const;
    simple_player_video_settings settings() const;
    void set_settings(const simple_player_video_settings& value);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
