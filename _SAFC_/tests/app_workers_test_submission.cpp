#include "app_workers_test_support.h"

#include <condition_variable>
#include <mutex>

// Match nested namespaces and block-scope elaborated type names used in the app.
namespace props_and_sets::SMIC
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

worker_addresses submission_workers()
{
    return props_and_sets::SMIC::resolve_workers();
}

void submit_running_tasks(test_state& state)
{
    for (auto* worker : submission_workers())
        worker->push([&state](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any condition;
            std::unique_lock lock(mutex);
            ++state.started;
            condition.wait(lock, stop, [] { return false; });
            if (stop.stop_requested())
                ++state.stop_seen;
        });
}

void submit_pending_tasks(test_state& state)
{
    for (auto* worker : submission_workers())
        worker->push([&state] { ++state.queued_ran; });
}
