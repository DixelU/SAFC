#ifndef UI_HPP
#define UI_HPP

#include "../core/midi_file_meta.h"
#include "../globals.h"

#define IM_CLAMP(V, MN, MX) ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))

inline bool midi_settings_window = false;
inline bool proc_popup = false;

void RenderMainWindow();

#endif