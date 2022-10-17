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
#include <ostream>

#include "../bbb_ffio.h"
#include "../SAFGUIF/header_utils.h"

#include "PLC.h"
#include "CAT.h"

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
		std::cout << this->message << std::endl;
	}
};

namespace 
{
	using base_type = std::uint8_t;
	FORCEDINLINE inline static void ostream_write(std::vector<base_type>& vec, std::ostream& out)
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

			std::map<std::uint16_t, base_type> key_events_at_selection_front;
			std::map<base_type, event_data_fixed_size_2> channel_events_at_selection_front;
			std::uint32_t frontal_tempo = default_tempo;
			color_event frontal_color_event;
		};

		selection selection_data;
	};

	using data_iterator = std::vector<base_type>::iterator;

	/* returns false if event was disabled */
	using event_transforming_filter = std::function<bool(const data_iterator&, const data_iterator&, const data_iterator&, single_track_data&)>;
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

		message_buffers():
			log(std::make_shared<logger_t>()),
			warning(std::make_shared<logger_t>()),
			error(std::make_shared<logger_t>()),
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

			bool piano_only = false;
		};

		struct tempo_override
		{
			double tempo_multiplier;
			metasize_type tempo_override_value;
			tempo_override():
				tempo_override_value(0), tempo_multiplier(1)
			{
			}
			inline void set_override_value(double tempo)
			{
				tempo_override_value = (60000000. / tempo);
			}
			inline metasize_type process(metasize_type a) const 
			{
				if (tempo_override_value)
					return tempo_override_value;
				return (std::min)((metasize_type)(a / tempo_multiplier), 0xFFFF7Fu);
			}
		};

		struct legacy_midi_standard
		{
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
			bool remove_remnants;
			bool remove_empty_tracks;
			bool channel_split;
		};

		sgtick_type offset;
		ppq_type old_ppqn;
		ppq_type new_ppqn;
		std::uint32_t thread_id;

		legacy_midi_standard legacy;
		legacy_filter_settings filter;
		tempo_override tempo;
		selection selection_data;

		std::shared_ptr<BYTE_PLC_Core> volume_map;
		std::shared_ptr<_14BIT_PLC_Core> pitch_map;
		std::shared_ptr<CutAndTransposeKeys> key_converter;
		details_data details;
		processing_details proc_details;
	};

	struct processing_data
	{
		settings_obj settings;
		std::wstring filename;
		std::wstring postfix;

		std::string appearance_filename;
		std::atomic_uint64_t tracks_count;
	};

	FORCEDINLINE inline static void ostream_write(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& beg, 
		const std::vector<base_type>::iterator& end, 
		std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}

	FORCEDINLINE inline static void ostream_write(std::vector<base_type>& vec, std::ostream& out)
	{
		out.write(((char*)vec.data()), vec.size());;
	}

	FORCEDINLINE inline static uint8_t push_vlv(const uint32_t& value, std::vector<base_type>& vec)
	{
		return push_vlv_s(value, vec);
	};

	FORCEDINLINE inline static uint8_t push_vlv_s(const tick_type& s_value, std::vector<base_type>& vec)
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

	FORCEDINLINE inline static uint64_t get_vlv(bbb_ffr& file_input)
	{
		uint64_t value = 0;
		base_type single_byte;
		do
		{
			single_byte = file_input.get();
			value = value << 7 | single_byte & 0x7F;
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	template<typename V>
	FORCEDINLINE inline static void __hack_resize(V& v, size_t newSize)
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
	FORCEDINLINE inline static size_t push_back(std::vector<base_type>& vec, const T& value)
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

	FORCEDINLINE inline static void copy_back(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& begin,
		const std::vector<base_type>::iterator& end, 
		const size_t expected_size)
	{
#ifdef SAFC_S2B_HACK_RESIZE
		//damn slow memcpy
		switch (expected_size)
		{
			case 1: { push_back(vec, get_value<uint8_t>(begin, 0)); break; }
			case 2: { push_back(vec, get_value<uint16_t>(begin, 0)); break; }
			case 3: { push_back(vec, get_value<uint16_t>(begin, 0)); push_back(vec, get_value<uint8_t>(begin, 2)); break; }
			case 4: { push_back(vec, get_value<uint32_t>(begin, 0)); break; }
			case 5: { push_back(vec, get_value<uint32_t>(begin, 0)); push_back(vec, get_value<uint8_t>(begin, 4)); break; }
			case 6: { push_back(vec, get_value<uint32_t>(begin, 0)); push_back(vec, get_value<uint16_t>(begin, 2)); break; }
			case 7: { push_back(vec, get_value<uint32_t>(begin, 0)); push_back(vec, get_value<uint16_t>(begin, 2)); push_back(vec, get_value<uint8_t>(begin, 6)); break; }
			case 8: { push_back(vec, get_value<uint64_t>(begin, 0)); break; }
			default:
			{
				vec.insert(vec.end(), begin, end);
				break;
			}
		}
#else
		vec.insert(vec.end(), begin, end);
#endif
	}

	template<typename T>
	FORCEDINLINE static T& get_value(std::vector<base_type>& vec, const size_t& index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static T& get_value(const std::vector<base_type>::iterator& vec, const size_t& index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static bool is_valid_index(std::vector<base_type>& vec, const size_t& index)
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

		if (file_input.eof())
			return false;

		const auto back_note_event_inserter = [&current_polyphony, &current_tick, &data_buffer]() {
			for (size_t idx = 0; idx < current_polyphony.size(); idx++)
			{
				auto& cur_note_stack = current_polyphony[idx];
				const auto note = idx & 0xF;
				const auto key = (idx >> 4)&0xFF;

				while (cur_note_stack.size())
				{
					auto note_ref_idx = cur_note_stack.back();

					const auto current_index = data_buffer.size();
					const auto tick_size = push_back<tick_type>(data_buffer, current_tick);

					auto other_note_reference = note_ref_idx + event_param3;

					get_value<tick_type>(data_buffer, other_note_reference) = current_index;

					push_back<base_type>(data_buffer, 0x80 | note);
					push_back<base_type>(data_buffer, key);
					push_back<base_type>(data_buffer, 0x40);
					push_back<tick_type>(data_buffer, note_ref_idx);

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
				param_buffer = file_input.get();
			}

			if (command < 0x80)
			{
				is_good = false;
				(*buffers.error) << (std::to_string(file_input.tellg()) + ": Unexpected 0 RSB");
				break;
			}

			const auto current_index = data_buffer.size();
			const auto tick_size = push_back<tick_type>(data_buffer, current_tick);

			switch (command >> 4)
			{
			case 0x8: case 0x9:	{
				base_type com = command;
				base_type key = param_buffer;
				base_type vel = file_input.get();
				tick_type reference = disable_tick;

				com ^= ((!vel && bool(com & 0x10)) << 4);

				std::uint16_t key_polyindex = (com & 0xF) | (((std::uint16_t)key) << 4);
				bool polyphony_error = false;
				auto& current_polyphony_object = current_polyphony[key_polyindex];
				if (com & 0x10)
					current_polyphony_object.push_back(current_index);
				else if (current_polyphony_object.size())
				{
					reference = current_polyphony_object.back();
					current_polyphony_object.pop_back();
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

				if (polyphony_error)
				{
					data_buffer.resize(current_index);
					continue;
				}

				push_back<base_type>(data_buffer, com);
				push_back<base_type>(data_buffer, key);
				push_back<base_type>(data_buffer, vel);
				push_back<tick_type>(data_buffer, reference);

				break;
			}
			case 0xA: case 0xB: case 0xE: {
				base_type com = command;
				base_type p1 = param_buffer;
				base_type p2 = file_input.get();

				push_back<base_type>(data_buffer, com);
				push_back<base_type>(data_buffer, p1);
				push_back<base_type>(data_buffer, p2);

				break;
			}
			case 0xC: case 0xD: {
				base_type com = command;
				base_type p1 = param_buffer;

				push_back<base_type>(data_buffer, com);
				push_back<base_type>(data_buffer, p1);

				break;
			}
			case 0xF: {
				base_type com = command;
				base_type type = param_buffer;

				is_going &= (type != 0x2F && com == 0xFF);
				if (!settings.legacy.ignore_meta_rsb)
					rsb = 0;

				if (!is_going)
				{
					// ignore the end of track event;
					data_buffer.resize(data_buffer.size() - event_type);
					break;
				}

				push_back<base_type>(data_buffer, com);
				push_back<base_type>(data_buffer, type);

				// 8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data

				auto length = get_vlv(file_input);
				auto encoded_length = push_vlv_s(length, meta_buffer);

				push_back<base_type>(data_buffer, encoded_length);

				for (std::size_t i = 0; i < length; ++i)
					push_back<base_type>(meta_buffer, file_input.get());
				length += encoded_length;

				push_back<metasize_type>(data_buffer, length);
				data_buffer.insert(data_buffer.end(), meta_buffer.begin(), meta_buffer.end());

				meta_buffer.clear();

				break;
			}
			default: {
				(*buffers.error) << (std::to_string(file_input.tellg()) + ": Unknown event type " + std::to_string(command));
				break;
			}
			}
		}

		back_note_event_inserter();

		return is_good;
	}

	template<bool ready_for_write = false>
	FORCEDINLINE inline constexpr static std::ptrdiff_t expected_size(const base_type& v)
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
	FORCEDINLINE inline static typename std::enable_if<!ready_for_write, std::ptrdiff_t>::type expected_size(const data_iterator& cur)
	{
		const auto& type = cur[8];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return size;

		// 8 tick  1 type  1 metatype  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		const auto& meta_size = get_value<metasize_type>(cur, event_param3);

		return std::ptrdiff_t(event_meta_raw) + std::ptrdiff_t(meta_size);
	}

	template<bool ready_for_write = false>
	FORCEDINLINE inline static typename std::enable_if<ready_for_write, std::array<std::ptrdiff_t, gapped_size_legnth>>::type
		expected_size(const data_iterator& cur)
	{
		const auto& type = cur[8];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return { size, 0, 0 };

		// 8 tick  1 type  1 metatype  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		const auto& meta_size = get_value<metasize_type>(cur, event_param3);

		return 
		{ 
			std::ptrdiff_t(event_param2), 
			std::ptrdiff_t(event_meta_raw),
			std::ptrdiff_t(event_meta_raw + meta_size)
		};
	}

	FORCEDINLINE inline static tick_type convert_ppq(const tick_type& value, const ppq_type& from, const ppq_type& to)
	{
		constexpr auto radix = 1ull << 32;
		auto hi = value >> 32;
		auto lo = value & (~0u);

		return value * (hi * to / from) * radix + (lo * to / from);
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
		std::array<filters_iterators, 16>& filters,
		single_track_data& std_ref,
		message_buffers& buffers)
	{
		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();

		auto size = data_buffer.size();

		if (size == 0) [[unlikely]]
			return ((*buffers.log) << "End of processing"), false;

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

				for (; isActive && beg_copy != end; ++beg_copy)
					isActive &= (beg_copy->second)(db_begin, db_end, db_current, std_ref);
			}

			{
				auto& [begin, end] = filters[channellless_type];
				auto beg_copy = begin;

				for (; isActive && beg_copy != end; ++beg_copy)
					isActive &= (beg_copy->second)(db_begin, db_end, db_current, std_ref);
			}

			db_current += di;
			i += di;
		}
		return true;
	}

	inline static std::multimap<base_type, event_transforming_filter> 
		filters_constructor(const settings_obj& settings)
	{
		std::multimap<base_type, event_transforming_filter> filters;

		const event_transforming_filter selection_filter = [selection_data = settings.selection_data]
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

			if (!filtering.pass_notes)
			{
				auto& reference_event_pair = get_value<tick_type>(cur, event_param3);
				auto& reference_event_tick = get_value<tick_type>(begin, reference_event_pair);

				reference_event_tick = disable_tick;
				tick = disable_tick;
				return false;
			}

			if (cat)
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

			if (vm && (type & 0x10)) // only for note-on events
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
			const auto& meta_type = get_value<base_type>(cur, event_param1);

			// 8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data

			if (meta_type == 0x51)
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

			if (!filtering.pass_other)
			{
				tick = disable_tick;
				return false;
			}

			return true;
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
			[selection_data = settings.selection_data, old_ppqn = settings.old_ppqn, new_ppqn = settings.new_ppqn, offset = settings.offset]
		(const data_iterator& begin, const data_iterator& end, const data_iterator& cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);

			if (offset < 0 && tick < -offset)
				return (tick = disable_tick), false;
			else
				tick += offset;

			if (old_ppqn == new_ppqn)
				return true;

			tick = convert_ppq(tick, old_ppqn, new_ppqn);
			return true;
		};

		filters.insert({ 0, selection_filter });
		filters.insert({ 0, tick_positive_linear_transform });
		filters.insert({ 0x80, key_transform });
		filters.insert({ 0x90, key_transform });
		filters.insert({ 0xA0, others_transform });
		filters.insert({ 0xB0, others_transform });
		filters.insert({ 0xC0, program_transform });
		filters.insert({ 0xD0, others_transform });
		filters.insert({ 0xE0, pitch_transform });
		filters.insert({ 0xF0, meta_transform });

		return filters;
	}

	template<bool compression, bool channels_split>
	inline static void write_selection_front(
		tick_type selection_front_tick,
		single_track_data& std_ref,
		message_buffers& buffers,
		track_data<channels_split>& out_buffer)
	{
		if (std_ref.selection_data.frontal_tempo != single_track_data::selection::default_tempo)
		{
			auto& prev_tick = out_buffer.get_tick(0);
			auto& track_data = out_buffer.get_vec(0);
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
			auto& prev_tick = out_buffer.get_tick(0);
			auto& track_data = out_buffer.get_vec(0);
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
		track_data<channels_split>& out_buffer)
	{
		out_buffer.clear();

		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();

		auto size = data_buffer.size();
		std::size_t i = 0;
		uint8_t rsb = 0;

		bool first_tick = settings_data.settings.selection_data.enable_selection_front;

		auto write_selection_front_wrap = [&]() {
			if (first_tick)
			{
				auto original_front_tick = settings_data.settings.selection_data.begin + settings_data.settings.offset;
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
					out_buffer);
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
				const auto channel = event_kind & 0xF;
				auto& prev_tick = out_buffer.get_tick(channel);
				auto& track_data = out_buffer.get_vec(channel);
				auto delta = tick - prev_tick;
				prev_tick = tick;

				push_vlv_s(delta, track_data);

				auto actual_event_size = expected_size<true>(db_current);

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
		track_data<channels_split> write_buffer;

		for (auto& el : polyphony_stacks)
			el.reserve(1000);
		track_buffers.data_buffer.reserve(1ull << 26);
		write_buffer.reserve(1ull << (24 - 4 * channels_split));
		track_buffers.meta_buffer.reserve(1ull << 10);

		auto filters = filters_constructor(data.settings);
		auto filter_iters = make_filter_bounding_iters(filters);

		bbb_ffr file_input(data.filename.c_str());
		auto [file_output_ptr, fo_ptr] = open_wide_stream<std::ostream>(data.filename + data.postfix, L"wb");
		auto& file_output = *file_output_ptr;
		
		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());

		data.settings.old_ppqn = ((std::uint16_t)file_input.get()) << 8;
		data.settings.old_ppqn |= ((std::uint16_t)file_input.get());
		file_output.put(data.settings.new_ppqn >> 8);
		file_output.put(data.settings.new_ppqn & 0xFF);

		tick_type track_counter = 0;
		while (file_input.good())
		{
			track_buffers.data_buffer.clear();

			put_data_in_buffer(file_input, track_buffers, loggers, data.settings, polyphony_stacks);
			single_track_data track_processing_data;
			if (!process_buffer(track_buffers.data_buffer, filter_iters, track_processing_data, loggers))
				break;

			if(data.settings.legacy.rsb_compression)
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

			auto current_count = write_buffer.count();
			track_counter += current_count;
			write_buffer.dump(file_output, data.settings.proc_details.remove_empty_tracks);
			write_buffer.clear();
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

#endif