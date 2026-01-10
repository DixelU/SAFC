#pragma once
#ifndef SAFC_SIMPLE_PLAYER
#define SAFC_SIMPLE_PLAYER

#include "Windows.h"

#include <vector>
#include <string>
#include <chrono>
#include <thread>

#include "../bbb_ffio.h"

#include "single_midi_processor_2.h"
#include "single_midi_info_collector.h"

template<typename T>
struct buffered_queue
{
private:
	static constexpr size_t slab_size = 4096;

	struct slab
	{
		std::aligned_storage_t<sizeof(T), alignof(T)> data[slab_size];
		const T* capacity_end;
		T* begin;
		T* end;

		std::unique_ptr<slab> next_slab;

		slab() :
			capacity_end(reinterpret_cast<T*>(data) + slab_size),
			begin(reinterpret_cast<T*>(data)),
			end(reinterpret_cast<T*>(data)),
			next_slab(nullptr)
		{}

		[[nodiscard]] size_t size() const { return end - begin; }
		[[nodiscard]] bool empty() const { return begin == end; }

		T& front() { return *begin; }
		const T& front() const { return *begin; }

		T& back() { return *(end - 1); }
		const T& back() const { return *(end - 1); }

		void pop() { begin->~T(); ++begin; }
		void push(T&& v) { ::new(end) T(std::move(v)); ++end; }

		bool full() const { return end == capacity_end; }

		void reset__unsafe() { begin = end = reinterpret_cast<T*>(data); }

		void clear() { while (begin != end) pop(); }
	};

	std::vector<std::unique_ptr<slab>> unused_slabs;
	std::unique_ptr<slab> head{};
	slab* tail = nullptr;

	std::unique_ptr<slab> get_empty_slab()
	{
		if (!unused_slabs.empty())
		{
			auto ptr = std::move(unused_slabs.back());
			unused_slabs.pop_back();
			return std::move(ptr);
		}

		return std::make_unique<slab>();
	}

	void dispose_of_empty_slab(std::unique_ptr<slab>&& slab_ptr)
	{
		slab_ptr->reset__unsafe();
		unused_slabs.push_back(std::move(slab_ptr));
	}

	struct iterator
	{
		slab* current_slab = nullptr;
		T* cur_obj = nullptr;

		T* operator->() { return cur_obj; }
		T& operator*() { return *cur_obj; }
		iterator& operator++(int)
		{

		}
	};
public:
	buffered_queue() = default;

	void push(T&& value)
	{
		if (!head)
		{
			head = get_empty_slab();
			tail = head.get();
		}

		if (tail->full())
		{
			tail->next_slab = get_empty_slab();
			tail = tail->next_slab.get();
		}

		tail->push(std::move(value));
	}

	void pop()
	{
		if (empty()) [[unlikely]]
			return;

		head->pop();

		if (!head->empty() || !head->full())
			return;

		auto new_head = std::move(head->next_slab);
		head->next_slab = nullptr;
		if (new_head == nullptr)
			tail = nullptr;

		dispose_of_empty_slab(std::move(head));
		head = std::move(new_head);
	}

	bool empty()
	{
		return !head || head->empty();
	}

	void clear()
	{
		while (!empty()) pop();
	}
};


struct simple_player
{
	simple_player() = default;

	void init()
	{
		enum_devices();
		init_midi_out(current_device);

		warnings = std::make_shared<printing_logger>("33");
	}

	using tick_type = std::uint64_t;

private:
	void try_init_kdmapi()
	{
		kdmapi_status = nullptr;

		auto moduleHandle = GetModuleHandleW(L"OmniMIDI");
		if (!moduleHandle)
			return;

		kdmapi_status = (decltype(kdmapi_status))GetProcAddress(moduleHandle, "IsKDMAPIAvailable");
		if (!kdmapi_status || !kdmapi_status())
			return;

		short_msg = (decltype(short_msg))GetProcAddress(moduleHandle, "SendDirectData");
	}

	bool open(std::wstring filename)
	{
		// prerequisite: this is a midi file with valid header;
		mmap = std::make_unique<bbb_mmap>(filename.c_str());
		info = midi_info{};

		if (!mmap || !mmap->good())
		{
			ThrowAlert_Warning("Unable to open MIDI file for playback");
			return false;
		}

		const auto begin = mmap->begin();
		const auto size = mmap->length();
		const auto end = begin + size;

		info.size = size;

		if (size < 18)
		{
			info.scanned = size;
			return false;
		}

		info.ppq = (begin[12] << 8) | (begin[13]);
		
		auto ptr = begin + 14;

		while (ptr < end)
		{
			read_through_one_track(ptr, end);
			info.scanned = ptr - begin;
		}

		info.scanned = end - begin;

		if (info.tracks.empty())
			return false;

		single_midi_info_collector::long_time time;
		uint64_t previous_tick = 0;
		uint32_t previous_tempo = 0x07A120;
		time.denominator = info.ppq;

		for (const auto& [tick, tempo_data] : info.tempo_tmp)
		{
			auto interval_seconds_rhs = dixelu::long_uint<0>{tick - previous_tick} * previous_tempo;

			time.numerator += interval_seconds_rhs;
			auto microseconds = time.numerator / time.denominator;

			info.time_map_mcsecs.emplace_back(tick, microseconds);

			previous_tick = tick;
			previous_tempo = tempo_data;
		}
	}

	void playback_thread()
	{
		
	}

	void read_through_one_track(const uint8_t*& cur, const uint8_t* end)
	{
		tick_type current_tick = 0;

		uint32_t header = 0;
		uint32_t rsb = 0;
		uint32_t track_expected_size = 0;

		bool is_good = true;
		bool is_reading = true;

		auto get_byte = [&cur, end]() { return get_value_and_increment(cur, end); };
		while (header != MTrk_header && (cur < end)) //MTrk = 1297379947
			header = (header << 8) | get_byte();

		if (cur >= end)
			return;

		for (int i = 0; i < 4; i++)
			track_expected_size = (track_expected_size << 8) | get_byte();

		const auto raw_track_data_begin = cur;
		while (cur < end && is_reading && is_good)
		{
			uint8_t command = 0;
			uint8_t param_buffer = 0;

			auto delta_time = get_vlv(cur, end);
			current_tick += delta_time;

			command = get_byte();
			if (command < 0x80)
			{
				param_buffer = command;
				command = rsb;
			}
			else
			{
				if (command < 0xF0)
					rsb = command;

				if (command < 0xF0 || command == 0xFF)
					param_buffer = get_byte();
				else
					param_buffer = 0xFF;
			}

			if (command < 0x80 || cur >= end)
			{
				ThrowAlert_Error("Byte " + std::to_string(cur - mmap->begin()) + ": Unexpected 0 RSB\nAt least one track was skipped!");
				is_good = false;
				break;
			}

			switch (command >> 4)
			{
			case 0x8: case 0x9:
			case 0xA: case 0xB: case 0xE:
				{
					get_byte();
					break;
				}
			case 0xC: case 0xD:
				{
					break;
				}
			case 0xF:
				{
					uint8_t com = command;
					uint8_t type = param_buffer;

					is_reading &= !(type == 0x2F && com == 0xFF);
					if (true /* disable if legacy rsb metas will misbehave again */)
						rsb = 0;

					if (!is_reading)
						continue;

					if (type == 0x51 && com == 0xFF)
					{
						auto length_3 = get_byte();
						uint32_t value = (get_byte() << 16);
						value |= (get_byte() << 8);
						value |= (get_byte());

						info.tempo_tmp[current_tick] = value;
					}

					auto length = get_vlv(cur, end);
					for (std::size_t i = 0; i < length; ++i)
						get_byte();

					break;
				}
			default:
				{
					ThrowAlert_Warning("Byte " + (std::to_string(cur - mmap->begin()) + ": Unknown event type " + std::to_string(command)));
					is_good = false;
					break;
				}
			}
		}

		if (!is_good || is_reading)
			return;

		if (track_expected_size != cur - raw_track_data_begin)
			warnings->report({track_expected_size, cur - raw_track_data_begin}, "Track size mismatch");

		info.ticks_length = std::max(current_tick, info.ticks_length);

		track_info track;
		track.begining = raw_track_data_begin;
		track.ending = cur;

		info.tracks.push_back(std::move(track));
	}

	inline static uint8_t get_value_and_increment(const uint8_t*& cur, const uint8_t* end)
	{
		if (cur < end)
			return *(cur++);
		return 0;
	}

	static uint64_t get_vlv(const uint8_t*& cur, const uint8_t* end)
	{
		uint64_t value = 0;

		uint8_t single_byte;
		do
		{
			single_byte = get_value_and_increment(cur, end);
			value = value << 7 | single_byte & 0x7F;
		}
		while (single_byte & 0x80);

		return value;
	}

	void enum_devices()
	{
		devices.clear();
		current_device = ~0ULL;

		auto count = midiOutGetNumDevs();
		devices.reserve(count);

		for (int i = 0; i < count; i++)
		{
			MIDIOUTCAPSW out;
			auto ret = midiOutGetDevCapsW(i, &out, sizeof(out));
			if (ret != MMSYSERR_NOERROR)
				continue;

			devices.emplace_back(std::move(out));
		}

		if (devices.size())
			current_device = 0; // select first one
	}

	void close_midi_out()
	{
		int attempts = 0;

		if (!hout)
			return;

		auto hout_copy = hout.load();
		hout = nullptr;

		midiOutReset(hout_copy);
		while (midiOutClose(hout_copy) != MMSYSERR_NOERROR && attempts++ < 8)
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

		if (attempts == 8)
			ThrowAlert_Error("Unable to close the MIDI out");
	}

	void init_midi_out(size_t device)
	{
		auto hout_copy = hout.load();
		if (hout_copy)
			return;

		if (device >= devices.size())
			return;

		try
		{
			if (midiOutOpen(&hout_copy, device, 0, 0, 0) != MMSYSERR_NOERROR)
			{
				std::wstring name = devices[device].szPname;
				auto readable_name = std::string(name.begin(), name.end());
				ThrowAlert_Error("Unable to open MIDI out '" + readable_name + "'!");
				hout = nullptr;
			}
			else
			{
				hout = hout_copy;
				short_msg = [](uint32_t msg) { midiOutShortMsg(hout, msg); };
			}

			if (!kdmapi_status)
				try_init_kdmapi();
		}
		catch (...)
		{
			ThrowAlert_Error("midiOutOpen() horribly failed...\n");
		}
	}

	void all_notes_off_channel(uint8_t channel)
	{
		channel &= 0x0F;

		short_msg(make_smsg(0xB0 | channel, 120));
		short_msg(make_smsg(0xB0 | channel, 121));
		short_msg(make_smsg(0xB0 | channel, 123));
	}

	void all_notes_off()
	{
		for (uint8_t c = 0; c < 16; c++)
			all_notes_off_channel(c);
	}

	inline static uint32_t make_smsg(uint8_t prog, uint8_t arg1, uint8_t arg2 = 0)
	{
		return (uint32_t)prog | (arg1 << 8) | (arg2 << 16);
	}

	struct track_info
	{
		const uint8_t* begining;
		const uint8_t* ending;
	};

	struct midi_info
	{
		std::vector<track_info> tracks;
		std::map<uint64_t, uint32_t> tempo_tmp;
		std::vector<std::pair<uint64_t, uint64_t>> time_map_mcsecs;
		uint64_t ticks_length;

		volatile uint64_t scanned{0};
		volatile bool ready{false};
		uint64_t size{0};

		uint16_t ppq{0};
	};

	struct track_playback_state
	{
		const uint8_t* position;

	};

	struct playback_state
	{
		tick_type load_tick;
		tick_type playback_tick;
	};

	std::shared_ptr<logger_base> warnings;

	std::unique_ptr<bbb_mmap> mmap;

	midi_info info;
	playback_state state;

	size_t current_device = ~0ULL;
	std::vector<MIDIOUTCAPSW> devices;
	inline static std::atomic<HMIDIOUT> hout;

	void(WINAPI* short_msg)(uint32_t msg) = nullptr;
	bool(WINAPI* kdmapi_status)() = nullptr;

	constexpr static std::uint32_t MTrk_header = 1297379947;
	constexpr static std::uint32_t MThd_header = 1297377380;
};

#endif