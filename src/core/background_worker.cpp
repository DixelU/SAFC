#include "background_worker.h"

#include <utility>

background_worker::background_worker(): 
	th(&background_worker::worker_thread_loop, this)
{}

background_worker::~background_worker()
{
	{
		std::unique_lock<std::mutex> lock(this->task_queue_mutex);

		this->terminate_flag.store(true);
		this->task_queue_cv.notify_one();
	}

	if (this->th.joinable())
		this->th.join();
}

void background_worker::push(std::function<void()> task)
{
	std::unique_lock<std::mutex> lock(this->task_queue_mutex);

	this->task_queue.push_back(std::move(task));
	this->task_queue_cv.notify_one();
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
