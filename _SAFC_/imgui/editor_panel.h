#pragma once

#include "platform_dialogs.h"

#include <memory>
#include <string>

namespace safc::imgui_ui
{
class playback_session;

// Owns the editor document, all canvas gestures and the serialized I/O worker.
// Public entry points belong to the UI thread. No SAFGUIF objects are used.
class editor_panel
{
public:
    explicit editor_panel(playback_session& playback, native_dialogs dialogs = {});
    ~editor_panel();
    editor_panel(const editor_panel&) = delete;
    editor_panel& operator=(const editor_panel&) = delete;

    void draw(bool* open);
    // Consume completed jobs every UI frame, including while the panel is hidden.
    void poll();
    void open_file(std::wstring path);
    void shutdown();
    bool has_unsaved_changes() const;
    bool busy() const;

    // Only used by the executable's deterministic, silent workflow smoke mode.
    // Creates fixtures inside output_directory and leaves the editor ready to draw.
    bool run_smoke(const std::wstring& output_directory, std::string& report);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
