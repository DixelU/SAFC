#pragma once

#ifndef SAF_SMRP2
#define SAF_SMRP2

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <memory>
#include <mutex>
#include <functional>
#include <stack>
#include <array>
#include <ostream>

#include "../bbb_ffio.h"
#include "../SAFGUIF/header_utils.h"

struct LoggerBase
{
	virtual ~LoggerBase() = default;
	virtual void operator<<(std::string&& message) {};
	virtual std::string getLast() const {};
};

struct Logger :
	LoggerBase
{
private:
	std::vector<std::string> messages;
	mutable std::mutex mtx;
public:
	~Logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		messages.push_back(std::move(message));
	}
	std::string getLast() const override
	{
		std::lock_guard locker(mtx);
		return (messages.size()) ? messages.back() : "";
	}
};

using DataIterator = std::vector<std::uint8_t>::iterator;

/* < 0 ~ event disabled; 0 ~ event wasn't parsed; > 0 ~ event was parsed, total event size */
using EventTransformingFilter = std::function<int(DataIterator data_end, DataIterator data_begin, DataIterator cur)>;


struct SingleMidiReprocessor_beta2
{
	inline static constexpr int MTrk = 1297379947;
	inline static constexpr int MThd = 1297377380;

	struct MessageBuffers
	{
		Logger log;
		Logger warning;
		Logger error;
	};

	struct Settings
	{

	};

	inline static void ostream_write(
		std::vector<std::uint8_t>& vec,
		const std::vector<std::uint8_t>::iterator& beg, 
		const std::vector<std::uint8_t>::iterator& end, 
		std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	inline static void ostream_write(std::vector<std::uint8_t>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	inline static uint8_t push_vlv(uint32_t value, std::vector<std::uint8_t>& vec) {
		return push_vlv_s(value, vec);
	};
	inline static uint8_t push_vlv_s(std::uint64_t s_value, std::vector<std::uint8_t>& vec)
	{
		constexpr uint8_t $7byte_mask = 0x7F, max_size = 10, $7byte_mask_size = 7;
		constexpr uint8_t $adjacent7byte_mask = ~$7byte_mask;
		uint64_t value = s_value;
		uint8_t stack[max_size];
		uint8_t size = 0;
		uint8_t r_size = 0;
		do
		{
			stack[size] = (value & $7byte_mask);
			value >>= $7byte_mask_size;
			if (size)
				stack[size] |= $adjacent7byte_mask;
			size++;
		} while (value);
		r_size = size;
		while (size)
			vec.push_back(stack[--size]);
		return r_size;
	}
	inline static uint64_t get_vlv(bbb_ffr& file_input)
	{
		uint64_t value = 0;
		std::uint8_t single_byte;
		do
		{
			single_byte = file_input.get();
			value = value << 7 | single_byte & 0x7F;
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	template<typename T>
	static size_t push_back(std::vector<std::uint8_t>& vec, T value)
	{
		auto type_size = sizeof(T);
		auto current_index = vec.size();
		vec.resize(current_index + type_size);
		auto& value_ref = *(T*)(&vec[current_index]);
		value_ref = value;
		return type_size;
	}

	template<typename T>
	static T& get_value(std::vector<std::uint8_t>& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	static bool is_valid_index(std::vector<std::uint8_t>& vec, size_t index)
	{
		return vec.size() <= index + sizeof(T);
	}

	inline static bool put_data_in_buffer(
		bbb_ffr& file_input,
		std::vector<std::uint8_t>& output,
		std::vector<std::uint8_t>& data_buffer,
		MessageBuffers& messages,
		const Settings& settings
	)
	{
		std::array<std::stack<std::uint64_t>, 4096> current_polyphony;
		std::uint64_t current_tick = 0;

		std::uint32_t header = 0;
		std::uint32_t rsb = 0;
		size_t noteoff_misses = 0;

		bool is_good = true;
		bool is_going = true;

		while (header != MTrk && file_input.good()) //MTrk = 1297379947
			header = (header << 8) | file_input.get();

		///here was header;
		for (int i = 0; i < 4 && file_input.good(); i++)
			file_input.get();

		if (file_input.eof())
			return false;

		while (file_input.good() && is_good && is_going)
		{
			std::uint8_t command = 0;
			std::uint8_t param_buffer = 0;

			auto delta_time = get_vlv(file_input);
			current_tick += delta_time;

			command = file_input.get();
			if (command < 0x80)
			{
				param_buffer = command;
				command = rsb;
			}
			else
			{
				if (command < 0xF0)
					rsb = command;
				param_buffer = file_input.get();
			}

			if (command < 0x80)
			{
				is_good = false;
				messages.error << (std::to_string(file_input.tellg()) + ": Unexpected 0 RSB");
				break;
			}

			const auto current_index = data_buffer.size();
			const auto tick_size = push_back<std::uint64_t>(data_buffer, current_tick);

			switch (command)
			{
				MH_CASE(0x80) : MH_CASE(0x90) :
				{
					std::uint8_t com = command;
					std::uint8_t key = param_buffer;
					std::uint8_t vel = file_input.get();
					std::uint64_t reference = ~std::uint64_t(0);

					com ^= ((!vel) << 4);

					std::uint16_t key_polyindex = (com & 0xF) | (((std::uint16_t)key) << 4);
					bool polyphony_error = false;
					auto& current_polyphony_object = current_polyphony[key_polyindex];
					if (com & 0x10)
						current_polyphony_object.push(current_index);
					else if (current_polyphony_object.size())
					{
						reference = current_polyphony_object.top();
						current_polyphony_object.pop();
						auto other_note_reference = reference + 8 + 3;
						if (is_valid_index<std::uint64_t>(data_buffer, other_note_reference))
							get_value<std::uint64_t>(data_buffer, other_note_reference) = current_index;
						else
						{
							polyphony_error = true;
							messages.warning << (std::to_string(file_input.tellg()) +
								": Incorrect index of note reference " + std::to_string(other_note_reference));
						}
					}
					else
					{
						polyphony_error = true;
						++noteoff_misses;
						messages.warning << (std::to_string(file_input.tellg()) + ": OFF of nonON Note: " + std::to_string(noteoff_misses));
					}

					if (polyphony_error)
					{
						data_buffer.resize(current_index);
						continue;
					}

					push_back<std::uint8_t>(data_buffer, com);
					push_back<std::uint8_t>(data_buffer, key);
					push_back<std::uint8_t>(data_buffer, vel);
					push_back<std::uint64_t>(data_buffer, current_tick);

					break;
				}
				MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
				{
					std::uint8_t com = command;
					std::uint8_t p1 = param_buffer;
					std::uint8_t p2 = file_input.get();

					push_back<std::uint8_t>(data_buffer, com);
					push_back<std::uint8_t>(data_buffer, p1);
					push_back<std::uint8_t>(data_buffer, p2);

					break;
				}
				MH_CASE(0xC0) : MH_CASE(0xD0) :
				{
					std::uint8_t com = command;
					std::uint8_t p1 = param_buffer;

					push_back<std::uint8_t>(data_buffer, com);
					push_back<std::uint8_t>(data_buffer, p1);

					break;
				}
			case 0xF0:
			case 0xF7:
			case 0xFF:
			{
				std::uint8_t com = command;
				std::uint8_t type = param_buffer;

				push_back<std::uint8_t>(data_buffer, com);
				push_back<std::uint8_t>(data_buffer, type);

				auto length = get_vlv(file_input);
				push_back<std::uint32_t>(data_buffer, type);

				for (std::size_t i = 0; i < length; ++i)
					push_back<std::uint8_t>(data_buffer, file_input.get());

				is_going &= (type != 0x2F);

				break;
			}
			default:
			{
				messages.error << (std::to_string(file_input.tellg()) + ": Unknown event type " + std::to_string(command));
				break;
			}
			}
		}
		return is_good;
	}

	inline static std::ptrdiff_t expected_size(std::uint8_t v)
	{
		switch (v)
		{
			MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
				return 8 + 3;
			MH_CASE(0x80) : MH_CASE(0x90) :
				return 8 + 3 + 8;
			MH_CASE(0xC0) : MH_CASE(0xD0) :
				return 8 + 2;
		default:
			return -1;
		}
	}

	inline static bool process_buffer(
		std::vector<std::uint8_t>& data_buffer,
		const std::multimap<std::uint8_t, EventTransformingFilter>& filters,
		MessageBuffers& messages,
		const Settings& settings)
	{
		size_t i = 0;

		while (true)
		{
			size_t di = 0;

			if (i + 8 >= data_buffer.size()) [[unlikely]]
			{
				messages.error << ("B*" + std::to_string(i) + ": unexpected end of buffer");
				break;
			}

			auto type = get_value<std::uint8_t>(data_buffer, i + 8);
			auto channellless_type = type & 0xF0;

			

		}
	}

};

#endif SAF_SMRP2