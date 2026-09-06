#pragma once

#include <background_worker.h>

// Shared tag declarations make each worker identical at submission and shutdown.
struct compressed_player_watcher;
struct editor_load;
struct editor_playback;
struct editor_save;
struct info_collection;
struct info_collection_watcher;
struct merge;
struct merge_global_cleanup;
struct merge_ri_stage;
struct merge_ri_stage_cleanup;
struct midi_file_list;
struct midi_out_selct;
struct player_thread;
struct player_watcher;
struct syncore_status_watcher;
struct version_check;

// GUI work must never be drained during static destruction: queued tasks can
// reopen devices or touch UI state after the main loop has ended, and long-lived watcher
// tasks need a stop request before their worker can join. Keep that lifecycle
// policy local to SAFC; the reusable utility intentionally drains by default.
template<typename Tag>
struct worker_singleton
{
	static dixelu::background_worker& instance()
	{
		static dixelu::background_worker worker(
			dixelu::background_worker_shutdown::cancel);
		return worker;
	}
};
