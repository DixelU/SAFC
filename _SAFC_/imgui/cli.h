#pragma once
#include <string>

namespace safc::imgui_ui
{
class project_session;
// Load and validate a config into an empty owned project. Throws on invalid
// configuration or any rejected input; entries never slide onto another MIDI.
// Passing false avoids reading registry preferences for hermetic callers.
void load_cli_config(const std::wstring& path, project_session& project, bool load_preferences = true);
int run_cli(const std::wstring& argument, bool load_preferences = true);
const char* cli_help();
}
