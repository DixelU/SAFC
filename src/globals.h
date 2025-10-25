#pragma once

#ifndef SAFC2_GLOBALS_H
#define SAFC2_GLOBALS_H

#include <vector>
#include <deque>
#include <mutex>

namespace safc_globals
{

struct alert_queue_item
{
	enum class alert_type
	{
		error,
		warning,
		info
	};

	std::string message;
	std::string header;
	alert_type type;
};

inline std::deque<alert_queue_item> global_alert_queue;
inline std::deque<midi_file_meta> global_midi_list;
inline midi_file_meta save_to_file;

inline bool cb_rm_empty_tracks = true;
inline bool cb_rm_merge_remnants = true;
inline bool cb_ignore_all_instruments_2_piano = false;
inline bool cb_ignore_tempo_events = false;
inline bool cb_ignore_pitchbend_events = false;
inline bool cb_ignore_note_events = false;
inline bool cb_ignore_except_everythig_spec = false;
inline bool cb_multichannel_split = true;
inline bool cb_rsb_compression = false;
inline bool cb_implace_merge = true;
inline bool cb_apply_offset_after_ppq_change = false;
inline bool cb_collapse_trk_into_1 = false;
inline bool cb_allow_sysex_events = false;
inline std::uint16_t thread_count = 1;
inline std::uint16_t global_ppq = 96;

inline std::recursive_mutex global_lock; // why is it needed here tho?

}

#endif // !SAFC2_GLOBALS_H
