#ifndef UI_HPP
#define UI_HPP

#include <string>
#include <vector>
#include <mutex>

extern "C"
{

#include <sfd/sfd.h>

}

#define IM_CLAMP(V, MN, MX)     ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))

static sfd_Options midi_ofd_opts =
{
	.title        = "Add MIDIs to the List",
	.filter_name  = "MIDI File",
	.filter       = "*.mid|*.midi|*.MID|*.MIDI",
};

static sfd_Options midi_sfd_opts =
{
	.title        = "Save output MIDI",
	.filter_name  = "MIDI File",
	.filter       = "*.mid|*.midi|*.MID|*.MIDI",
};

inline std::vector<std::string> midi_list;

inline bool midi_settings_window = false;

inline bool cb_rm_empty_tracks               = true;
inline bool cb_rm_merge_remnants             = true;
inline bool cb_all_instruments_2_piano       = false;
inline bool cb_ignore_tempo_events           = false;
inline bool cb_ignore_pitchbend_events       = false;
inline bool cb_ignore_note_events            = false;
inline bool cb_ignore_except_everythig_spec  = false;
inline bool cb_multichannel_split            = true;
inline bool cb_rsb_compression               = false;
inline bool cb_implace_merge                 = true;
inline bool cb_apply_offset_after_ppq_change = false;
inline bool cb_collapse_trk_into_1           = false;
inline bool cb_allow_sysex_events            = false;
inline int thread_count = 1;

inline bool proc_popup = false;
inline std::recursive_mutex Lock;

void RenderMainWindow();

#endif