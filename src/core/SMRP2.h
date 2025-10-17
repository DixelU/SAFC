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
#include <atomic>
#include <iostream>
#include <ostream>
#include <cstring>
#include <algorithm>

#include "bbb_ffio.h"
#include "header_utils.h"

#include "PLC.h"
#include "CAT.h"

#include "function_wrapper.h"

struct logger_base
{
	virtual ~logger_base() = default;
	virtual void operator<<(std::string&& message) {};
	virtual std::string getLast() const { return ""; };
};

struct logger :
	public logger_base
{
protected:
	std::vector<std::string> messages;
	mutable std::mutex mtx;
public:
	~logger() override {}
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

struct singleline_logger :
	public logger_base
{
protected:
	std::string message;
	mutable std::mutex mtx;
public:
	~singleline_logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		this->message = std::move(message);
	}
	std::string getLast() const override
	{
		std::lock_guard locker(mtx);
		return message;
	}
};

struct printing_logger :
	public singleline_logger
{
public:
	~printing_logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		this->message = std::move(message);
		/*std::osyncstream(*/std::cout/*)*/ << this->message << std::endl;
	}
};

namespace 
{
	using base_type = std::uint8_t;
	FORCEDINLINE static void ostream_write(std::vector<base_type>& vec, std::ostream& out)
	{
		out.write(((char*)vec.data()), vec.size());;
	}
}


template<bool channels_split>
struct track_data;

template<>
struct track_data<false>
{
	using tick_type = std::uint64_t;
	using metasize_type = std::uint32_t;
	using sgsize_type = std::int32_t;
	using sgtick_type = std::int64_t;
	using ppq_type = std::uint16_t;
	
	std::vector<uint8_t> data;
	tick_type prev_tick;
	inline std::vector<uint8_t>& get_vec(const uint8_t&)
	{
		return data;
	}
	inline void clear()
	{
		data.clear();
		prev_tick = 0;
	}
	inline tick_type& get_tick(const uint8_t&)
	{
		return prev_tick;
	}
	inline tick_type count()
	{
		return !data.empty();
	}
	inline void reserve(const uint64_t& size)
	{
		data.reserve(size);
	}
	inline void dump(std::ostream& out, bool disallow_empty_tracks)
	{
		if (disallow_empty_tracks && data.empty())
			return;

		auto size_plus_4 = data.size() + 4;

		base_type header[8];
		header[0] = 'M';
		header[1] = 'T';
		header[2] = 'r';
		header[3] = 'k';
		header[4] = (size_plus_4 >> 24) & 0xFF;
		header[5] = (size_plus_4 >> 16) & 0xFF;
		header[6] = (size_plus_4 >> 8) & 0xFF;
		header[7] = (size_plus_4) & 0xFF;

		base_type ending[4];
		ending[0] = 0;
		ending[1] = 0xFF;
		ending[2] = 0x2F;
		ending[3] = 0;

		out.write((const char*) &header[0], sizeof(header));
		ostream_write(data, out);
		out.write((const char*)&ending[0], sizeof(ending));
	}
};

template<>
struct track_data<true>
{
	using tick_type = std::uint64_t;
	using base_type = std::uint8_t;
	using metasize_type = std::uint32_t;
	using sgsize_type = std::int32_t;
	using sgtick_type = std::int64_t;
	using ppq_type = std::uint16_t;

	track_data<false> data[16];

	inline std::vector<uint8_t>& get_vec(const uint8_t& channel)
	{
		return data[channel].get_vec(channel);
	}
	inline void clear()
	{
		for (auto& singleData : data)
			singleData.clear();
	}
	inline tick_type count()
	{
		tick_type counter = 0;
		for (auto& singleData : data)
			counter += singleData.count();
		return counter;
	}
	inline tick_type& get_tick(const uint8_t& channel)
	{
		return data[channel].get_tick(channel);
	}
	inline void dump(std::ostream& out, bool disallow_empty_tracks)
	{
		for (auto& singleData : data)
			singleData.dump(out, disallow_empty_tracks);
	}
	inline void reserve(const uint64_t& size)
	{
		for (auto& singleData : data)
			singleData.reserve(size);
	}
};

struct single_midi_processor_2
{
	inline static constexpr std::uint32_t MTrk_header = 1297379947;
	inline static constexpr std::uint32_t MThd_header = 1297377380;
	inline static constexpr std::uint64_t disable_tick = (~0ull);

	using tick_type = std::uint64_t;
	using base_type = std::uint8_t;
	using metasize_type = std::uint32_t;
	using sgsize_type = std::int32_t;
	using sgtick_type = std::int64_t;
	using ppq_type = std::uint16_t;

	inline static constexpr std::size_t tick_position = 0;
	inline static constexpr std::size_t event_type = 8;
	inline static constexpr std::size_t event_param1 = 9;
	inline static constexpr std::size_t event_param2 = 10;
	inline static constexpr std::size_t event_param3 = 11;
	inline static constexpr std::size_t event_param4 = 12;
	inline static constexpr std::size_t event_param5 = 13;

	// meta:   8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data
	inline static constexpr std::size_t event_meta_raw = 8 + 1 + 1 + 1 + 4;
	inline constexpr static std::size_t get_meta_param_index(std::size_t vlv_size, base_type parameter)
	{
		return event_meta_raw + vlv_size + parameter;
	}

	struct single_track_data
	{
		struct selection
		{
			inline static constexpr std::uint32_t default_tempo = 0x7A120;
			struct color_event
			{
				std::array<base_type, 0x0B> data;
				base_type size = 0xB;
				bool is_empty = true;
			};

			struct event_data_fixed_size_2
			{
				base_type field1 = 0;
				base_type field2 = 0;
				bool field1_is_set = true;
				bool field2_is_set = false;
			};

			// todo verify if map is faster than unordered_map
			std::map<std::uint16_t, base_type> key_events_at_selection_front;
			std::map<base_type, event_data_fixed_size_2> channel_events_at_selection_front;

			std::uint32_t frontal_tempo = default_tempo;
			color_event frontal_color_event{};

			inline void clear()
			{
				key_events_at_selection_front.clear();
				channel_events_at_selection_front.clear();
				//key_events_at_selection_front.reserve(1 << 16);
				//channel_events_at_selection_front.reserve(1 << 8);

				frontal_tempo = default_tempo;
				frontal_color_event = {};
			}
		};

		selection selection_data;

		inline void clear()
		{
			selection_data.clear();
		}
	};

	using data_iterator = std::vector<base_type>::iterator;

	/* returns false if event was disabled */
	using event_transforming_filter = dixelu::type_erased_function_container<
		bool(const data_iterator&,
			const data_iterator&,
			const data_iterator&,
			single_track_data&)>;
	// using this instead of std::function because of profiling transparency
	/* begin, end, cur */

	struct message_buffers
	{
		std::shared_ptr<logger_base> log;
		std::shared_ptr<logger_base> warning;
		std::shared_ptr<logger_base> error;
		std::atomic_uint64_t last_input_position;
		std::atomic_bool processing;
		std::atomic_bool finished;

		using logger_t = singleline_logger;

		message_buffers(bool is_console_oriented = false):
			log(is_console_oriented ? 
				std::make_shared<printing_logger>() : 
				std::make_shared<logger_t>()),
			warning(is_console_oriented ? 
				std::make_shared<printing_logger>() : 
				std::make_shared<logger_t>()),
			error(is_console_oriented ? 
				std::make_shared<printing_logger>() : 
				std::make_shared<logger_t>()),
			last_input_position(0),
			processing(false),
			finished(false)		
		{
		}
	};

	struct settings_obj
	{
		struct selection
		{
			tick_type begin;
			tick_type end;
			tick_type length;
			bool enable_selection_front;
			selection(tick_type begin = 0, tick_type length = ~0ULL) :
				begin(begin), length(length), end(begin + length), enable_selection_front(true) {}
		};

		struct legacy_filter_settings
		{
			bool pass_tempo = true;
			bool pass_pitch = true;
			bool pass_notes = true;
			bool pass_other = true;
			bool pass_sysex = false;

			bool piano_only = false;
		};

		struct tempo_override
		{
			double tempo_multiplier;
			metasize_type tempo_override_value;
			tempo_override():
				tempo_override_value(single_track_data::selection::default_tempo), tempo_multiplier(1)
			{
			}
			inline void set_override_value(double tempo)
			{
				tempo_override_value = (60000000. / tempo);
			}
			inline metasize_type process(metasize_type a) const 
			{
				if (tempo_override_value != single_track_data::selection::default_tempo)
					return tempo_override_value;
				return (std::min)((metasize_type)(a / tempo_multiplier), 0xFFFF7Fu);
			}
		};

		struct legacy_midi_standard
		{
			bool enable_zero_velocity = false;
			bool ignore_meta_rsb = false;
			bool rsb_compression = false;
		};

		struct details_data
		{
			tick_type initial_filesize;
			ppq_type group_id;
			bool inplace_mergable;
		};

		struct processing_details
		{
			bool apply_offset_after;
			bool remove_remnants;
			bool remove_empty_tracks;
			bool channel_split;
			bool whole_midi_collapse;
		};

		sgtick_type offset;
		ppq_type old_ppqn;
		ppq_type new_ppqn;
		std::uint32_t thread_id;

		legacy_midi_standard legacy;
		legacy_filter_settings filter;
		tempo_override tempo;
		selection selection_data;

		std::shared_ptr<byte_plc_core> volume_map;
		std::shared_ptr<_14bit_plc_core> pitch_map;
		std::shared_ptr<cut_and_transpose> key_converter;
		details_data details;
		processing_details proc_details;
	};

	struct processing_data
	{
		settings_obj settings;
		std_unicode_string filename;
		std_unicode_string postfix;

		std::string appearance_filename;
		std::atomic_uint64_t tracks_count;
	};

	FORCEDINLINE static void ostream_write(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& beg, 
		const std::vector<base_type>::iterator& end, 
		std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}

	FORCEDINLINE static void ostream_write(std::vector<base_type>& vec, std::ostream& out)
	{
		out.write(((char*)vec.data()), vec.size());;
	}

	FORCEDINLINE static uint8_t push_vlv(uint32_t value, std::vector<base_type>& vec)
	{
		return push_vlv_s(value, vec);
	};

	FORCEDINLINE static uint8_t push_vlv_s(tick_type s_value, std::vector<base_type>& vec)
	{
		constexpr uint8_t $7byte_mask = 0x7F, max_size = 11, $7byte_mask_size = 7;
		constexpr uint8_t $adjacent7byte_mask = ~$7byte_mask;
		uint64_t value = s_value;
		uint8_t __stack[max_size]{};
		uint8_t* const stack_end = __stack + max_size;
		uint8_t* stack = stack_end;
		do
		{
			stack--;
			*stack = (value & $7byte_mask);
			value >>= $7byte_mask_size;
			if (stack_end - stack != 1)
				*stack |= $adjacent7byte_mask;
		} while (value);

		auto stack_size = size_t(stack_end - stack);

		copy_back_traits::copy_back(
			vec,
			copy_back_traits::raw_storage{ stack, stack_size }
		);

		return stack_size;
	}

	FORCEDINLINE static uint64_t get_vlv(bbb_ffr& file_input)
	{
		uint64_t value = 0;
		base_type single_byte;
		do
		{
			single_byte = file_input.get();
			value = (value << 7) | (single_byte & 0x7F);
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	template<typename V>
	FORCEDINLINE static void __hack_resize(V& v, size_t newSize)
	{
		//hack beyond hacks
		struct vt { typename V::value_type v; vt() {} };
		static_assert(sizeof(vt[10]) == sizeof(typename V::value_type[10]), "alignment error");
		typedef std::vector<vt, typename std::allocator_traits<typename V::allocator_type>::template rebind_alloc<vt>> V2;
		reinterpret_cast<V2&>(v).resize(newSize);
	}

#define SAFC_S2B_PUSH_BACK
#define SAFC_S2B_HACK_RESIZE
	template<typename T>
	FORCEDINLINE static size_t push_back(std::vector<base_type>& vec, const T& value)
	{
		constexpr auto type_size = sizeof(T);
		auto current_index = vec.size();
#ifdef SAFC_S2B_PUSH_BACK
#ifdef SAFC_S2B_HACK_RESIZE
		__hack_resize(vec, current_index + type_size);
#else
		vec.resize(current_index + type_size);
#endif
		auto& value_ref = *(T*)(&vec[current_index]);
		value_ref = value;
#else
		auto begin = (const base_type*)&value;
		vec.insert(vec.end(), begin, begin + type_size);
#endif
		return type_size;
	}

	struct copy_back_traits
	{
		struct raw_storage
		{
			void* ptr;
			size_t size;
		};

		template<typename T>
		static void __copy_T_to_array(
			base_type* storage, 
			const typename std::enable_if<std::is_same<T, raw_storage>::value, T>::type& value)
		{
			std::memcpy(storage, value.ptr, value.size);
		}

		template<typename T>
		static void __copy_T_to_array(
			base_type* storage, 
			const typename std::enable_if<(!std::is_same<T, raw_storage>::value), T>::type& value)
		{
			*(T*)(storage) = value;
		}

		template<typename T>
		constexpr static typename std::enable_if<(!std::is_same<T, raw_storage>::value), size_t>::type
			size_of_contained_data(const T& /*data*/)
		{
			return sizeof(T);
		}

		template<typename T>
		constexpr static typename std::enable_if<std::is_same<T, raw_storage>::value, size_t>::type
			size_of_contained_data(const T& data)
		{
			return data.size;
		}

		template<typename _head>
		static size_t __total_size(const _head& head)
		{
			return size_of_contained_data(head);
		}

		template<typename _head, typename... _tail>
		static size_t __total_size(const _head& head, const _tail&... tail)
		{
			return
				size_of_contained_data(head) +
				__total_size(tail...);
		}

		template<typename _head, typename... _tail>
		FORCEDINLINE void static __copy_to_array(base_type* storage, _head head_value, _tail... tail_values)
		{
			__copy_T_to_array<_head>(storage, head_value);

			__copy_to_array(storage + sizeof(_head), tail_values...);
		}

		template<typename _head>
		FORCEDINLINE void static __copy_to_array(base_type* storage, _head head_value)
		{
			__copy_T_to_array<_head>(storage, head_value);
		}

		template<typename... _variadic_types>
		FORCEDINLINE void static copy_back(
			std::vector<base_type>& vec,
			_variadic_types... types)
		{
			auto total_size = __total_size(types...);
			auto current_index = vec.size();
			__hack_resize(vec, current_index + total_size);
			base_type* new_storage = &vec[current_index];
			__copy_to_array(new_storage, types...);
			//std::cout << "##SCE##" << std::endl;
		}
	};

	FORCEDINLINE static void copy_back(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& begin,
		const std::vector<base_type>::iterator& end, 
		const size_t expected_size)
	{
#ifdef SAFC_S2B_HACK_RESIZE
		//damn slow memcpy
		switch (expected_size)
		{
			case 1: { copy_back_traits::copy_back(vec, get_value<uint8_t>(begin, 0)); break; }
			case 2: { copy_back_traits::copy_back(vec, get_value<uint16_t>(begin, 0)); break; }
			case 3: { copy_back_traits::copy_back(vec, get_value<uint16_t>(begin, 0), get_value<uint8_t>(begin, 2)); break; }
			case 4: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0)); break; }
			case 5: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint8_t>(begin, 4)); break; }
			case 6: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint16_t>(begin, 2)); break; }
			case 7: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint16_t>(begin, 2), get_value<uint8_t>(begin, 6)); break; }
			case 8: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0)); break; }

			//case 9: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint8_t>(begin, 0 + 8)); break; }
			//case 10: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint16_t>(begin, 0 + 8)); break; }
			//case 11: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint16_t>(begin, 0 + 8), get_value<uint8_t>(begin, 2 + 8)); break; }
			//case 12: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8)); break; }
			//case 13: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint8_t>(begin, 4 + 8)); break; }
			//case 14: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint16_t>(begin, 2 + 8)); break; }
			//case 15: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint16_t>(begin, 2 + 8), get_value<uint8_t>(begin, 6 + 8)); break; }
			//case 16: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint64_t>(begin, 0 + 8)); break; }
			default:
			{
				//vec.insert(vec.end(), begin, end);
				std::size_t size = end - begin;
				copy_back_traits::copy_back(vec, copy_back_traits::raw_storage{ &*begin, size });
				break;
			}
		}
#else
		vec.insert(vec.end(), begin, end);
#endif
	}

	template<typename T>
	FORCEDINLINE static T& get_value(std::vector<base_type>& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static T& get_value(const std::vector<base_type>::iterator& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static bool is_valid_index(std::vector<base_type>& vec, size_t index)
	{
		return vec.size() <= index + sizeof(T);
	}

	struct data_buffers
	{
		std::vector<base_type> data_buffer;
		std::vector<base_type> meta_buffer;
	};

	inline static bool put_data_in_buffer(
		bbb_ffr& file_input,
		data_buffers& data_buffers,
		message_buffers& buffers,
		const settings_obj& settings,
		std::vector<std::vector<tick_type>>& current_polyphony
	)
	{
		auto& meta_buffer = data_buffers.meta_buffer;
		auto& data_buffer = data_buffers.data_buffer;

		tick_type current_tick = 0;

		std::uint32_t header = 0;
		std::uint32_t rsb = 0;
		std::uint32_t track_expected_size = 0;
		size_t noteoff_misses = 0;

		bool is_good = true;
		bool is_going = true;

		while (header != MTrk_header && file_input.good()) //MTrk = 1297379947
			header = (header << 8) | file_input.get();

		for (int i = 0; i < 4 && file_input.good(); i++)
			track_expected_size = (track_expected_size << 8) | file_input.get();

		data_buffer.reserve(track_expected_size * expected_size(0x80) / 4);

		if (file_input.eof())
			return false;

		const auto back_note_event_inserter =
		[](decltype(current_polyphony)& current_polyphony, 
			decltype(current_tick)& current_tick, 
			decltype(data_buffer)& data_buffer) {
			for (size_t idx = 0; idx < current_polyphony.size(); idx++)
			{
				auto& cur_note_stack = current_polyphony[idx];
				const base_type note = idx & 0xF;
				const base_type key = (idx >> 4)&0xFF;

				while (cur_note_stack.size())
				{
					auto note_ref_idx = cur_note_stack.back();

					const auto current_index = data_buffer.size();

					auto other_note_reference = note_ref_idx + event_param3;

					get_value<tick_type>(data_buffer, other_note_reference) = current_index;

					copy_back_traits::copy_back(
						data_buffer,
						current_tick,
						base_type(0x80 | note),
						key,
						base_type(0x40),
						note_ref_idx);

					cur_note_stack.pop_back();
				}
			}
		};

		while (file_input.good() && is_good && is_going)
		{
			base_type command = 0;
			base_type param_buffer = 0;

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

				if (command < 0xF0 || command == 0xFF)
					param_buffer = file_input.get();
				else 
					param_buffer = 0xFF;
			}

			if (command < 0x80)
			{
				is_good = false;
				(*buffers.error) << (std::to_string(file_input.tellg()) + ": Unexpected 0 RSB");
				break;
			}

			const auto current_index = data_buffer.size();

			switch (command >> 4)
			{
			case 0x8: case 0x9:	{
				base_type com = command;
				base_type key = param_buffer;
				base_type vel = file_input.get();
				tick_type reference = disable_tick;

				if (!settings.legacy.enable_zero_velocity) [[likely]]
					com ^= ((!bool(vel) & bool(com & 0x10)) << 4);
				else if (!vel && bool(com & 0x10))
					vel = 1;

				std::uint16_t key_polyindex = (com & 0xF) | (((std::uint16_t)key) << 4);
				bool polyphony_error = false;
				auto& current_polyphony_object = current_polyphony[key_polyindex];
				if (com & 0x10)
					current_polyphony_object.push_back(current_index); // hot smh
				else if (current_polyphony_object.size())
				{
					reference = current_polyphony_object.back();
					current_polyphony_object.pop_back(); // hot smh
					auto other_note_reference = reference + event_param3;
					if (true || is_valid_index<tick_type>(data_buffer, other_note_reference)) [[likely]]
						get_value<tick_type>(data_buffer, other_note_reference) = current_index;
					else
					{
						polyphony_error = true;
						(*buffers.warning) << (std::to_string(file_input.tellg()) +
							": Incorrect index of note reference " + std::to_string(other_note_reference));
					}
				}
				else
				{
					polyphony_error = true;
					++noteoff_misses;
					(*buffers.warning) << (std::to_string(file_input.tellg()) + ": OFF of nonON Note: " + std::to_string(noteoff_misses));
				}

				if (polyphony_error) [[unlikely]]
				{
					data_buffer.resize(current_index);
					continue;
				}

				copy_back_traits::copy_back(data_buffer, current_tick, com, key, vel, reference);

				break;
			}
			case 0xA: case 0xB: case 0xE: {
				base_type com = command;
				base_type p1 = param_buffer;
				base_type p2 = file_input.get();

				copy_back_traits::copy_back(data_buffer, current_tick, com, p1, p2);

				break;
			}
			case 0xC: case 0xD: {
				base_type com = command;
				base_type p1 = param_buffer;

				copy_back_traits::copy_back(data_buffer, current_tick, com, p1);

				break;
			}
			case 0xF: {
				base_type com = command;
				base_type type = param_buffer;

				is_going &= !(type == 0x2F && com == 0xFF);
				if (!settings.legacy.ignore_meta_rsb)
					rsb = 0;

				if (!is_going)
				{
					// ignore the end of track event;
					continue;
				}

				//if(com == 0xFF) // damn sysex broke it all >:C
					//push_back<base_type>(data_buffer, type);

				// 8 tick  1 type  1 metatype (except when sysex)  1 vlv size  4 size  ...<raw meta>~vlv+data

				metasize_type length = get_vlv(file_input);
				auto encoded_length = push_vlv_s(length, meta_buffer);

				for (std::size_t i = 0; i < length; ++i)
					meta_buffer.push_back(file_input.get());
				length += encoded_length;
				
				if (com == 0xFF) [[likely]]
				{
					copy_back_traits::copy_back(
						data_buffer,
						current_tick,
						com,
						type,
						encoded_length,
						length,
						copy_back_traits::raw_storage{
							meta_buffer.data(), meta_buffer.size() });
				}
				else
				{
					copy_back_traits::copy_back(
						data_buffer,
						current_tick,
						com,
						encoded_length,
						length,
						copy_back_traits::raw_storage{
							meta_buffer.data(), meta_buffer.size() });
				}

				meta_buffer.clear();

				break;
			}
			default: {
				(*buffers.error) << (std::to_string(file_input.tellg()) + ": Unknown event type " + std::to_string(command));
				break;
			}
			}
		}

		back_note_event_inserter(current_polyphony, current_tick, data_buffer);

		return is_good;
	}

	template<bool ready_for_write = false>
	FORCEDINLINE constexpr static std::ptrdiff_t expected_size(base_type v)
	{
		switch (v >> 4)
		{
			//MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
		case 0xA: case 0xB: case 0xE:
			return 8ll + 3ll;
			//MH_CASE(0x80) : MH_CASE(0x90) :
		case 0x8: case 0x9:
			return 8ll + 3ll + (8ll * !ready_for_write);
			//MH_CASE(0xC0) : MH_CASE(0xD0) :
		case 0xC: case 0xD:
			return 8ll + 2ll;
		default:
			return -1ll;
		}
	}

	inline static constexpr int gapped_size_legnth = 2 * (1) + 1;

	template<bool ready_for_write = false>
	FORCEDINLINE static typename std::enable_if<!ready_for_write, std::ptrdiff_t>::type expected_size(const data_iterator& cur)
	{
		const auto& type = cur[event_type];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return size;

		// 8 tick  1 type  1 metatype (when not sysex)  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		bool is_sysex = type != 0xFF;
		const auto& meta_size = get_value<metasize_type>(cur, event_param3 - is_sysex);

		return std::ptrdiff_t(event_meta_raw) + std::ptrdiff_t(meta_size) - is_sysex;
	}

	template<bool ready_for_write = false>
	FORCEDINLINE static typename std::enable_if<ready_for_write, std::array<std::ptrdiff_t, gapped_size_legnth>>::type
		expected_size(const data_iterator& cur)
	{
		const auto& type = cur[event_type];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return { size, 0, 0 };

		// 8 tick  1 type  1 metatype (when not sysex)  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		bool is_sysex = type != 0xFF;
		const auto& meta_size = get_value<metasize_type>(cur, event_param3 - is_sysex);

		return 
		{ 
			std::ptrdiff_t(event_param2 - is_sysex),
			std::ptrdiff_t(event_meta_raw - is_sysex),
			std::ptrdiff_t(event_meta_raw - is_sysex + meta_size)
		};
	}

	FORCEDINLINE static tick_type convert_ppq(tick_type value, ppq_type from, ppq_type to)
	{
		constexpr auto radix = 1ull << 32;
		auto hi = value >> 32;
		auto lo = value & (~0u);

		return (hi * to / from) * radix + (lo * to / from);
	}

	using filters_multimap = std::multimap<base_type, event_transforming_filter>;
	using filters_iterators = std::pair<filters_multimap::const_iterator, filters_multimap::const_iterator>;
	inline static std::array<filters_iterators, 16> make_filter_bounding_iters(const filters_multimap& map)
	{
		std::array<filters_iterators, 16> iters;
		for (int i = 0; i < 16; ++i)
			iters[i] = map.equal_range(i << 4);
		return iters;
	}

	inline static bool process_buffer(
		std::vector<base_type>& data_buffer,
		const std::array<filters_iterators, 16>& filters,
		single_track_data& std_ref,
		message_buffers& buffers)
	{
		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();

		auto size = data_buffer.size();

		if (size == 0) [[unlikely]]
			return ((*buffers.log) << "Empty buffer"), false;

		std::size_t i = 0;

		while (i < size)
		{
			std::size_t di = 0;

			if (i + 8 >= size) [[unlikely]]
			{
				(*buffers.error) << ("B*" + std::to_string(i) + ": unexpected end of buffer");
				break;
			}

			auto type = get_value<base_type>(data_buffer, i + event_type);
			auto channellless_type = type >> 4;
			di = expected_size(db_current);
			bool isActive = true;

			//todo: move "equal range" outside of the loop
			{
				auto& [begin, end] = filters[0];
				auto beg_copy = begin;

				for (; isActive && beg_copy != end; ++beg_copy) // hot smh
					isActive &= (beg_copy->second)(db_begin, db_end, db_current, std_ref); // hot smh
			}

			{
				auto& [begin, end] = filters[channellless_type];
				auto beg_copy = begin;

				for (; isActive && beg_copy != end; ++beg_copy) // hot smh
					isActive &= (beg_copy->second)(db_begin, db_end, db_current, std_ref); // hot smh
			}

			db_current += di;
			i += di;
		}
		return true;
	}

	inline static bool sort_buffer(
		std::vector<base_type>& data_buffer,
		single_track_data& std_ref,
		message_buffers& buffers)
	{
		auto db_current = data_buffer.begin();

#pragma pack (push, 1)
		struct data
		{
			tick_type tick;
			tick_type pointer;
			metasize_type length;

			bool operator<(const data& op) const
			{
				return tick < op.tick;
			}
		};
#pragma pack(pop)

		auto size = data_buffer.size();

		if (size == 0) // [[unlikely]]
			return ((*buffers.log) << "Empty buffer (sort)"), false;
		else
			(*buffers.log) << "Prepairing buffer (size: " + std::to_string(size) + ")";

		std::vector<data> data_pointers;
		data_pointers.reserve(2500000);

		tick_type i = 0;

		while (i < size)
		{
			metasize_type di = 0;

			if (i + 8 >= size) // [[unlikely]]
			{
				(*buffers.error) << ("B*" + std::to_string(i) + ": unexpected end of buffer");
				break;
			}

			auto& tick = get_value<tick_type>(db_current, tick_position);

			di = expected_size(db_current);

			if(tick != disable_tick)
				data_pointers.push_back(data{tick, i, di});

			db_current += di;
			i += di;
		}

		(*buffers.log) << "Sorting buffer (elements: " + std::to_string(data_pointers.size()) + ")";

		std::sort(data_pointers.begin(), data_pointers.end());

		(*buffers.log) << "Copying buffer (elements: " + std::to_string(data_pointers.size()) + ")";

		std::vector<base_type> sorted_data_buffer;
		sorted_data_buffer.reserve(data_buffer.size());
		for (auto& el : data_pointers)
		{
			copy_back(
				sorted_data_buffer,
				data_buffer.begin() + el.pointer,
				data_buffer.begin() + el.pointer + el.length,
				el.length);
		}

		sorted_data_buffer.swap(data_buffer);

		return true;
	}

	inline static std::multimap<base_type, event_transforming_filter> 
		filters_constructor(const settings_obj& settings)
	{
		std::multimap<base_type, event_transforming_filter> filters;

		const event_transforming_filter selection_filter = [&selection_data = settings.selection_data]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			
			bool before_selection = tick < selection_data.begin;
			bool after_selection = tick >= selection_data.end;

			if (!before_selection && !after_selection) [[likely]]
				return true;

			if (tick == disable_tick)
				return false;

			const auto& type = get_value<base_type>(cur, event_type);
			auto channelless_type = type & 0xF0;

			switch (channelless_type)
			{
			case 0x90:
			case 0x80:
			{
				bool is_note_on = channelless_type & 0x10;

				auto& reference_event_pair = get_value<tick_type>(cur, event_param3);
				auto& reference_event_tick = get_value<tick_type>(begin, reference_event_pair);
				bool trim_condition = 
					(is_note_on && before_selection && (
						reference_event_tick >= selection_data.begin && 
						reference_event_tick != disable_tick)
					) ||
					(!is_note_on && after_selection && (reference_event_tick < selection_data.end));

				if (!trim_condition)
				{
					reference_event_tick = disable_tick;
					tick = disable_tick;
					return false;
				}

				if (is_note_on)
				{
					auto& volume = get_value<base_type>(cur, event_param2);
					volume = 1;
					tick = selection_data.begin;
				}
				else
				{
					tick = selection_data.end - 1;
				}
				return true;
				break;
			}
			default:
			{
				if (before_selection)
				{
					switch (channelless_type)
					{
					case 0xA0:
					case 0xB0:
					{
						const auto& param1 = get_value<base_type>(cur, event_param1);
						const auto& param2 = get_value<base_type>(cur, event_param2);
						const std::uint16_t key = (type << 8) | (param1);
						auto& data = std_ref.selection_data.key_events_at_selection_front[key];
						data = param2;
						break;
					}
					case 0xC0:
					case 0xD0:
					{
						const auto& param = get_value<base_type>(cur, event_param1);
						const std::uint16_t key = type;
						auto& data = std_ref.selection_data.channel_events_at_selection_front[key];
						data.field1 = param;
						data.field2_is_set = false;
						break;
					}
					case 0xE0:
					{
						const auto& param1 = get_value<base_type>(cur, event_param1);
						const auto& param2 = get_value<base_type>(cur, event_param2);
						const std::uint16_t key = type;
						auto& data = std_ref.selection_data.channel_events_at_selection_front[key];
						data.field1 = param1;
						data.field2 = param2;
						data.field2_is_set = true;
						break;
					}
					case 0xF0:
					{
						if (type != 0xFF) [[unlikely]] break;
						const auto& meta_subtype = get_value<base_type>(cur, event_param1);

						// 8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data
						//const auto& vlv_size = get_value<base_type>(cur, event_param3);

						switch (meta_subtype)
						{
						case 0x51:
						{
							constexpr auto base_position = get_meta_param_index(1, 0);
							const auto& tempo_byte1 = get_value<base_type>(cur, base_position + 0);
							const auto& tempo_byte2 = get_value<base_type>(cur, base_position + 1);
							const auto& tempo_byte3 = get_value<base_type>(cur, base_position + 2);

							std_ref.selection_data.frontal_tempo = (tempo_byte1 << 16) | (tempo_byte2 << 8) | tempo_byte3;
							break;
						}
						[[unlikely]] case 0x0A:
						{
							constexpr auto base_position = get_meta_param_index(1, 0);

							const auto& size = get_value<base_type>(cur, base_position);
							if (size != 0x8 && size != 0xB) [[unlikely]]
								break;

							const auto& signature = get_value<base_type>(cur, base_position + 0);
							if (signature) [[unlikely]]
								break;
							std_ref.selection_data.frontal_color_event.is_empty = false;
							std_ref.selection_data.frontal_color_event.size = size;
							std_ref.selection_data.frontal_color_event.data[0] = signature;
							for(size_t i = 1; i < size; ++i)
								std_ref.selection_data.frontal_color_event.data[i] = get_value<base_type>(cur, event_param3 + i);
							break;
						}
						default:
							break;
						}
						break;
					}
					}
				}
				tick = disable_tick;
			}

			return false;
			}
		};

		const event_transforming_filter program_transform = [filtering = settings.filter]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool 
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			//const auto& type = get_value<base_type>(cur, event_type);

			if (!filtering.pass_other)
			{
				tick = disable_tick;
				return false;
			}

			auto& program = get_value<base_type>(cur, event_param1);

			if (filtering.piano_only)
				program = 0;

			return true;
		};

		const event_transforming_filter pitch_transform = [filtering = settings.filter, pm = settings.pitch_map]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			//const auto& type = get_value<base_type>(cur, event_type);

			if (!filtering.pass_pitch)
			{
				tick = disable_tick;
				return false;
			}

			if (pm)
			{
				auto& lsb = get_value<base_type>(cur, event_param1);
				auto& msb = get_value<base_type>(cur, event_param2);

				std::uint16_t pitch = ((msb & 0x7F) << 7) | (lsb & 0x7F);
				pitch = (*pm)[pitch];

				msb = (pitch >> 7) & 0x7F;
				lsb = (pitch & 0x7F);
			}

			return true;
		};

		const event_transforming_filter key_transform = [ filtering = settings.filter, cat = settings.key_converter, vm = settings.volume_map ]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			const auto& type = get_value<base_type>(cur, event_type);

			if (!filtering.pass_notes) [[unlikely]]
			{
				auto& reference_event_pair = get_value<tick_type>(cur, event_param3);
				auto& reference_event_tick = get_value<tick_type>(begin, reference_event_pair);

				reference_event_tick = disable_tick;
				tick = disable_tick;
				return false;
			}

			if (cat) [[unlikely]]
			{
				auto& key = get_value<base_type>(cur, event_param1);
				auto optkey = (*cat).process(key);

				if (!optkey)
				{
					auto& reference_event_pair = get_value<tick_type>(cur, event_param3);
					auto& reference_event_tick = get_value<tick_type>(begin, reference_event_pair);

					reference_event_tick = disable_tick;
					tick = disable_tick;
					return false;
				}

				key = *optkey;
			}

			if (vm && (type & 0x10)) [[unlikely]] // only for note-on events
			{
				auto& velocity = get_value<base_type>(cur, event_param2);
				velocity = (*vm)[velocity];

				if (!velocity)
				{
					auto& reference_event_pair = get_value<tick_type>(cur, event_param3);
					auto& reference_event_tick = get_value<tick_type>(begin, reference_event_pair);

					reference_event_tick = disable_tick;
					tick = disable_tick;
					return false;
				}
			}

			return true;
		};

		const event_transforming_filter meta_transform = [ filtering = settings.filter, tempo = settings.tempo ]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			const auto& event_class = get_value<base_type>(cur, event_type);
			const auto& meta_type = get_value<base_type>(cur, event_param1);

			// 8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data

			if (event_class == 0xFF && meta_type == 0x51)
			{
				if (!filtering.pass_tempo)
				{
					tick = disable_tick;
					return false;
				}

				constexpr auto base_position = get_meta_param_index(1, 0);

				auto& tempo_bit1 = get_value<base_type>(cur, base_position + 0);
				auto& tempo_bit2 = get_value<base_type>(cur, base_position + 1);
				auto& tempo_bit3 = get_value<base_type>(cur, base_position + 2);
				auto new_tempo = tempo.process((tempo_bit1 << 16) | (tempo_bit2 << 8) | tempo_bit3);

				if (!new_tempo)
				{
					tick = disable_tick;
					return false;
				}

				tempo_bit1 = (new_tempo >> 16) & 0xFF;
				tempo_bit2 = (new_tempo >> 8) & 0xFF;
				tempo_bit3 = (new_tempo) & 0xFF;

				return true;
			}

			switch (event_class)
			{
				case 0xF0:
				case 0xF7:
					if (!filtering.pass_sysex)
						tick = disable_tick;
				case 0xFF:
				default: 
					if (!filtering.pass_other)
						tick = disable_tick;
			}

			return tick != disable_tick;
		};

		const event_transforming_filter others_transform = [ filtering = settings.filter ]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			//const auto& type = get_value<base_type>(cur, event_type);
			if (!filtering.pass_other)
			{
				tick = disable_tick;
				return false;
			}

			return true;
		};

		const event_transforming_filter tick_positive_linear_transform =
			[old_ppqn = settings.old_ppqn,
			 new_ppqn = settings.new_ppqn, 
			 offset = settings.offset,
			 apply_offset_after = settings.proc_details.apply_offset_after]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);

			if (apply_offset_after)
			{
				auto new_tick = sgtick_type(
					old_ppqn == new_ppqn ?
					tick :
					convert_ppq(tick, old_ppqn, new_ppqn)) +
					offset;

				if (new_tick < 0)
					return (tick = disable_tick), false;
				return (tick = new_tick), true;
			}

			sgtick_type new_tick = sgtick_type(tick) + offset;
			if(new_tick < 0)
				return (tick = disable_tick), false;

			if (old_ppqn != new_ppqn)
				new_tick = convert_ppq(new_tick, old_ppqn, new_ppqn);
			return (tick = new_tick), true;
		};

		const event_transforming_filter rsb_compression_note_switchup = []
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& type = get_value<base_type>(cur, event_type);

			if ((type & 0xF0) == 0x80)
			{
				type = 0x90 | (type & 0x0F);

				auto& velocity = get_value<base_type>(cur, event_param2);
				velocity = 0;
			}

			return true;
		};


		filters.emplace( 0, selection_filter );
		filters.emplace( 0, tick_positive_linear_transform );

		if (settings.key_converter || settings.volume_map || !settings.filter.pass_notes)
		{
			filters.emplace(0x80, key_transform);
			filters.emplace(0x90, key_transform);
		}

		if (settings.legacy.rsb_compression)
			filters.emplace(0x80, rsb_compression_note_switchup);

		if (!settings.filter.pass_other)
		{
			filters.emplace(0xA0, others_transform);
			filters.emplace(0xB0, others_transform);
		}

		if (!settings.filter.pass_other || settings.filter.piano_only)
		{
			filters.emplace(0xC0, program_transform);
			filters.emplace(0xD0, others_transform);
		}

		if (settings.pitch_map || !settings.filter.pass_pitch)
			filters.emplace(0xE0, pitch_transform);

		filters.emplace(0xF0, meta_transform);


		return filters;
	}

	template<bool channels_split, typename _=void>
	struct track_data;

	template<typename _>
	struct track_data<false, _>
	{
		static constexpr bool fill_empty_track_with_at_least_one_event = false;

		std::vector<uint8_t> data;
		tick_type prev_tick;
		inline std::vector<uint8_t>& get_vec(uint8_t)
		{
			return data;
		}
		inline void swap(track_data<false>& track)
		{
			data.swap(track.data);
			std::swap(prev_tick, track.prev_tick);
		}
		inline void clear()
		{
			data.clear();
			prev_tick = 0;
		}
		inline tick_type& get_tick(uint8_t)
		{
			return prev_tick;
		}
		inline tick_type count(bool disallow_empty_tracks)
		{
			return !disallow_empty_tracks || !data.empty();
		}
		inline void reserve(uint64_t size)
		{
			data.reserve(size);
		}
		inline void dump(std::ostream& out, bool disallow_empty_tracks)
		{
			if (disallow_empty_tracks && data.empty())
				return;

			constexpr base_type ending[] = { 0x0, 0xFF, 0x2F, 0x0 };
			constexpr base_type placeholder[] = { 0x0, 0xFF, 0x01, 0x0 };

			const size_t ending_size =
				sizeof(ending) +
				fill_empty_track_with_at_least_one_event * sizeof(placeholder) * data.empty();
			auto size_plus_ending = data.size() + ending_size;

			base_type header[8];
			header[0] = 'M';
			header[1] = 'T';
			header[2] = 'r';
			header[3] = 'k';
			header[4] = (size_plus_ending >> 24) & 0xFF;
			header[5] = (size_plus_ending >> 16) & 0xFF;
			header[6] = (size_plus_ending >> 8) & 0xFF;
			header[7] = (size_plus_ending) & 0xFF;

			out.write((const char*) &header[0], sizeof(header));
			ostream_write(data, out);
			if (fill_empty_track_with_at_least_one_event && data.empty())
				out.write((const char*)&placeholder[0], sizeof(placeholder));
			out.write((const char*)&ending[0], sizeof(ending));
		}
		inline void swap_zero_and_channel(uint8_t) { return; }
	};

	template<typename _>
	struct track_data<true, _>
	{
		track_data<false> data[16]{};
		uint8_t last_channel{0};

		inline std::vector<uint8_t>& get_vec(uint8_t channel)
		{
			if (channel == 0xFF)
				channel = last_channel;
			return data[channel].get_vec(channel);
		}
		inline void swap_zero_and_channel(uint8_t channel)
		{
			data[0].swap(data[channel]);
		}
		inline void clear()
		{
			last_channel = 0;
			for (auto& singleData : data)
				singleData.clear();
		}
		inline tick_type count(bool disallow_empty_tracks)
		{
			tick_type counter = 0;
			for (auto& singleData : data)
				counter += singleData.count(disallow_empty_tracks);
			return counter;
		}
		inline tick_type& get_tick(uint8_t channel)
		{
			if (channel == 0xFF)
				channel = last_channel;
			return data[channel].get_tick(channel);
		}
		inline void dump(std::ostream& out, bool disallow_empty_tracks)
		{
			for (auto& singleData : data)
				singleData.dump(out, disallow_empty_tracks);
		}
		inline void reserve(uint64_t size)
		{
			for (auto& singleData : data)
				singleData.reserve(size);
		}
	};

	template<bool compression, bool channels_split>
	inline static void write_selection_front(
		tick_type selection_front_tick,
		single_track_data& std_ref,
		message_buffers& buffers,
		track_data<channels_split, void>& out_buffer, 
		bool& had_non_meta_events)
	{
		if (std_ref.selection_data.frontal_tempo != single_track_data::selection::default_tempo)
		{
			auto& prev_tick = out_buffer.get_tick(0xFF);
			auto& track_data = out_buffer.get_vec(0xFF);
			auto delta = selection_front_tick - prev_tick;
			prev_tick = selection_front_tick;
			push_vlv_s(delta, track_data);

			track_data.push_back(0xFF);
			track_data.push_back(0x51);
			track_data.push_back(0x03);
			track_data.push_back(std_ref.selection_data.frontal_tempo >> 16);
			track_data.push_back((std_ref.selection_data.frontal_tempo >> 8) & 0xFF);
			track_data.push_back(std_ref.selection_data.frontal_tempo & 0xFF);
		}
		std_ref.selection_data.frontal_tempo = single_track_data::selection::default_tempo;

		if (!std_ref.selection_data.frontal_color_event.is_empty)
		{
			auto& prev_tick = out_buffer.get_tick(0xFF);
			auto& track_data = out_buffer.get_vec(0xFF);
			auto delta = selection_front_tick - prev_tick;
			prev_tick = selection_front_tick;
			push_vlv_s(delta, track_data);

			track_data.push_back(0xFF);
			track_data.push_back(0x0A);
			track_data.push_back(std_ref.selection_data.frontal_color_event.size);
			for(int i = 0; i < std_ref.selection_data.frontal_color_event.size; ++i)
				track_data.push_back(std_ref.selection_data.frontal_color_event.data[i]);
		}
		std_ref.selection_data.frontal_color_event.is_empty = true;

		for (auto& [key_param1, param2] : std_ref.selection_data.key_events_at_selection_front)
		{
			auto event_kind = key_param1 >> 8;
			auto param_1 = key_param1 & 0xFF;
			auto channel = event_kind & 0xF;

			if (!had_non_meta_events)
			{
				had_non_meta_events = true;
				out_buffer.swap_zero_and_channel(channel);
			}

			auto& prev_tick = out_buffer.get_tick(channel);
			auto& track_data = out_buffer.get_vec(channel);
			auto delta = selection_front_tick - prev_tick;
			prev_tick = selection_front_tick;
			push_vlv_s(delta, track_data);

			track_data.push_back(event_kind);
			track_data.push_back(param_1);
			track_data.push_back(param2);
		}
		std_ref.selection_data.key_events_at_selection_front.clear();

		for (auto& [event_kind, data] : std_ref.selection_data.channel_events_at_selection_front)
		{
			auto channel = event_kind & 0xF;

			if (!had_non_meta_events)
			{
				had_non_meta_events = true;
				out_buffer.swap_zero_and_channel(channel);
			}

			auto& prev_tick = out_buffer.get_tick(channel);
			auto& track_data = out_buffer.get_vec(channel);
			auto delta = selection_front_tick - prev_tick;
			prev_tick = selection_front_tick;
			push_vlv_s(delta, track_data);

			track_data.push_back(event_kind);
			track_data.push_back(data.field1);
			if(data.field2_is_set)
				track_data.push_back(data.field2);
		}
		std_ref.selection_data.channel_events_at_selection_front.clear();
	}

	template<bool compression, bool channels_split>
	inline static bool write_track(
		std::vector<base_type>& data_buffer,
		single_track_data& std_ref,
		message_buffers& buffers,
		processing_data& settings_data,
		track_data<channels_split, void>& out_buffer)
	{
		out_buffer.clear();

		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();
		bool had_non_meta_events = false;

		auto size = data_buffer.size();
		std::size_t i = 0;
		uint8_t rsb = 0;

		bool first_tick = settings_data.settings.selection_data.enable_selection_front;

		auto write_selection_front_wrap = [&]() {
			if (first_tick) [[unlikely]]
			{
				auto original_front_tick = 
					sgtick_type(settings_data.settings.selection_data.begin) + settings_data.settings.offset;

				original_front_tick = (original_front_tick < 0) ? 0 : original_front_tick;

				auto selection_front_tick =
					convert_ppq(
						original_front_tick,
						settings_data.settings.old_ppqn,
						settings_data.settings.new_ppqn
					);

				write_selection_front<compression, channels_split>(
					selection_front_tick,
					std_ref,
					buffers,
					out_buffer, 
					had_non_meta_events);
				first_tick = false;
			}
		};

		while (i < size)
		{
			std::size_t di = 0;

			if (i + 8 >= size) [[unlikely]]
			{
				(*buffers.error) << ("B*" + std::to_string(i) + ": unexpected end of buffer");
				break;
			}
			
			auto tick = get_value<tick_type>(data_buffer, i);
			di = expected_size(db_current);
			if (tick != disable_tick)
			{
				write_selection_front_wrap();

				const auto& event_kind = get_value<base_type>(data_buffer, i + event_type);
				auto channel = event_kind & 0xF;

				const bool is_non_meta_event = (event_kind & 0xF0) != 0xF0;
				if (is_non_meta_event && !had_non_meta_events)
				{
					had_non_meta_events = is_non_meta_event;
					out_buffer.swap_zero_and_channel(channel);
				}

				if (!is_non_meta_event)
					channel = 0xFF;

				auto& prev_tick = out_buffer.get_tick(channel);
				auto& track_data = out_buffer.get_vec(channel);
				auto delta = tick - prev_tick;
				prev_tick = tick;

				push_vlv_s(delta, track_data);

				auto actual_event_size = expected_size<true>(db_current); // hot smh

				if (!compression)
				{
					copy_back(track_data, 
						db_current + event_type, 
						db_current + actual_event_size[0], 
						actual_event_size[0] - event_type);

					if(actual_event_size[1] != actual_event_size[2])
						copy_back(track_data,
							db_current + actual_event_size[1], 
							db_current + actual_event_size[2], 
							actual_event_size[2] - actual_event_size[1]);
				}
				else
				{
					if (rsb != event_kind || ((rsb & 0xF0) == 0xF0)) [[likely]]
						track_data.push_back(event_kind);

					copy_back(track_data,
						db_current + event_param1,
						db_current + actual_event_size[0],
						actual_event_size[0] - event_param1);

					if (actual_event_size[1] != actual_event_size[2])
						copy_back(track_data,
							db_current + actual_event_size[1],
							db_current + actual_event_size[2],
							actual_event_size[2] - actual_event_size[1]);

					rsb = event_kind;
				}
				first_tick = false;
			}
			db_current += di;
			i += di;
		}

		write_selection_front_wrap();

		return true;
	}
		
	template<bool channels_split>
	static void sync_processing(processing_data& data, message_buffers& loggers)
	{
		loggers.processing = true;

		std::vector<std::vector<tick_type>> polyphony_stacks(4096);
		data_buffers track_buffers;
		track_data<channels_split, void> write_buffer;

		for (auto& el : polyphony_stacks)
			el.reserve(1000);

		track_buffers.data_buffer.reserve(1ull << 26);
		write_buffer.reserve(1ull << (24 - 4 * channels_split));
		track_buffers.meta_buffer.reserve(1ull << 10);

		auto filters = filters_constructor(data.settings);
		auto filter_iters = make_filter_bounding_iters(filters);

		bbb_ffr file_input(data.filename.c_str());
		auto [file_output_ptr, fo_ptr] = 
			open_wide_stream<std::ostream>(data.filename + data.postfix, to_cchar_t("wb"));
		
		auto& file_output = *file_output_ptr;
		
		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());

		data.settings.old_ppqn = ((std::uint16_t)file_input.get()) << 8;
		data.settings.old_ppqn |= ((std::uint16_t)file_input.get());
		file_output.put(data.settings.new_ppqn >> 8);
		file_output.put(data.settings.new_ppqn & 0xFF);

		tick_type track_counter = 0;
		single_track_data track_processing_data;

		while (file_input.good())
		{
			if(!data.settings.proc_details.whole_midi_collapse)
				track_buffers.data_buffer.clear();

			bool is_readable = put_data_in_buffer(file_input, track_buffers, loggers, data.settings, polyphony_stacks);

			track_processing_data.selection_data.clear();
			track_processing_data.selection_data.frontal_tempo =
				data.settings.tempo.tempo_override_value;

			bool buffer_should_be_processed =
				(!data.settings.proc_details.whole_midi_collapse || !file_input.good()) &&
				track_buffers.data_buffer.size();

			bool sort_data_buffer_flag =
				data.settings.proc_details.whole_midi_collapse && !file_input.good();
			bool data_buffer_is_dumpable =
				!data.settings.proc_details.whole_midi_collapse || !file_input.good();

			if (buffer_should_be_processed)
			{
				bool successful_processing =
					process_buffer(track_buffers.data_buffer, filter_iters, track_processing_data, loggers);
				if (!successful_processing)
					(*loggers.error) << "Something went wrong during processing";
			}

			if (sort_data_buffer_flag)
			{
				bool successful_processing =
					sort_buffer(track_buffers.data_buffer, track_processing_data, loggers);
				if (!successful_processing)
					(*loggers.error) << "Something went wrong during sorting";
			}

			size_t current_count;

			if (data_buffer_is_dumpable)
			{
				if (data.settings.legacy.rsb_compression)
					write_track<true, channels_split>(
						track_buffers.data_buffer,
						track_processing_data,
						loggers,
						data,
						write_buffer);
				else
					write_track<false, channels_split>(
						track_buffers.data_buffer,
						track_processing_data,
						loggers,
						data,
						write_buffer);
			}

			current_count = write_buffer.count(data.settings.proc_details.remove_empty_tracks);

			if (data_buffer_is_dumpable)
			{
				track_counter += current_count;
				write_buffer.dump(file_output, data.settings.proc_details.remove_empty_tracks);
				write_buffer.clear();
			}

			loggers.last_input_position = file_input.tellg();
			(*loggers.log) << (std::to_string(track_counter) + " tracks processed. (" + std::to_string(current_count) + ") new.");
		}

		file_input.close();
		file_output.seekp(10, std::ios::beg);
		file_output.put(track_counter >> 8);
		file_output.put(track_counter & 0xFF);
		fclose(fo_ptr);
		delete file_output_ptr;

		data.tracks_count = track_counter;

		loggers.processing = false;
		loggers.finished = true;
	}
};

#endif //SAF_SMRP2
