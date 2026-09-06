#pragma once

#include <string>
#include <vector>

std::vector<std::wstring> multiple_open_file_dialog(const wchar_t* title);
std::wstring playback_source_open_file_dialog();
std::wstring syncore_bank_open_file_dialog();
std::wstring save_open_file_dialog(const wchar_t* title);