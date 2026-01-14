#pragma once

#ifndef SAFC_BGWORKER
#define SAFC_BGWORKER

#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <functional>

#include <utility>

struct background_worker
{
	background_worker();
	~background_worker();

	void push(std::function<void()> task);
	void stop();

private:
	void worker_thread_loop();

	std::atomic_bool terminate_flag = false;
	std::thread th;

	std::mutex task_queue_mutex;
	std::condition_variable task_queue_cv;
	std::deque<std::function<void()>> task_queue;
};

struct workers_collection
{
	static std::vector<std::reference_wrapper<background_worker>>& get()
	{
		static std::vector<std::reference_wrapper<background_worker>> workers;
		return workers;
	}

	static void add(std::reference_wrapper<background_worker> ref) { get().emplace_back(ref); }
	static void stop_all()
	{
		for (auto& worker : get())
			worker.get().stop();
	}

	explicit workers_collection(background_worker& worker) { add(std::ref(worker)); }
};

template<typename __tag_name>
struct worker_singleton
{
	static background_worker& instance()
	{
		static background_worker instance;
		static workers_collection _{instance};
		return instance;
	}
};

background_worker::background_worker() :
	th(&background_worker::worker_thread_loop, this)
{}

background_worker::~background_worker()
{
	this->stop();
}

void background_worker::push(std::function<void()> task)
{
	std::unique_lock<std::mutex> lock(this->task_queue_mutex);

	this->task_queue.push_back(std::move(task));
	this->task_queue_cv.notify_one();
}

void background_worker::stop()
{
	{
		std::unique_lock<std::mutex> lock(this->task_queue_mutex);

		this->terminate_flag.store(true);
		this->task_queue_cv.notify_one();
	}

	if (this->th.joinable())
		this->th.join();
}

void background_worker::worker_thread_loop()
{
	while (!this->terminate_flag.load())
	{
		std::function<void()> task;

		{
			std::unique_lock<std::mutex> lock(this->task_queue_mutex);
			this->task_queue_cv.wait(lock, [this]() { return this->terminate_flag.load() || !this->task_queue.empty(); });

			if (this->terminate_flag.load())
				return;

			task = std::move(this->task_queue.front());
			this->task_queue.pop_front();
		}

		if (task)
			task();
	}
}

#endif // !SAFC_BGWORKER