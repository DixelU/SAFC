#pragma once

#ifndef SAF_SPILLABLE_ALLOCATOR
#define SAF_SPILLABLE_ALLOCATOR

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#define SAF_SPILLABLE_ALLOCATOR_DEFINED_NOMINMAX
#endif
#include <windows.h>
#ifdef SAF_SPILLABLE_ALLOCATOR_DEFINED_NOMINMAX
#undef NOMINMAX
#undef SAF_SPILLABLE_ALLOCATOR_DEFINED_NOMINMAX
#endif
#endif

// Shared by every buffer of one job; must outlive the allocations made with it.
struct spill_settings
{
	// Blocks below this size never leave the heap.
	std::size_t minimal_spilled_size = 64ull << 20;
	// Physical memory that a heap block must leave available to the system.
	std::size_t memory_headroom = 1ull << 30;
	// Directory of the backing files. Empty means the system temporary directory.
	std::filesystem::path directory;
	// Puts every block of at least a page on disk, whatever the memory state.
	bool force_spill = false;

	std::atomic_uint64_t spilled_bytes{0};
	std::atomic_uint64_t spilled_blocks_count{0};
};

// Heap blocks while memory lasts, temporary file mappings after that. Every
// block carries its own release information, so all instances are interchangeable.
struct spillable_memory
{
	struct block_header
	{
		std::uint64_t size;
		spill_settings* settings;
		void* file;
		void* mapping;
	};
	static_assert(sizeof(block_header) == 32);

	[[nodiscard]] static void* allocate(std::size_t size, spill_settings* settings)
	{
		if (size > (~std::size_t(0)) - sizeof(block_header))
			throw std::bad_alloc{};

		const std::size_t block_size = size + sizeof(block_header);
		block_header* header = nullptr;

		const bool is_spilled = should_spill(block_size, settings);
		if (!is_spilled)
			header = static_cast<block_header*>(std::malloc(block_size));

		if (header)
			*header = block_header{ block_size, nullptr, nullptr, nullptr };
		else if (is_spilled || (settings && block_size >= settings->minimal_spilled_size))
			header = map_temporary_file(block_size, *settings);
		else
			throw std::bad_alloc{};

		return header + 1;
	}

	static void deallocate(void* pointer) noexcept
	{
		if (!pointer)
			return;

		auto* header = static_cast<block_header*>(pointer) - 1;
		if (!header->mapping)
			return std::free(header);

		const auto block = *header;
#ifdef _WIN32
		UnmapViewOfFile(header);
		CloseHandle(block.mapping);
		// The file was opened as delete-on-close
		CloseHandle(block.file);
#endif
		block.settings->spilled_bytes -= block.size;
		--block.settings->spilled_blocks_count;
	}

private:
	static bool should_spill(std::size_t block_size, const spill_settings* settings)
	{
		constexpr std::size_t minimal_forced_size = 4096;

		if (!settings)
			return false;
		if (settings->force_spill)
			return block_size >= minimal_forced_size;
		if (block_size < settings->minimal_spilled_size)
			return false;

#ifdef _WIN32
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		if (!GlobalMemoryStatusEx(&status))
			return false;

		// A block that only fits with the help of the page file would make the
		// whole system swap; pages of a mapped file are dropped cheaply instead.
		const auto available = (std::min)(status.ullAvailPhys, status.ullAvailPageFile);
		return available < settings->memory_headroom ||
			block_size > available - settings->memory_headroom;
#else
		return false;
#endif
	}

	[[nodiscard]] static block_header* map_temporary_file(std::size_t block_size, spill_settings& settings)
	{
#ifdef _WIN32
		static std::atomic_uint64_t counter{0};

		std::error_code error;
		auto directory = settings.directory.empty() ?
			std::filesystem::temp_directory_path(error) : settings.directory;
		if (error)
			throw std::bad_alloc{};

		const auto filename = directory / (L"safc-buffer-" +
			std::to_wstring(GetCurrentProcessId()) + L"-" +
			std::to_wstring(counter.fetch_add(1)) + L".tmp");

		HANDLE file = CreateFileW(
			filename.c_str(),
			GENERIC_READ | GENERIC_WRITE | DELETE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
			nullptr);

		if (file == INVALID_HANDLE_VALUE)
			throw std::bad_alloc{};

		const auto wide_size = static_cast<std::uint64_t>(block_size);
		HANDLE mapping = CreateFileMappingW(
			file,
			nullptr,
			PAGE_READWRITE,
			static_cast<DWORD>(wide_size >> 32),
			static_cast<DWORD>(wide_size & 0xFFFFFFFFu),
			nullptr);

		if (!mapping)
		{
			CloseHandle(file);
			throw std::bad_alloc{};
		}

		void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, block_size);
		if (!view)
		{
			CloseHandle(mapping);
			CloseHandle(file);
			throw std::bad_alloc{};
		}

		auto* header = static_cast<block_header*>(view);
		*header = block_header{ block_size, &settings, file, mapping };

		settings.spilled_bytes += block_size;
		++settings.spilled_blocks_count;

		return header;
#else
		throw std::bad_alloc{};
#endif
	}
};

template<typename T>
struct spillable_allocator
{
	using value_type = T;
	using propagate_on_container_copy_assignment = std::true_type;
	using propagate_on_container_move_assignment = std::true_type;
	using propagate_on_container_swap = std::true_type;
	using is_always_equal = std::false_type;

	spill_settings* settings = nullptr;

	spillable_allocator() noexcept = default;
	spillable_allocator(spill_settings* settings) noexcept : settings(settings) {}
	template<typename U>
	spillable_allocator(const spillable_allocator<U>& other) noexcept : settings(other.settings) {}

	[[nodiscard]] T* allocate(std::size_t count)
	{
		static_assert(alignof(T) <= alignof(std::max_align_t));
		if (count > (~std::size_t(0)) / sizeof(T))
			throw std::bad_alloc{};

		return static_cast<T*>(spillable_memory::allocate(count * sizeof(T), settings));
	}

	void deallocate(T* pointer, std::size_t) noexcept
	{
		spillable_memory::deallocate(pointer);
	}

	// Any instance can release a block of any other one
	template<typename U>
	bool operator==(const spillable_allocator<U>&) const noexcept { return true; }
};

#endif // SAF_SPILLABLE_ALLOCATOR
