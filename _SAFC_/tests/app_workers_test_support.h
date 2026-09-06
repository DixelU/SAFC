#pragma once

#include "../app/app_workers.h"

#include <array>
#include <atomic>

using worker_addresses = std::array<dixelu::background_worker*, 16>;

struct test_state
{
    std::atomic<int> started{0};
    std::atomic<int> stop_seen{0};
    std::atomic<int> queued_ran{0};
};

worker_addresses submission_workers();
worker_addresses shutdown_workers();
void submit_running_tasks(test_state& state);
void submit_pending_tasks(test_state& state);
void shutdown_from_other_translation_unit();
