#pragma once
#ifndef SAFGUIF_STLSPRESETS
#define SAFGUIF_STLSPRESETS

#include "single_text_line_settings.h"

// These objects have stable addresses because controls retain pointers to them.
// Their mode-dependent state is assigned explicitly by safc_gui_runtime after
// the registry has been read; static initialization must not inspect is_fonted.
inline single_text_line_settings legacy_white("_", 0, 0, 5, 0xFFFFFFFF);
inline single_text_line_settings legacy_black("_", 0, 0, 5, 0x000000FF);

inline single_text_line_settings system_black("_", 0, 0, 5, 0x000000FF);
inline single_text_line_settings system_white("_", 0, 0, 5, 0xFFFFFFFF);
inline single_text_line_settings system_red("_", 0, 0, 5, 0xFF7F3FFF);
inline single_text_line_settings system_blue("_", 0, 0, 5, 0x9FCFFFFF);

inline void initialise_system_text_styles(bool fonts_enabled)
{
	system_black = fonts_enabled
		? single_text_line_settings(10, 0x000000FF)
		: single_text_line_settings("_", 0, 0, 5, 0x000000FF);
	system_white = fonts_enabled
		? single_text_line_settings(10, 0xFFFFFFFF)
		: single_text_line_settings("_", 0, 0, 5, 0xFFFFFFFF);
	system_red = fonts_enabled
		? single_text_line_settings(10, 0xFF7F3FFF)
		: single_text_line_settings("_", 0, 0, 5, 0xFF7F3FFF);
	system_blue = fonts_enabled
		? single_text_line_settings(10, 0x9FCFFFFF)
		: single_text_line_settings("_", 0, 0, 5, 0x9FCFFFFF);
}

#endif
