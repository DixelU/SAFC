#pragma once

#include <array>
#include <cstdint>

// major.minor.patch.build, ordered lexicographically.
using version_t = std::array<std::uint16_t, 4>;

extern version_t g_version_tuple;

version_t get_executable_version();
void safc_version_check();
