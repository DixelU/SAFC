#define NOMINMAX
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "../imgui/window_icon.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void check_resources(HMODULE module)
{
    require(FindResourceW(module, L"MAINICON", RT_GROUP_ICON) != nullptr, "Embedded MAINICON group is missing");
    require(FindResourceW(module, L"GLUT_ICON", RT_GROUP_ICON) != nullptr, "Legacy GLUT_ICON group is missing");
    auto resource = FindResourceW(module, L"MAINICON", RT_GROUP_ICON);
    require(SizeofResource(module, resource) >= 6, "MAINICON group is empty");
    const auto data = static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)));
    require(data != nullptr, "Cannot read MAINICON group");
    const auto count = data[4] | (data[5] << 8);
    require(count > 0 && SizeofResource(module, resource) >= 6u + count * 14u, "MAINICON group is malformed");
    for (int i = 0; i < count; ++i)
    {
        const auto entry = data + 6 + i * 14;
        const auto id = static_cast<WORD>(entry[12] | (entry[13] << 8));
        require(FindResourceW(module, MAKEINTRESOURCEW(id), RT_ICON) != nullptr, "MAINICON image resource is missing");
    }
}

std::vector<std::uint32_t> render_icon(HICON icon, int width, int height)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels, "Create icon image bitmap");
    struct bitmap_guard { HBITMAP value; ~bitmap_guard() { DeleteObject(value); } } bitmap_lifetime{bitmap};
    auto dc = CreateCompatibleDC(nullptr);
    require(dc != nullptr, "Create icon image DC");
    struct dc_guard { HDC value; ~dc_guard() { DeleteDC(value); } } dc_lifetime{dc};
    const auto original = SelectObject(dc, bitmap);
    auto* begin = static_cast<std::uint32_t*>(pixels);
    std::fill_n(begin, width * height, 0xff152638u);
    const auto drawn = DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
    SelectObject(dc, original);
    require(drawn != FALSE, "Render native icon");
    return {begin, begin + width * height};
}

void check_window_icon(HWND window, WPARAM size, int width, int height)
{
    const auto actual = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, size, 0));
    require(actual != nullptr, "Native window icon was not assigned");
    const auto expected = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"MAINICON", IMAGE_ICON,
        width, height, LR_DEFAULTCOLOR));
    require(expected != nullptr, "Load reference embedded icon");
    struct icon_guard { HICON value; ~icon_guard() { DestroyIcon(value); } } expected_lifetime{expected};
    require(render_icon(actual, width, height) == render_icon(expected, width, height),
        "Native icon pixels differ from embedded MAINICON");
}
}

int wmain(int argc, wchar_t** argv)
try
{
    check_resources(GetModuleHandleW(nullptr));
    for (int i = 1; i < argc; ++i)
    {
        const auto module = LoadLibraryExW(argv[i], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        require(module != nullptr, "Load application executable resources");
        struct module_guard { HMODULE value; ~module_guard() { FreeLibrary(value); } } module_lifetime{module};
        check_resources(module);
    }
    require(glfwInit() != GLFW_FALSE, "Initialize GLFW");
    struct glfw_guard { ~glfw_guard() { glfwTerminate(); } } glfw_lifetime;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    safc::imgui_ui::window_icon icon;
    require(!icon.apply(nullptr), "Null windows must be rejected");
    // Reuse the owned icons across window recreation and poll the real Win32 messages.
    for (int i = 0; i < 2; ++i)
    {
        auto* window = glfwCreateWindow(200, 100, "SAFC icon regression", nullptr, nullptr);
        require(window != nullptr, "Create hidden native GLFW window");
        struct window_guard { GLFWwindow* value; ~window_guard() { glfwDestroyWindow(value); } } window_lifetime{window};
        require(icon.apply(window), "Assign embedded native icons");
        glfwPollEvents();
        check_window_icon(glfwGetWin32Window(window), ICON_BIG, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
        check_window_icon(glfwGetWin32Window(window), ICON_SMALL, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    }
    std::cout << "PASS: embedded executable icon groups and native large/small icon pixels across window recreation\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
