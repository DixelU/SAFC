#define NOMINMAX
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "window_icon.h"

namespace safc::imgui_ui
{
struct window_icon::impl
{
    HICON large_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"MAINICON", IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON small_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"MAINICON", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    ~impl()
    {
        if (small_icon) DestroyIcon(small_icon);
        if (large_icon) DestroyIcon(large_icon);
    }
};

window_icon::window_icon() : impl_(std::make_unique<impl>()) {}
window_icon::~window_icon() = default;

bool window_icon::apply(GLFWwindow* window) const noexcept
{
    if (!window || !impl_->large_icon || !impl_->small_icon) return false;
    const auto native = glfwGetWin32Window(window);
    if (!native) return false;
    SendMessageW(native, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(impl_->large_icon));
    SendMessageW(native, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(impl_->small_icon));
    return true;
}
}
