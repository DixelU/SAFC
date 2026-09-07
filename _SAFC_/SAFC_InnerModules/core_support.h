#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

// Core portability and diagnostics do not depend on a GUI implementation.
#ifndef FORCEDINLINE
#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE inline __attribute__((always_inline))
#endif
#endif

void throw_alert_error(std::string&& text);
void throw_alert_warning(std::string&& text);
