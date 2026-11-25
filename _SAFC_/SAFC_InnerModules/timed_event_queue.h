#pragma once

#include <buffered_queue_spsc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

// SPSC queue for ordered playback events. Payloads occupy 32 KiB pages;
// a separate FIFO records where each timestamp starts. Publishing the event
// count also makes the unfinished page/run readable, even for a lone event.
template<typename Stamp, typename Event>
class timed_event_queue
{
	static constexpr std::size_t page_size = std::bit_floor(32768 / sizeof(Event));
	using page = std::array<Event, page_size>;

	struct boundary
	{
		Stamp stamp;
		std::size_t first;
	};

	dixelu::buffered_queue_spsc<page, 1> pages;
	dixelu::buffered_queue_spsc<boundary, 256> boundaries;

	alignas(64) std::atomic<std::size_t> published{0};
	page* tail = nullptr;
	Stamp last_stamp{};

	alignas(64) std::atomic<std::size_t> consumed{0};
	Stamp front_stamp{};

public:
	struct run
	{
		Stamp stamp;
		std::span<const Event> events;
	};

	void push(Stamp stamp, Event event)
	{
		const auto index = published.load(std::memory_order_relaxed);
		const auto slot = index % page_size;
		if (slot == 0)
			tail = &pages.emplace();
		if (slot == 0 || stamp != last_stamp)
		{
			boundaries.push({stamp, index});
			last_stamp = stamp;
		}
		(*tail)[slot] = event;
		published.store(index + 1, std::memory_order_release);
	}

	// The span remains valid until its page is consumed. The producer only
	// writes subsequent slots and cannot recycle this page beneath the reader.
	[[nodiscard]] auto front() -> run
	{
		const auto index = consumed.load(std::memory_order_relaxed);
		const auto available = published.load(std::memory_order_acquire) - index;
		if (available == 0)
			return {};

		if (!boundaries.empty() && boundaries.front().first == index)
		{
			front_stamp = boundaries.front().stamp;
			boundaries.pop();
		}
		const auto slot = index % page_size;
		auto count = (std::min)(available, page_size - slot);
		if (!boundaries.empty())
			count = (std::min)(count, boundaries.front().first - index);
		return {front_stamp, {pages.front().data() + slot, count}};
	}

	// Consume an accepted prefix of the front span, including a partial send.
	void pop(std::size_t count)
	{
		const auto next = consumed.load(std::memory_order_relaxed) + count;
		if (count && next % page_size == 0)
			pages.pop();
		consumed.store(next, std::memory_order_release);
	}

	[[nodiscard]] auto approximate_size() const noexcept -> std::size_t
	{
		const auto first = consumed.load(std::memory_order_acquire);
		return published.load(std::memory_order_acquire) - first;
	}

	// Both endpoints must be stopped before a seek/reset.
	void clear()
	{
		boundaries.clear();
		pages.clear();
		tail = nullptr;
		published.store(0, std::memory_order_relaxed);
		consumed.store(0, std::memory_order_relaxed);
	}
};
