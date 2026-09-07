#pragma once

#include <memory>

struct GLFWwindow;

namespace safc::imgui_ui
{
// Keep this owner alive until all windows using its icons have been destroyed.
// Both sizes are loaded directly from the executable's MAINICON resource.
class window_icon
{
public:
    window_icon();
    ~window_icon();
    window_icon(const window_icon&) = delete;
    window_icon& operator=(const window_icon&) = delete;

    bool apply(GLFWwindow* window) const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
