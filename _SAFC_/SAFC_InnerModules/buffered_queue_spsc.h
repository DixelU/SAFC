#pragma once
#ifndef SAFC_BUFFERED_QUEUE_SPSC
#define SAFC_BUFFERED_QUEUE_SPSC

#include <atomic>
#include <cstddef>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

template<typename T>
struct buffered_queue_spsc
{
private:
	static constexpr size_t slab_size = 1 << 15;

	// Cap on cached empty slabs per queue. Above this, freed slabs are
	// deleted rather than pushed to the recycle stack, so peak polyphony
	// doesn't leave the recycle stack hoarding memory indefinitely.
	static constexpr size_t max_recycled_slabs = 6;

	struct slab
	{
		alignas(64) std::aligned_storage_t<sizeof(T), alignof(T)> data[slab_size];

		T* begin;                      // consumer-owned, only consumer modifies
		std::atomic<T*> end;           // producer writes, consumer reads
		std::atomic<slab*> next_slab;  // producer writes, consumer reads

		slab() :
			begin(reinterpret_cast<T*>(data)),
			end(reinterpret_cast<T*>(data)),
			next_slab(nullptr)
		{}

		T* capacity_end() const { return reinterpret_cast<T*>(const_cast<slab*>(this)->data) + slab_size; }
		T* data_begin() { return reinterpret_cast<T*>(data); }

		[[nodiscard]] bool empty_consumer() const
		{
			return begin == end.load(std::memory_order_acquire);
		}

		T& front() { return *begin; }
		const T& front() const { return *begin; }

		void pop_consumer() { begin->~T(); ++begin; }

		bool full_producer() const
		{
			return end.load(std::memory_order_relaxed) == capacity_end();
		}

		void push_producer(T&& v)
		{
			T* cur_end = end.load(std::memory_order_relaxed);
			::new(cur_end) T(std::move(v));
			end.store(cur_end + 1, std::memory_order_release);
		}

		T& back_producer()
		{
			return *(end.load(std::memory_order_relaxed) - 1);
		}

		void reset_for_reuse()
		{
			begin = data_begin();
			end.store(data_begin(), std::memory_order_relaxed);
			next_slab.store(nullptr, std::memory_order_relaxed);
		}

		void clear_consumer()
		{
			while (begin != end.load(std::memory_order_acquire))
				pop_consumer();
		}
	};

	// Producer-owned
	alignas(64) slab* tail_ = nullptr;
	size_t pushed_local_ = 0; // plain mirror of pushed_, avoids re-reading the atomic

	// Consumer-owned
	alignas(64) slab* head_ = nullptr;
	size_t popped_local_ = 0; // plain mirror of popped_

	// lock-free recycle stack: consumer pushes, producer pops
	alignas(64) std::atomic<slab*> recycle_head_{nullptr};

	// Bound on cached slabs in recycle stack (touched by both threads)
	alignas(64) std::atomic<size_t> recycle_count_{0};

	// Approximate element count for size-based throttling, split into
	// monotonic per-side counters so neither thread does an atomic RMW
	// on a shared cache line: each side bumps its local mirror and
	// publishes it with a relaxed store to its own line.
	alignas(64) std::atomic<size_t> pushed_{0};
	alignas(64) std::atomic<size_t> popped_{0};

	slab* producer_get_slab()
	{
		// Try recycle stack first (lock-free pop)
		slab* r = recycle_head_.load(std::memory_order_acquire);
		while (r)
		{
			slab* next = r->next_slab.load(std::memory_order_relaxed);
			if (recycle_head_.compare_exchange_weak(r, next,
				std::memory_order_acquire, std::memory_order_relaxed))
			{
				recycle_count_.fetch_sub(1, std::memory_order_relaxed);
				r->reset_for_reuse();
				return r;
			}
		}
		return new slab();
	}

	void consumer_recycle_slab(slab* s)
	{
		// Cap the recycle stack so freed slabs don't accumulate after a peak
		if (recycle_count_.load(std::memory_order_relaxed) >= max_recycled_slabs)
		{
			delete s;
			return;
		}

		// lock-free push to recycle stack
		slab* old_head = recycle_head_.load(std::memory_order_relaxed);
		do {
			s->next_slab.store(old_head, std::memory_order_relaxed);
		} while (!recycle_head_.compare_exchange_weak(old_head, s,
			std::memory_order_release, std::memory_order_relaxed));
		recycle_count_.fetch_add(1, std::memory_order_relaxed);
	}

	void ensure_initialized_producer()
	{
		if (!tail_)
		{
			tail_ = producer_get_slab();
			head_ = tail_;
		}
	}

public:
	buffered_queue_spsc() = default;

	~buffered_queue_spsc()
	{
		// Clean up main chain
		while (head_)
		{
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			head_->clear_consumer();
			delete head_;
			head_ = next;
		}

		// Clean up recycle stack
		slab* r = recycle_head_.load(std::memory_order_relaxed);
		while (r)
		{
			slab* next = r->next_slab.load(std::memory_order_relaxed);
			delete r;
			r = next;
		}
	}

	// Non-copyable, non-movable (due to internal pointers)
	buffered_queue_spsc(const buffered_queue_spsc&) = delete;
	buffered_queue_spsc& operator=(const buffered_queue_spsc&) = delete;

	// Producer: push element
	SIMPLE_PLAYER_FORCE_NO_INLINE void push(T&& value)
	{
		ensure_initialized_producer();

		if (tail_->full_producer())
		{
			slab* new_slab = producer_get_slab();
			if (!new_slab) [[unlikely]]
				return;

			new_slab->push_producer(std::move(value));
			// Publish new slab - consumer will see it when following next_slab
			tail_->next_slab.store(new_slab, std::memory_order_release);
			tail_ = new_slab;
		}
		else
			tail_->push_producer(std::move(value));

		pushed_.store(++pushed_local_, std::memory_order_relaxed);
	}

	// Producer: get reference to last pushed element (for pending tracking)
	[[nodiscard]] T& back()
	{
		return tail_->back_producer();
	}

	// Consumer: pop front element
	SIMPLE_PLAYER_FORCE_NO_INLINE void pop()
	{
		if (!head_) [[unlikely]]
			return;

		head_->pop_consumer();
		popped_.store(++popped_local_, std::memory_order_relaxed);

		// If current slab exhausted, try to advance
		if (head_->empty_consumer())
		{
			slab* next = head_->next_slab.load(std::memory_order_acquire);
			if (next)
			{
				slab* old = head_;
				head_ = next;
				consumer_recycle_slab(old);
			}
			// else: keep empty slab as head (producer might add more)
		}
	}

	// Consumer: check if empty
	[[nodiscard]] bool empty() const
	{
		if (!head_)
			return true;

		if (!head_->empty_consumer())
			return false;

		// Current slab empty - check if there's a next one
		return head_->next_slab.load(std::memory_order_acquire) == nullptr;
	}

	// Consumer: get front element
	[[nodiscard]] T& front()
	{
		// Advance past empty slabs if needed
		while (head_->empty_consumer())
		{
			slab* next = head_->next_slab.load(std::memory_order_acquire);
			if (!next) break;
			slab* old = head_;
			head_ = next;
			consumer_recycle_slab(old);
		}
		return head_->front();
	}

	[[nodiscard]] const T& front() const
	{
		return const_cast<buffered_queue_spsc*>(this)->front();
	}

	// Clear all elements (consumer operation, but callable during reset)
	void clear()
	{
		while (head_)
		{
			head_->clear_consumer();
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			if (next)
			{
				consumer_recycle_slab(head_);
				head_ = next;
			}
			else
			{
				// Keep one slab for reuse
				head_->reset_for_reuse();
				tail_ = head_;
				break;
			}
		}
		// clear() only runs with the producer stopped (reset/seek restart),
		// so resetting the producer-side counter here is safe.
		pushed_local_ = 0;
		popped_local_ = 0;
		pushed_.store(0, std::memory_order_relaxed);
		popped_.store(0, std::memory_order_relaxed);
	}

	// Approximate element count. The two counters are published with relaxed
	// stores, so the loads may be mutually stale; clamp because a reader can
	// observe popped_ ahead of pushed_. The producer sees an exact-or-over
	// estimate, the consumer exact-or-under - both safe for throttling.
	[[nodiscard]] size_t approximate_size() const
	{
		size_t p = pushed_.load(std::memory_order_relaxed);
		size_t c = popped_.load(std::memory_order_relaxed);
		return p > c ? p - c : 0;
	}

	// Consumer iterator for traversal
	struct iterator
	{
		slab* current_slab;
		T* cur;
		T* slab_end;  // cached end for current slab

		iterator(slab* s, T* p, T* e) : current_slab(s), cur(p), slab_end(e) {}

		T& operator*() { return *cur; }
		T* operator->() { return cur; }

		iterator& operator++()
		{
			++cur;
			if (cur >= slab_end)
			{
				slab* next = current_slab ?
					current_slab->next_slab.load(std::memory_order_acquire) : nullptr;
				if (next)
				{
					current_slab = next;
					cur = current_slab->begin;
					slab_end = current_slab->end.load(std::memory_order_acquire);
				}
				// else: stay at end position
			}
			return *this;
		}

		bool operator!=(const iterator& other) const { return cur != other.cur; }
		bool operator==(const iterator& other) const { return cur == other.cur; }
	};

	// Consumer: begin iterator
	[[nodiscard]] iterator begin()
	{
		if (!head_)
			return iterator(nullptr, nullptr, nullptr);
		T* e = head_->end.load(std::memory_order_acquire);
		return iterator(head_, head_->begin, e);
	}

	// Consumer: end iterator (snapshot of current tail position)
	[[nodiscard]] iterator end()
	{
		if (!tail_)
			return iterator(nullptr, nullptr, nullptr);
		T* e = tail_->end.load(std::memory_order_acquire);
		return iterator(tail_, e, e);
	}
};

#endif
