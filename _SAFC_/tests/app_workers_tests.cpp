#include "app_workers_test_support.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>

int main()
{
    const auto submitted = submission_workers();
    const auto closed = shutdown_workers();
    if (submitted != closed || std::set(submitted.begin(), submitted.end()).size() != 16)
    {
        std::fputs("FAIL: worker identity differs across translation units or tags\n", stderr);
        std::_Exit(1);
    }

    test_state state;
    submit_running_tasks(state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state.started.load() != 16 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (state.started.load() != 16)
    {
        std::fputs("FAIL: tasks did not start\n", stderr);
        std::_Exit(2);
    }

    submit_pending_tasks(state);
    shutdown_from_other_translation_unit();
    if (state.stop_seen.load() != 16 || state.queued_ran.load() != 0)
    {
        std::fputs("FAIL: cross-TU shutdown did not cancel active and queued work\n", stderr);
        return 3;
    }
    for (auto* worker : submitted)
    {
        const auto status = worker->snapshot();
        if (status.state != dixelu::background_worker_state::stopped ||
            status.completed != 1 || status.cancelled != 1 ||
            worker->push([] {}) != dixelu::background_worker_submit_result::stopped)
        {
            std::fputs("FAIL: shutdown state or rejection is incorrect\n", stderr);
            return 4;
        }
    }

    std::puts("PASS: all 16 worker tags share identity across TUs; cancellation stops active tasks, discards queued tasks, and rejects later submissions");
}
