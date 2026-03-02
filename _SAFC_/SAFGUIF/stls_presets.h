#pragma once
#ifndef SAFGUIF_STLSPRESETS
#define SAFGUIF_STLSPRESETS

#include "single_text_line_settings.h"

// Inline preset instances — no dynamic allocation, no leaks.
// Callers that need a pointer should use &system_white etc.
inline single_text_line_settings legacy_white("_", 0, 0, 5, 0xFFFFFFFF);
inline single_text_line_settings legacy_black("_", 0, 0, 5, 0x000000FF);

inline single_text_line_settings system_black = is_fonted
    ? single_text_line_settings(10, 0x000000FF)
    : single_text_line_settings("_", 0, 0, 5, 0x000000FF);

inline single_text_line_settings system_white = is_fonted
    ? single_text_line_settings(10, 0xFFFFFFFF)
    : single_text_line_settings("_", 0, 0, 5, 0xFFFFFFFF);

inline single_text_line_settings system_red = is_fonted
    ? single_text_line_settings(10, 0xFF7F3FFF)
    : single_text_line_settings("_", 0, 0, 5, 0xFF7F3FFF);

inline single_text_line_settings system_blue = is_fonted
    ? single_text_line_settings(10, 0x9FCFFFFF)
    : single_text_line_settings("_", 0, 0, 5, 0x9FCFFFFF);

// Compatibility pointer aliases (old code passes these as pointers to constructors)
inline single_text_line_settings* Legacy_White = &legacy_white;
inline single_text_line_settings* Legacy_Black = &legacy_black;
inline single_text_line_settings* System_Black = &system_black;
inline single_text_line_settings* System_White = &system_white;
inline single_text_line_settings* System_Red   = &system_red;
inline single_text_line_settings* System_Blue  = &system_blue;

#endif
