#include "../imgui/birthday_notification.h"
#include "../imgui/folded_theme.h"

#include <imgui_internal.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct context_guard
{
    ImGuiContext* context = ImGui::CreateContext();
    explicit context_guard(float scale)
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200.f * scale, 800.f * scale};
        io.DeltaTime = 1.f / 60.f;
        ImFontConfig font;
        font.SizePixels = 17.f * scale;
        io.Fonts->AddFontDefault(&font);
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        safc::imgui_ui::apply_theme(scale);
        io.FontGlobalScale = 1.f; // Match the application's DPI-scaled font atlas.
    }
    ~context_guard() { ImGui::DestroyContext(context); }
};

std::string frame(safc::imgui_ui::birthday_notification& birthday, bool log = false)
{
    ImGui::NewFrame();
    if (log) ImGui::LogToBuffer();
    birthday.draw();
    const std::string text = log ? ImGui::GetCurrentContext()->LogBuffer.c_str() : "";
    if (log) ImGui::LogFinish();
    ImGui::Render();
    return text;
}

ImGuiWindow* birthday_window()
{
    return ImGui::FindWindowByName("SAFC birthday");
}

ImGuiWindow* birthday_content()
{
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (window->ParentWindow == birthday_window()
            && std::strstr(window->Name, "##folded-content")) return window;
    throw std::runtime_error("Birthday window has no folded content.");
}

void click_control(safc::imgui_ui::birthday_notification& birthday, ImGuiWindow* window, const char* label)
{
    const auto id = window->GetID(label);
    const auto bounds = window->InnerRect;
    auto& io = ImGui::GetIO();
    // Locate the actual control using ImGui hit testing, without duplicating
    // the notification's padding or button-position calculations.
    for (float y = bounds.Min.y + 2.f; y < bounds.Max.y; y += 5.f)
        for (float x = bounds.Min.x + 2.f; x < bounds.Max.x; x += 5.f)
        {
            io.AddMousePosEvent(x, y);
            frame(birthday);
            if (ImGui::GetHoveredID() != id) continue;
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame(birthday);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(birthday);
            frame(birthday);
            return;
        }
    throw std::runtime_error(std::string("Birthday control is unreachable: ") + label);
}
}

int main()
try
{
    using safc::imgui_ui::birthday_notification;
    require(!birthday_notification().active(), "An unspecified date enabled the birthday.");
    require(!birthday_notification(2017, 8, 31).active(), "Birthday predates the first release.");
    require(birthday_notification(2018, 8, 31).active(), "First release date was rejected.");
    require(!birthday_notification(2026, 8, 30).active(), "Birthday started a day early.");
    require(birthday_notification(2026, 8, 31).active(), "The eighth birthday was rejected.");
    require(!birthday_notification(2026, 9, 1).active(), "Birthday continued into September.");
    require(!birthday_notification(2024, 2, 29).active(), "Leap day triggered the birthday.");
    require(birthday_notification(2024, 8, 31).active(), "Leap year shifted the birthday.");

    for (const float scale : {1.f, 2.f})
    {
        context_guard context(scale);
        birthday_notification ordinary_day(2026, 9, 1);
        frame(ordinary_day);
        require(!birthday_window(), "An ordinary day rendered a birthday window.");

        birthday_notification first_release(2018, 8, 31);
        frame(first_release);
        require(frame(first_release, true).find("exactly 0 years since first SAFC release.") != std::string::npos,
            "The release-year anniversary text is incorrect.");

        birthday_notification birthday(2026, 8, 31);
        frame(birthday);
        const auto text = frame(birthday, true);
        require(birthday_window() && birthday_window()->Active, "The birthday notification was not rendered.");
        require((birthday_window()->Flags & ImGuiWindowFlags_Modal) == 0
            && ImGui::GetCurrentContext()->OpenPopupStack.empty(), "The birthday notification blocks the workspace.");
        require(text.find("Interesting fact: today is exactly 8 years since first SAFC release.") != std::string::npos
            && text.find("(o w o  )") != std::string::npos, "Birthday text or anniversary was not preserved.");
        require(birthday_content()->ScrollMax.x == 0.f && birthday_content()->ScrollMax.y == 0.f,
            "Birthday content exceeds the notification's visible area.");
        click_control(birthday, birthday_content(), "OK");
        require(!birthday_window()->Active, "OK did not dismiss the birthday notification.");
        require(birthday.active(), "Dismissing the greeting disabled the session's birthday background.");
        for (int i = 0; i < 60; ++i) frame(birthday);
        require(!birthday_window()->Active, "A dismissed notification reopened during the same session.");

        birthday_notification next_launch(2026, 8, 31);
        frame(next_launch); frame(next_launch);
        require(birthday_window()->Active, "A fresh launch failed to show the birthday again.");
        click_control(next_launch, birthday_window(), "##folded-close");
        require(!birthday_window()->Active, "The folded caption close button did not dismiss the notification.");
        frame(next_launch);
        require(!birthday_window()->Active, "Caption dismissal reopened the notification.");
    }
    std::cout << "PASS: birthday dates, anniversary text, 1x/2x layout, OK/caption dismissal, and launch lifetime\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "Birthday regression failed: " << error.what() << '\n';
    return 1;
}
