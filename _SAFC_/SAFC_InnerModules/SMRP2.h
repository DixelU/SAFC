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

#include "PLC.h"
#include "CAT.h"

struct logger_base
{
	virtual ~logger_base() = default;
	virtual void operator<<(std::string&& message) {};
	virtual std::string getLast() const { return ""; };
};

struct logger :
	logger_base
{
private:
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

struct single_midi_processor_beta2
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

			std::unordered_map<std::uint16_t, std::uint16_t> events_at_selection_front;
			std::uint32_t frontal_tempo = default_tempo;
			color_event frontal_color_event;
		};

		selection selection_data;
	};

	using data_iterator = std::vector<base_type>::iterator;

	/* returns false if event was disabled */
	using event_transforming_filter = std::function<bool(data_iterator, data_iterator, data_iterator, single_track_data&)>;
	/* begin, end, cur */

	struct message_buffers
	{
		std::shared_ptr<logger> log;
		std::shared_ptr<logger> warning;
		std::shared_ptr<logger> error;
	};

	struct settings_obj
	{
		struct selection
		{
			const tick_type begin;
			const tick_type end;
			const sgtick_type length;
			selection(tick_type begin = 0, sgtick_type length = -1) :
				begin(begin), length(length), end(begin + length) {	}
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
				return (std::max)((metasize_type)(a / tempo_multiplier), 0xFFFF7Fu);
			}
		};

		struct legacy_midi_standard
		{
			bool ignore_meta_rsb = false;
			bool rsb_compression = false;
		};

		sgtick_type offset;
		ppq_type old_ppqn;
		ppq_type new_ppqn;

		legacy_midi_standard legacy;
		legacy_filter_settings filter;
		tempo_override tempo;
		selection selection;

		std::shared_ptr<BYTE_PLC_Core> volume_map;
		std::shared_ptr<_14BIT_PLC_Core> pitch_map;
		std::shared_ptr<CutAndTransposeKeys> key_converter;
	};

	struct processing_data
	{
		settings_obj settigns;
		std::wstring filename;
		std::wstring postfix;
	};

	inline static void ostream_write(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& beg, 
		const std::vector<base_type>::iterator& end, 
		std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	inline static void ostream_write(std::vector<base_type>& vec, std::ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	inline static uint8_t push_vlv(uint32_t value, std::vector<base_type>& vec) {
		return push_vlv_s(value, vec);
	};
	inline static uint8_t push_vlv_s(tick_type s_value, std::vector<base_type>& vec)
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
		base_type single_byte;
		do
		{
			single_byte = file_input.get();
			value = value << 7 | single_byte & 0x7F;
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	template<typename T>
	static size_t push_back(std::vector<base_type>& vec, T value)
	{
		auto type_size = sizeof(T);
		auto current_index = vec.size();
		vec.resize(current_index + type_size);
		auto& value_ref = *(T*)(&vec[current_index]);
		value_ref = value;
		return type_size;
	}

	template<typename T>
	static T& get_value(std::vector<base_type>& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	static T& get_value(std::vector<base_type>::iterator vec, size_t index)
	{
		return *(T*)&vec[index];
	}


	template<typename T>
	static bool is_valid_index(std::vector<base_type>& vec, size_t index)
	{
		return vec.size() <= index + sizeof(T);
	}

	inline static bool put_data_in_buffer(
		bbb_ffr& file_input,
		std::vector<base_type>& data_buffer,
		message_buffers& buffers,
		const settings_obj& settings
	)
	{
		std::array<std::stack<tick_type>, 4096> current_polyphony;
		tick_type current_tick = 0;

		std::uint32_t header = 0;
		std::uint32_t rsb = 0;
		size_t noteoff_misses = 0;

		bool is_good = true;
		bool is_going = true;

		while (header != MTrk_header && file_input.good()) //MTrk = 1297379947
			header = (header << 8) | file_input.get();

		///here was header;
		for (int i = 0; i < 4 && file_input.good(); i++)
			file_input.get();

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
					auto note_ref_idx = cur_note_stack.top();

					const auto current_index = data_buffer.size();
					const auto tick_size = push_back<tick_type>(data_buffer, current_tick);

					auto other_note_reference = note_ref_idx + event_param3;

					get_value<tick_type>(data_buffer, other_note_reference) = current_index;

					push_back<base_type>(data_buffer, 0x80 | note);
					push_back<base_type>(data_buffer, key);
					push_back<base_type>(data_buffer, 0x40);
					push_back<tick_type>(data_buffer, note_ref_idx);

					cur_note_stack.pop();
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

			switch (command)
			{
				MH_CASE(0x80) : MH_CASE(0x90) :
				{
					base_type com = command;
					base_type key = param_buffer;
					base_type vel = file_input.get();
					tick_type reference = disable_tick;

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
				MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
				{
					base_type com = command;
					base_type p1 = param_buffer;
					base_type p2 = file_input.get();

					push_back<base_type>(data_buffer, com);
					push_back<base_type>(data_buffer, p1);
					push_back<base_type>(data_buffer, p2);

					break;
				}
				MH_CASE(0xC0) : MH_CASE(0xD0) :
				{
					base_type com = command;
					base_type p1 = param_buffer;

					push_back<base_type>(data_buffer, com);
					push_back<base_type>(data_buffer, p1);

					break;
				}
			case 0xF0:
			case 0xF7:
			case 0xFF:
			{
				base_type com = command;
				base_type type = param_buffer;

				is_going &= (type != 0x2F && com != 0xFF);
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

				auto length = get_vlv(file_input);
				push_back<metasize_type>(data_buffer, length);

				for (std::size_t i = 0; i < length; ++i)
					push_back<base_type>(data_buffer, file_input.get());

				break;
			}
			default:
			{
				(*buffers.error) << (std::to_string(file_input.tellg()) + ": Unknown event type " + std::to_string(command));
				break;
			}
			}
		}

		back_note_event_inserter();

		return is_good;
	}

	inline constexpr static std::ptrdiff_t expected_size(base_type v)
	{
		switch (v)
		{
			MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
				return 8ll + 3ll;
			MH_CASE(0x80) : MH_CASE(0x90) :
				return 8ll + 3ll + 8ll;
			MH_CASE(0xC0) : MH_CASE(0xD0) :
				return 8ll + 2ll;
		default:
			return -1ll;
		}
	}

	inline static std::ptrdiff_t expected_size(data_iterator cur)
	{
		const auto& type = cur[8];
		auto size = expected_size(type);
		if (size > 0)
			return size;

		const auto& meta_size = get_value<metasize_type>(cur, 10);
		return std::ptrdiff_t(8 + 2 + 4) + std::ptrdiff_t(meta_size);
	}

	inline static tick_type convert_ppq(tick_type value, ppq_type from, ppq_type to)
	{
		constexpr auto radix = 1ull << 32;
		auto hi = value >> 32;
		auto lo = value & (~0u);

		return value * (hi * to / from) * radix + (lo * to / from);
	}

	inline static bool process_buffer(
		std::vector<base_type>& data_buffer,
		const std::multimap<base_type, event_transforming_filter>& filters,
		single_track_data& std_ref,
		message_buffers& buffers)
	{
		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();

		auto size = data_buffer.size();
		std::size_t i = 0;

		if (size < 9) [[unlikely]]
		{
			(*buffers.error) << "BP: Empty/corrupted buffer";
			return false;
		}

		while (i < size)
		{
			std::size_t di = 0;

			if (i + 8 >= size) [[unlikely]]
			{
				(*buffers.error) << ("B*" + std::to_string(i) + ": unexpected end of buffer");
				break;
			}

			auto type = get_value<base_type>(data_buffer, i + event_type);
			auto channellless_type = type & 0xF0;
			di = expected_size(db_current);
			bool isActive = true;

			{
				auto [begin, end] = filters.equal_range(0x00);

				for (; isActive && begin != end; ++begin)
					isActive &= (begin->second)(db_begin, db_end, db_current, std_ref);
			}

			{
				auto [begin, end] = filters.equal_range(channellless_type);

				for (; isActive && begin != end; ++begin)
					isActive &= (begin->second)(db_begin, db_end, db_current, std_ref);
			}

			db_current += di;
			i += di;
		}
		return true;
	}

	inline static std::multimap<base_type, event_transforming_filter> filters_constructor(const settings_obj& settings)
	{
		std::multimap<base_type, event_transforming_filter> filters;

		event_transforming_filter selection = [selection = settings.selection]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			
			bool before_selection = tick < selection.begin;
			bool after_selection = tick >= selection.end;

			if (!before_selection && !after_selection) [[likely]]
				return true;

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
				bool trim_condition = (is_note_on && before_selection && (reference_event_tick >= selection.begin)) ||
										(!is_note_on && after_selection && (reference_event_tick < selection.end));
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
					tick = selection.begin;
				}
				else
				{
					tick = selection.end - 1;
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
						std::uint16_t key = (type << 8) | (param1);
						std_ref.selection_data.events_at_selection_front[key] = param2;
						break;
					}
					case 0xC0:
					case 0xD0:
					{
						const auto& param = get_value<base_type>(cur, event_param1);
						std::uint16_t key = (type << 8);
						std_ref.selection_data.events_at_selection_front[key] = param;
						break;
					}
					case 0xE0:
					{
						const auto& param1 = get_value<base_type>(cur, event_param1);
						const auto& param2 = get_value<base_type>(cur, event_param2);
						std::uint16_t key = (type << 8);
						std_ref.selection_data.events_at_selection_front[key] = (param1 << 8) | param2;
						break;
					}
					case 0xF0:
					{
						if (type != 0xFF) [[unlikely]] break;
						const auto& meta_subtype = get_value<base_type>(cur, event_param1);

						switch (meta_subtype)
						{
						case 0x51:
						{
							const auto& tempo_byte1 = get_value<base_type>(cur, event_param3);
							const auto& tempo_byte2 = get_value<base_type>(cur, event_param4);
							const auto& tempo_byte3 = get_value<base_type>(cur, event_param5);

							std_ref.selection_data.frontal_tempo = (tempo_byte1 << 16) | (tempo_byte2 << 8) | tempo_byte3;
							break;
						}
						[[unlikely]] case 0x0A:
						{
							const auto& size = get_value<base_type>(cur, event_param2);
							if (size != 0x8 && size != 0xB) [[unlikely]]
								break;
							const auto& signature = get_value<base_type>(cur, event_param3);
							if (!signature) [[unlikely]]
								break;
							std_ref.selection_data.frontal_color_event.is_empty = false;
							std_ref.selection_data.frontal_color_event.size = size;
							std_ref.selection_data.frontal_color_event.data[0] = signature;
							for(size_t i = 1; i < size; ++i)
								std_ref.selection_data.frontal_color_event.data[i] = get_value<base_type>(cur, event_param3 + i);
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

		event_transforming_filter program_transform = [filtering = settings.filter]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool 
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

		event_transforming_filter pitch_transform = [filtering = settings.filter, pm = settings.pitch_map]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
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

		event_transforming_filter key_transform = [ filtering = settings.filter, cat = settings.key_converter, vm = settings.volume_map ]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
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

		event_transforming_filter meta_transform = [ filtering = settings.filter, tempo = settings.tempo ]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);
			const auto& meta_type = get_value<base_type>(cur, event_param1);

			if (meta_type == 0x51)
			{
				if (!filtering.pass_tempo)
				{
					tick = disable_tick;
					return false;
				}

				auto& tempo_bit1 = get_value<base_type>(cur, event_param3);
				auto& tempo_bit2 = get_value<base_type>(cur, event_param4);
				auto& tempo_bit3 = get_value<base_type>(cur, event_param5);
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

		event_transforming_filter others_transform = [ filtering = settings.filter ]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
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

		event_transforming_filter tick_positive_linear_transform =
			[selection = settings.selection, old_ppqn = settings.old_ppqn, new_ppqn = settings.new_ppqn, offset = settings.offset]
		(data_iterator begin, data_iterator end, data_iterator cur, single_track_data& std_ref) -> bool
		{
			auto& tick = get_value<tick_type>(cur, tick_position);

			if (tick < selection.begin)
				return (tick = disable_tick), false;
			if (tick >= selection.end)
				return (tick = disable_tick), false;

			if (offset < 0 && tick < -offset)
				return (tick = disable_tick), false;
			else
				tick += offset;

			convert_ppq(tick, old_ppqn, new_ppqn);
			return true;
		};

		filters.insert({ 0, selection });
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

	template<bool channels_split>
	struct track_data;

	template<>
	struct track_data<false>
	{
		std::vector<uint8_t> data;
		tick_type prev_tick;
		inline std::vector<uint8_t>& get_vec(uint8_t)
		{
			return data;
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
		inline tick_type count()
		{
			return !data.empty();
		}
		inline void dump(std::ostream& out)
		{
			if (data.empty())
				return;

			base_type header[8];
			header[0] = 'M';
			header[1] = 'T';
			header[2] = 'r';
			header[3] = 'k';
			header[4] = (data.size() >> 24) & 0xFF;
			header[5] = (data.size() >> 16) & 0xFF;
			header[6] = (data.size() >> 8) & 0xFF;
			header[7] = (data.size()) & 0xFF;

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
		track_data<false> data[16];

		inline std::vector<uint8_t>& get_vec(uint8_t channel)
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
		inline tick_type& get_tick(uint8_t channel)
		{
			return data[channel].get_tick(channel);
		}
		inline void dump(std::ostream& out)
		{
			for (auto& singleData : data)
				singleData.dump(out);
		}
	};

	template<bool compression, bool channels_split>
	inline static bool write_track(
		std::vector<base_type>& data_buffer,
		single_track_data& std_ref,
		message_buffers& buffers,
		track_data<channels_split>& out_buffer)
	{
		out_buffer.clear();

		auto db_begin = data_buffer.begin();
		auto db_end = data_buffer.end();
		auto db_current = data_buffer.begin();

		auto size = data_buffer.size();
		std::size_t i = 0;
		uint8_t rsb = 0;

		// todo: add selection's edge data dump

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
				const auto& event_type = get_value<base_type>(data_buffer, i + event_type);
				const auto channel = event_type & 0xF;
				auto& prev_tick = out_buffer.get_tick(channel);
				auto& track_data = out_buffer.get_vec(channel);
				auto delta = tick - prev_tick;
				prev_tick = tick;
				push_vlv_s(delta, track_data);

				if(!compression)
					track_data.insert(track_data.end(), db_current + event_type, db_current + di);
				else
				{
					if (rsb != event_type || ((rsb & 0xF0) == 0xF0)) [[likely]]
						track_data.push_back(event_type);
					track_data.insert(track_data.end(), db_current + event_param1, db_current + di);
					rsb = event_type;
				}
			}

			db_current += di;
			i += di;
		}
		return true;
	}
		
	template<bool channels_split>
	static void sync_processing(processing_data data, message_buffers loggers)
	{
		bbb_ffr file_input(data.filename.c_str());
		std::ofstream file_output(data.filename + data.postfix, std::ios::binary | std::ios::out);

		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());

		data.settigns.old_ppqn = ((std::uint16_t)file_input.get()) << 8;
		data.settigns.old_ppqn |= ((std::uint16_t)file_input.get());
		file_output.put(data.settigns.new_ppqn >> 8);
		file_output.put(data.settigns.new_ppqn & 0xFF);

		auto filters = filters_constructor(data.settigns);

		std::vector<base_type> track_buffer;
		track_data<channels_split> write_buffer;

		tick_type track_counter = 0;
		while (file_input.good())
		{
			put_data_in_buffer(file_input, track_buffer, loggers, data.settigns);
			single_track_data track_processing_data;
			process_buffer(track_buffer, filters, track_processing_data, loggers);

			if(data.settigns.legacy.rsb_compression)
				write_track<true, channels_split>(track_buffer, track_processing_data, loggers, write_buffer);
			else 
				write_track<false, channels_split>(track_buffer, track_processing_data, loggers, write_buffer);
			track_counter += write_buffer.counter();
			write_buffer.dump(file_output);
			write_buffer.clear();
		}


	}
};

#endif SAF_SMRP2