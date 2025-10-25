#pragma once

#ifndef SAFC_BGWORKER
#define SAFC_BGWORKER

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

struct background_worker
{
	background_worker();
	~background_worker();

	void push(std::function<void()> task);

private:
	void worker_thread_loop();

	std::atomic_bool terminate_flag = false;
	std::jthread th;

	std::mutex task_queue_mutex;
	std::condition_variable task_queue_cv;
	std::vector<std::function<void()>> task_queue;
};

template<typename __tag_name>
struct worker_singleton
{
	static background_worker& instance()
	{
		static background_worker instance;
		return instance;
	}
};

#endif // !SAFC_BGWORKER