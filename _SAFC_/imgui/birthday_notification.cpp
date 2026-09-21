#define NOMINMAX
#include <Windows.h>

#include "birthday_notification.h"
#include "folded_theme.h"

#include <cmath>
#include <numbers>

namespace safc::imgui_ui
{
birthday_notification::birthday_notification(int year, int month, int day)
	: years_old_(month == 8 && day == 31 && year >= 2018 ? year - 2018 : -1), open_(active())
{
}

birthday_notification birthday_notification::today()
{
	SYSTEMTIME date{};
	GetLocalTime(&date);
	return {date.wYear, date.wMonth, date.wDay};
}

void birthday_notification::draw()
{
	if (!open_)
		return;
	const float scale = ImGui::GetFontSize() / workspace_font_size;
	ImGui::SetNextWindowSize({460.f * scale, 160.f * scale}, ImGuiCond_Always);
	if (first_frame_)
	{
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {.5f, .5f});
		ImGui::SetNextWindowFocus();
		first_frame_ = false;
	}
	if (begin_folded_window("SAFC birthday", &open_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
	{
		// Retain the legacy orange-to-white rotating ring as decoration.
		const auto position = ImGui::GetCursorScreenPos();
		const ImVec2 center{position.x + 20.f * scale, position.y + 20.f * scale};
		auto* draw = ImGui::GetWindowDrawList();
		constexpr float turn = 2.f * std::numbers::pi_v<float>;
		constexpr int spinner_segments = 50;
		constexpr float rotation_seconds = 2.25f;
		constexpr float segment_fill = .8f;
		const float rotation =
			static_cast<float>(std::fmod(ImGui::GetTime(), rotation_seconds)) * turn / rotation_seconds;
		for (int i = 0; i < spinner_segments; ++i)
		{
			const float a = rotation + turn * i / spinner_segments;
			const float b = rotation + turn * (i + segment_fill) / spinner_segments;
			const ImVec2 from{center.x + 15.f * scale * std::cos(a), center.y + 15.f * scale * std::sin(a)};
			const ImVec2 to{center.x + 15.f * scale * std::cos(b), center.y + 15.f * scale * std::sin(b)};
			draw->AddLine(from, to,
				IM_COL32(255, 127 + 128 * i / spinner_segments, 63 + 192 * i / spinner_segments, 255), 3.f * scale);
		}
		ImGui::Dummy({40.f * scale, 40.f * scale});
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextWrapped(
			"Interesting fact: today is exactly %d years since first SAFC release.\n(o w o  )", years_old_);
		ImGui::Spacing();
		if (ImGui::Button("OK", {80.f * scale, 0}))
			open_ = false;
		ImGui::EndGroup();
	}
	end_folded_window();
}
}
