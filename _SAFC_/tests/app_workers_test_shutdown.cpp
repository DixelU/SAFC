#include "app_workers_test_support.h"

// Match the anonymous namespace that owns gl_close().
namespace
{
worker_addresses resolve_workers()
{
    return {
        &worker_singleton<struct compressed_player_watcher>::instance(),
        &worker_singleton<struct editor_load>::instance(),
        &worker_singleton<struct editor_playback>::instance(),
        &worker_singleton<struct editor_save>::instance(),
        &worker_singleton<struct info_collection>::instance(),
        &worker_singleton<struct info_collection_watcher>::instance(),
        &worker_singleton<struct merge>::instance(),
        &worker_singleton<struct merge_global_cleanup>::instance(),
        &worker_singleton<struct merge_ri_stage>::instance(),
        &worker_singleton<struct merge_ri_stage_cleanup>::instance(),
        &worker_singleton<struct midi_file_list>::instance(),
        &worker_singleton<struct midi_out_selct>::instance(),
        &worker_singleton<struct player_thread>::instance(),
        &worker_singleton<struct player_watcher>::instance(),
        &worker_singleton<struct syncore_status_watcher>::instance(),
        &worker_singleton<struct version_check>::instance()
    };
}
}

worker_addresses shutdown_workers()
{
    return resolve_workers();
}

void shutdown_from_other_translation_unit()
{
    for (auto* worker : resolve_workers())
        worker->shutdown(dixelu::background_worker_shutdown::cancel);
}
