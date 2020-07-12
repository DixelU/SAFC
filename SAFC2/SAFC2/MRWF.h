#pragma once
#ifndef SAF_MRWF
#define SAF_MRWF 

#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <cassert>
#include <vector>
#include <mutex>
#include <deque>
#include <array>
#include <tuple>
#include <list>

#include "bbb_ffio.h"

namespace mrwf {
#define MULTICHANNEL_CASE(channel) case (channel<<8)|0x0:case (channel<<8)|0x1:case (channel<<8)|0x2:case (channel<<8)|0x3:case (channel<<8)|0x4:case (channel<<8)|0x5:case (channel<<8)|0x6:case (channel<<8)|0x7:case (channel<<8)|0x8:case (channel<<8)|0x9:case (channel<<8)|0xA:case (channel<<8)|0xB:case (channel<<8)|0xC:case (channel<<8)|0xD:case (channel<<8)|0xE:case (channel<<8)|0xF:

	template<typename data_type>
	struct value_modifier {
		enum class type { replace, add, mult };
		enum class check_type{ always, equals, totally_equals, lesser, bigger, never };
		double modifier;
		type vm_type;
		check_type vm_ctype;
		data_type comparing_value;
		bool continueable;
		value_modifier(double modifier = 0., type vm_type = type::replace, check_type vm_ctype = check_type::always, data_type value = data_type(), bool continueable=true) :
			modifier(modifier), vm_type(vm_type), vm_ctype(vm_ctype), value(value), continueable(continueable) {}
		inline bool operator()(data_type& inp_value) const {
			switch (vm_ctype) {
			case check_type::always:
				break;
			case check_type::equals:
				if (!(comparing_value < inp_value && inp_value < comparing_value))
					return false;
				break;
			case check_type::totally_equals:
				if (!(comparing_value == inp_value))
					return false;
				break;
			case check_type::lesser:
				if (!(inp_value < comparing_value))
					return false;
				break;
			case check_type::bigger:
				if (!(comparing_value < inp_value))
					return false;
				break;
			case check_type::never:
				return false;
			default:
				return false;
				break;
			}
			switch (vm_type) {
			case type::add:
				inp_value += modifier;
				break;
			case type::multiply:
				inp_value *= modifier;
				break;
			case type::replace:
				inp_value = modifier;
				break;
			default:
				break;
			}
			return true;
		}
		inline std::pair<bool, data_type> operator()(data_type inp_value, void* null) const {
			switch (vm_ctype) {
			case check_type::always:
				break;
			case check_type::equals:
				if (!(comparing_value < inp_value && inp_value < comparing_value))
					return { false , inp_value };
				break;
			case check_type::totally_equals:
				if (!(comparing_value == inp_value))
					return { false , inp_value };
				break;
			case check_type::lesser:
				if (!(inp_value < comparing_value))
					return { false , inp_value };
				break;
			case check_type::bigger:
				if (!(comparing_value < inp_value))
					return { false , inp_value };
				break;
			default:
				return { false , inp_value };
				break;
			}
			switch (vm_type) {
			case type::add:
				inp_value += modifier;
				break;
			case type::multiply:
				inp_value *= modifier;
				break;
			case type::replace:
				inp_value = modifier;
				break;
			default:
				break;
			}
			return { true , inp_value };
		}
	};

	template<typename data_type>
	struct check_node {
		enum class check_type { always, equals, totally_equals, lesser, bigger, never };
		check_type ctype;
		data_type root_value;
		check_node(data_type root_value = data_type(), check_type ctype = check_type::always) : root_value(root_value), ctype(ctype) {}
		inline bool operator()(const data_type& inp_value) const {
			switch (ctype) {
			case check_type::always:
				break;
			case check_type::equals:
				if (!(root_value < inp_value && inp_value < root_value))
					return false;
				break;
			case check_type::totally_equals:
				if (!(root_value == inp_value))
					return false;
				break;
			case check_type::lesser:
				if (!(inp_value < root_value))
					return false;
				break;
			case check_type::bigger:
				if (!(root_value < inp_value))
					return false;
				break;
			default:
				return false;
				break;
			}
			return true;
		}
	};

	template<typename data_type>
	struct check_expr {
		std::vector<std::vector<check_node>> expr; // DNF // 
		check_expr(const std::vector<std::vector<check_node>>& expr) : expr(expr) {}
		inline bool operator()(const data_type& inp_value) const {
			bool flag = false;
			for (const auto& v : expr) {
				bool co_check = true;
				for (const auto& el : v) 
					co_check &= v(inp_type);
				flag |= co_check;
			}
		}
	};

	template<typename data_type, size_t size>
	struct multivalued_intersect_modifier {
		enum class type { replace, add, mult };
		type mtype;
		data_type data;
		check_expr check;
		size_t check_ptr;
		size_t value_ptr;
		bool continueable;
		multivalued_intersect_modifier(const check_expr& check, const data_type& data, size_t check_ptr, size_t value_ptr, bool continueable = false) :
			data(data), check(check), check_ptr(check_ptr), value_ptr(value_ptr) { }
		inline bool operator()(const std::array<data_type&, size>& arr) const {
			if (check(arr[check_ptr])) {
				switch (m_type) {
				case type::add:
					arr[value_ptr] += modifier;
					break;
				case type::multiply:
					arr[value_ptr] *= modifier;
					break;
				case type::replace:
					arr[value_ptr] = modifier;
					break;
				default:
					break;
				}
				return true;
			}
			else 
				return continueable;
		}
	};

	namespace mrwf {
		enum type {
			null = 0,
			sysex = 7, noteoff = 8, noteon = 9, note_aftertouch = 10, controller = 11, program_change = 12, channel_aftertouch = 13, pitch_change = 14, meta = 15,
			color = 20 + 0xA, endoftrack = 20 + 0x2F, tempo = 20 + 0x51, grad_color = 0xA + 128 + 20
		};

		constexpr uint32_t expected_track_header = 1297379947;
		constexpr uint32_t expected_file_header = 1297377380;
	}

	//first output is vlv itself, the second is size of vlv
	template<typename bbb_get_obj>
	[[nodiscard("Reading VLV will shift file pointer, value might not be reachable anymore.")]] 
	inline std::pair<uint32_t, uint8_t> get_vlv(bbb_get_obj& inp) {
		uint32_t value = 0;
		uint8_t size = 0;
		uint8_t IO = 0;
		do {
			IO = inp.get();
			value = (value << 7) | (IO & 0x7F);
			size++;
		} while (IO & 0x80);
		return { value, size };
	}

	//returns the size of vlv
	inline uint8_t push_vlv(uint32_t value, std::vector<uint8_t>& vec) {
		uint8_t stack[5];
		uint8_t size = 0;
		uint8_t r_size = 0;
		do {
			stack[size] = (value & 0x7F);
			value >>= 7;
			if (size)
				stack[size] |= 0x80;
			size++;
		} while (value);
		r_size = size;
		while (size) 
			vec.push_back(stack[--size]);
		return r_size;
	}

#define EXTENDED_WARNINGS_AND_CHECKS

	struct midi_event {
	protected:
		double absolute_tick;
		const uint32_t event_size;
		uint8_t junk_var;
		uint8_t is_event_enabled;
	public:
		midi_event(uint64_t absolute_tick, uint32_t event_size) : event_size(event_size), absolute_tick(absolute_tick), is_event_enabled(true) {}
		inline virtual uint8_t& get_parameter(int param_no);
		inline virtual mrwf::type get_type() const;
		inline virtual uint32_t get_event_size() const;
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const;
		inline double& get_absolute_tick() { return absolute_tick; }
		inline const uint64_t& get_absolute_tick() const { return absolute_tick; }
		inline uint8_t& is_enabled() { return is_event_enabled; }
	};

	struct noteon : midi_event {
		uint8_t channel, key, velocity;
		noteon(uint64_t absolute_tick, uint8_t channel, uint8_t key, uint8_t velocity) : midi_event(absolute_tick, 3), channel(channel), key(key), velocity(velocity) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return key;
			case 2:
				return velocity;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::noteon;
		}
		inline virtual uint32_t get_event_size() const override {
			return 3;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::noteon) << 4));
			output.push_back(key);
			output.push_back(velocity);
		}
	};

	struct noteoff : midi_event {
		uint8_t channel, key, velocity;
		noteoff(uint64_t absolute_tick, uint8_t channel, uint8_t key, uint8_t velocity) : midi_event(absolute_tick, 3), channel(channel), key(key), velocity(velocity) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return key;
			case 2:
				return velocity;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::noteoff;
		}
		inline virtual uint32_t get_event_size() const override {
			return 3;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::noteoff)<<4));
			output.push_back(key);
			output.push_back(velocity);
		}
	};

	struct note_aftertouch : midi_event {
		uint8_t channel, key, value;
		note_aftertouch(uint64_t absolute_tick, uint8_t channel, uint8_t key, uint8_t value) : midi_event(absolute_tick, 3), channel(channel), key(key), value(value) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return key;
			case 2:
				return value;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::note_aftertouch;
		}
		inline virtual uint32_t get_event_size() const override {
			return 3;
		}

		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::note_aftertouch) << 4));
			output.push_back(key);
			output.push_back(value);
		}
	};

	struct controller : midi_event {
		uint8_t channel, controller_no, value;
		controller(uint64_t absolute_tick, uint8_t channel, uint8_t controller_no, uint8_t value) : midi_event(absolute_tick, 3), channel(channel), controller_no(controller_no), value(value) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return controller_no;
			case 2:
				return value;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::controller;
		}
		inline virtual uint32_t get_event_size() const override {
			return 3;
		}

		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::controller) << 4));
			output.push_back(controller_no);
			output.push_back(value);
		}
	};

	struct program_change : midi_event {
		uint8_t channel, program_no;
		program_change(uint64_t absolute_tick, uint8_t channel, uint8_t program_no) : midi_event(absolute_tick, 2), channel(channel), program_no(program_no) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return program_no;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::program_change;
		}
		inline virtual uint32_t get_event_size() const override {
			return 2;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::program_change) << 4));
			output.push_back(program_no);
		}
	};

	struct channel_aftertouch : midi_event {
		uint8_t channel, value;
		channel_aftertouch(uint64_t absolute_tick, uint8_t channel, uint8_t value) : midi_event(absolute_tick, 2), channel(channel), value(value) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return value;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::channel_aftertouch;
		}
		inline virtual uint32_t get_event_size() const override {
			return 2;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::channel_aftertouch) << 4));
			output.push_back(value);
		}
	};

	struct pitch_change : midi_event {
		uint8_t channel, lsb, msb;
		pitch_change(uint64_t absolute_tick, uint8_t channel, uint8_t lsb, uint8_t msb) : midi_event(absolute_tick, 3), channel(channel), lsb(lsb), msb(msb) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return channel;
			case 1:
				return lsb;
			case 2:
				return msb;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::pitch_change;
		}
		inline virtual uint32_t get_event_size() const override {
			return 3;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back((channel & 0xF) | (((uint8_t)mrwf::type::pitch_change) << 4));
			output.push_back(lsb);
			output.push_back(msb);
		}
		inline operator uint16_t() const {
			return ((msb&0x7F)<<7)|(lsb&0x7F);
		}
		inline uint16_t operator=(uint16_t value) {
			lsb = value & 0x7F;
			msb = (value >> 7) & 0x7F;
			return value;
		}
	};

	struct meta : midi_event {
		uint8_t meta_type;
		std::deque<uint8_t>::iterator data;
		meta(uint64_t absolute_tick, uint8_t meta_type, uint32_t size /*size of meta data*/, std::deque<uint8_t>::iterator data) : midi_event(absolute_tick, size), meta_type(meta_type), data(data) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return meta_type;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type deduct_type() const {
			switch (meta_type) {
			case 0x2F:
				return mrwf::type::endoftrack;/*end of tracks are not allowed in internal representation*/
			case 0x51:
				return mrwf::type::tempo;
			case 0x0A:
				if (event_size == 0x8 && *data == 0)
					return mrwf::type::color;
				if (event_size == 0xC && *data == 0)
					return mrwf::type::grad_color;
			default:
				return mrwf::type::meta;
			}
		}
		inline mrwf::type get_type() const override {
			return deduct_type();
		}
		inline uint32_t get_event_size() const override {
			return event_size;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back(0xFF);
			output.push_back(meta_type);
			push_vlv(event_size, output);
			output.insert(output.end(), data, data + event_size);
		}
	};

	struct sysex : midi_event {
		uint8_t sysex_head;
		std::deque<uint8_t>::iterator data;
		sysex(uint64_t absolute_tick, uint8_t sysex_head, uint32_t size /*size of meta data*/, std::deque<uint8_t>::iterator data) : midi_event(absolute_tick, size), sysex_head(sysex_head), data(data) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return sysex_head;
			}
			junk_var = 0;
			return junk_var;
		}
		inline mrwf::type get_type() const override {
			return mrwf::type::sysex;
		}
		inline uint32_t get_event_size() const override {
			return event_size;
		}
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const override {
			if (!is_enabled)
				return;
			uint64_t difference = absolute_tick - previous_tick;
			assert(difference <= 0xFFFFFFFF);
			push_vlv(difference, output);

			output.push_back(sysex_head);
			push_vlv(event_size, output);
			for (uint64_t i = 0; i < event_size; i++)
				output.push_back(data[i]);
		}
	};

	struct tempo : meta {
		tempo(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, std::deque<uint8_t>::iterator data) : meta(absolute_tick, meta_type, size, data) {}
		inline mrwf::type get_type() const override {
			return mrwf::type::meta;
		}
		inline uint32_t get_event_size() const override {
			return 6;
		}
		inline operator float() {
			uint32_t t = (data[0] << 16) | (data[1] << 8) | data[0];
			return (60000000.f / t);
		}
		inline const float& operator=(const float& tempo) {
			uint32_t t = (60000000 / tempo);
			data[0] = (t >> 16) & 0xFF;
			data[1] = (t >> 8) & 0xFF;
			data[2] = (t) & 0xFF;
			return tempo;
		}
	};

	struct single_color_event : meta {
		single_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, std::deque<uint8_t>::iterator data) : meta(absolute_tick, meta_type, size, data) {}
		inline mrwf::type get_type() const override {
			return mrwf::type::color;
		}
		inline uint32_t get_event_size() const override {
			return 0x8;
		}
		uint8_t& r() { return data[4]; }
		uint8_t& g() { return data[5]; }
		uint8_t& b() { return data[6]; }
		uint8_t& a() { return data[7]; }
	};

	struct gradient_color_event : meta {
		gradient_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, std::deque<uint8_t>::iterator data) : meta(absolute_tick, meta_type, size, data) {}
		inline mrwf::type get_type() const override {
			return mrwf::type::color;
		}
		inline uint32_t get_event_size() const override {
			return 0xC;
		}
		uint8_t& r1() { return data[4]; }
		uint8_t& g1() { return data[5]; }
		uint8_t& b1() { return data[6]; }
		uint8_t& a1() { return data[7]; }
		uint8_t& r2() { return data[8]; }
		uint8_t& g2() { return data[9]; }
		uint8_t& b2() { return data[10]; }
		uint8_t& a2() { return data[11]; }
	};

	struct event_modifiers {
		std::vector<value_modifier<double>> tick_modifiers;
		std::vector<value_modifier<uint8_t>> note_key_modifiers;
		std::vector<value_modifier<uint8_t>> note_on_velocity_modifiers;
		std::vector<value_modifier<uint8_t>> note_off_velocity_modifiers;
		std::vector<value_modifier<uint8_t>> program_modifiers;
		std::vector<value_modifier<uint8_t>> channel_aftertouch_modifiers;
		std::vector<value_modifier<uint8_t>> note_aftertouch_key_modifiers;
		std::vector<value_modifier<uint8_t>> note_aftertouch_value_modifiers;
		std::vector<value_modifier<uint8_t>> controller_no_modifiers;
		std::vector<value_modifier<uint8_t>> controller_value_modifiers;
		std::vector<value_modifier<uint16_t>> pitch_modifiers;
		std::vector<value_modifier<double>> tempo_modifiers;
		event_modifiers() {}
		event_modifiers(
			const std::vector<value_modifier<double>> &tick_modifiers,
			const std::vector<value_modifier<uint8_t>> &note_key_modifiers,
			const std::vector<value_modifier<uint8_t>> &note_on_velocity_modifiers,
			const std::vector<value_modifier<uint8_t>> &note_off_velocity_modifiers,
			const std::vector<value_modifier<uint8_t>> &program_modifiers,
			const std::vector<value_modifier<uint8_t>> &channel_aftertouch_modifiers,
			const std::vector<value_modifier<uint8_t>> &note_aftertouch_key_modifiers,
			const std::vector<value_modifier<uint8_t>> &note_aftertouch_value_modifiers,
			const std::vector<value_modifier<uint8_t>> &controller_no_modifiers,
			const std::vector<value_modifier<uint8_t>> &controller_value_modifiers,
			const std::vector<value_modifier<uint16_t>> &pitch_modifiers,
			const std::vector<value_modifier<double>>& tempo_modifiers
		) : tick_modifiers(tick_modifiers), 
			note_key_modifiers(note_key_modifiers), 
			note_on_velocity_modifiers(note_on_velocity_modifiers),
			note_off_velocity_modifiers(note_off_velocity_modifiers),
			program_modifiers(program_modifiers),
			channel_aftertouch_modifiers(channel_aftertouch_modifiers),
			note_aftertouch_key_modifiers(note_aftertouch_key_modifiers),
			note_aftertouch_value_modifiers(note_aftertouch_value_modifiers),
			controller_no_modifiers(controller_no_modifiers),
			controller_value_modifiers(controller_value_modifiers),
			pitch_modifiers(pitch_modifiers), 
			tempo_modifiers(tempo_modifiers)
		{ }

		[[nodiscard("You will not know if value is validated.")]]
		inline bool apply(midi_event* ev) const {
			if (!ev->is_enabled())
				return false;
			for (const auto& mod : tick_modifiers) {
				bool ans = mod(ev->get_absolute_tick());
				if (!ans && !mod.continueable)
					return false;
			}
			return true;
		}
		inline void apply(noteon& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : note_on_velocity_modifiers) {
				bool ans = mod(ev.velocity);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
			if (!ev.velocity)
				ev.velocity = 1;
			for (const auto& mod : note_key_modifiers) {
				bool ans = mod(ev.key);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(noteoff& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : note_off_velocity_modifiers) {
				bool ans = mod(ev.velocity);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
			for (const auto& mod : note_key_modifiers) {
				bool ans = mod(ev.key);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(program_change& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : program_modifiers) {
				bool ans = mod(ev.program_no);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(channel_aftertouch& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : channel_aftertouch_modifiers) {
				bool ans = mod(ev.value);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(note_aftertouch& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : note_aftertouch_key_modifiers) {
				bool ans = mod(ev.key);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
			for (const auto& mod : note_aftertouch_value_modifiers) {
				if (!ev.is_enabled())
					return;
				bool ans = mod(ev.value);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(controller& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : controller_no_modifiers) {
				bool ans = mod(ev.controller_no);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
			for (const auto& mod : note_aftertouch_value_modifiers) {
				bool ans = mod(ev.value);
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(pitch_change& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : pitch_modifiers) {
				auto [ans, value] = mod(ev, nullptr);
				ev = value;
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void apply(tempo& ev) const {
			if (!ev.is_enabled())
				return;
			for (const auto& mod : tempo_modifiers) {
				auto [ans, value] = mod(ev, nullptr);
				ev = value;
				if (!ans && !mod.continueable) {
					ev.is_enabled() = false;
					return;
				}
			}
		}
		inline void merge(event_modifiers& ev_mods) {
			tick_modifiers.insert(tick_modifiers.begin(), ev_mods.tick_modifiers.begin(), ev_mods.tick_modifiers.end());
			tempo_modifiers.insert(tempo_modifiers.begin(), ev_mods.tempo_modifiers.begin(), ev_mods.tempo_modifiers.end());
			pitch_modifiers.insert(pitch_modifiers.begin(), ev_mods.pitch_modifiers.begin(), ev_mods.pitch_modifiers.end());
			note_aftertouch_value_modifiers.insert(note_aftertouch_value_modifiers.begin(), ev_mods.note_aftertouch_value_modifiers.begin(), ev_mods.note_aftertouch_value_modifiers.end());
			controller_no_modifiers.insert(controller_no_modifiers.begin(), ev_mods.controller_no_modifiers.begin(), ev_mods.controller_no_modifiers.end());
			note_aftertouch_value_modifiers.insert(note_aftertouch_value_modifiers.begin(), ev_mods.note_aftertouch_value_modifiers.begin(), ev_mods.note_aftertouch_value_modifiers.end());
			note_aftertouch_key_modifiers.insert(note_aftertouch_key_modifiers.begin(), ev_mods.note_aftertouch_key_modifiers.begin(), ev_mods.note_aftertouch_key_modifiers.end());
			channel_aftertouch_modifiers.insert(channel_aftertouch_modifiers.begin(), ev_mods.channel_aftertouch_modifiers.begin(), ev_mods.channel_aftertouch_modifiers.end());
			program_modifiers.insert(program_modifiers.begin(), ev_mods.program_modifiers.begin(), ev_mods.program_modifiers.end());
			note_key_modifiers.insert(note_key_modifiers.begin(), ev_mods.note_key_modifiers.begin(), ev_mods.note_key_modifiers.end());
			note_on_velocity_modifiers.insert(note_on_velocity_modifiers.begin(), ev_mods.note_on_velocity_modifiers.begin(), ev_mods.note_on_velocity_modifiers.end());
		}
		inline bool empty() const {
			return tick_modifiers.empty() &&
				note_key_modifiers.empty() &&
				note_on_velocity_modifiers.empty() &&
				note_off_velocity_modifiers.empty() &&
				program_modifiers.empty() &&
				channel_aftertouch_modifiers.empty() &&
				note_aftertouch_key_modifiers.empty() &&
				note_aftertouch_value_modifiers.empty() &&
				controller_no_modifiers.empty() &&
				controller_value_modifiers.empty() &&
				pitch_modifiers.empty() &&
				tempo_modifiers.empty();
		}
	};

	struct midi_track {
	protected:
		std::deque<uint8_t> meta_events_data;

		std::vector<midi_event*> events_list;

		std::vector<noteon*> noteon_ptrs;
		std::vector<noteoff*> noteoff_ptrs;
		std::vector<note_aftertouch*> note_aftertouch_ptrs;
		std::vector<controller*> controller_ptrs;
		std::vector<channel_aftertouch*> controller_ptrs;
		std::vector<program_change*> controller_ptrs;
		std::vector<pitch_change*> controller_ptrs;
		std::vector<meta*> controller_ptrs;

		std::array<uint32_t, 4096> polyphonies;

		bbb_ffr* file_input;
		/*sysex data is not allowed*/

		event_modifiers* modifiers;
		std::recursive_mutex locker;

		void __modify() {
			locker.lock();
			if (modifiers->empty())
				return;
			for (const auto& ev : events_list)
				ev->is_enabled() = modifiers->apply((midi_event*)ev);
			for (const auto& ev : noteon_ptrs)
				ev->is_enabled() = modifiers->apply((noteon*)ev);
			for (const auto& ev : noteoff_ptrs)
				ev->is_enabled() = modifiers->apply((noteoff*)ev);
			for (const auto& ev : note_aftertouch_ptrs)
				ev->is_enabled() = modifiers->apply((note_aftertouch*)ev);
			for (const auto& ev : note_aftertouch_ptrs)
				ev->is_enabled() = modifiers->apply((controller*)ev);
			for (const auto& ev : note_aftertouch_ptrs)
				ev->is_enabled() = modifiers->apply((channel_aftertouch*)ev);
			for (const auto& ev : note_aftertouch_ptrs)
				ev->is_enabled() = modifiers->apply((program_change*)ev);
			for (const auto& ev : note_aftertouch_ptrs)
				ev->is_enabled() = modifiers->apply((pitch_change*)ev);
			
			
			locker.unlock();
		}
	public:
		midi_track(event_modifiers* modifiers, bbb_ffr* file_input) : modifiers(modifiers), file_input(file_input) {

		}
		void clear() {
			locker.lock();
			meta_events_data.clear();
			for (auto& ev : events_list)
				delete ev;
			for (auto& channeled_key_polyphony : polyphonies)
				channeled_key_polyphony = 0;
			locker.unlock();
		}
		void swap(midi_track& mt) {
			locker.lock();
			mt.locker.lock();
			meta_events_data.swap(mt.meta_events_data);
			events_list.swap(mt.events_list);
			std::swap(modifiers, mt.modifiers);
			std::swap(file_input, mt.file_input);
			mt.locker.unlock();
			locker.unlock();
		}
		void read_one_track() {
			uint64_t current_tick = 0;
			uint32_t header = 0;
			uint8_t running_status_byte = 0;

			/*header*/
			while (header != mrwf::expected_file_header && file_input->good() && !file_input->eof())
				header = (header << 8) | file_input->get();

			/*passing through tracksize*/
			for (int i = 0; i < 4; i++)
				file_input->get();

			if (file_input->eof())
				return;

			while (file_input->good() && !file_input->eof()) {
				uint8_t event_type = 0;
				auto [new_delta_time, delta_time_length] = get_vlv(*file_input);
				current_tick += new_delta_time;
				event_type = file_input->get();
				switch (event_type) {
					MULTICHANNEL_CASE(0x8)
						uint8_t key = file_input->get();
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel); 
						if (polyphonies[index]) {
							polyphonies[index]--;
							events_list.push_back(new noteoff(current_tick, event_type & 0xF, key, velocity));
						}
					break;
					MULTICHANNEL_CASE(0x9)
						running_status_byte = event_type;
						uint8_t key = file_input->get();
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel);
						if (velocity) {
							polyphonies[index]++;
							events_list.push_back(new noteon(current_tick, event_type & 0xF, key, velocity));
						}
						else if (polyphonies[index]) {
							polyphonies[index]--;
							events_list.push_back(new noteoff(current_tick, event_type & 0xF, key, velocity));
						}
					break;
				}

			}


		}
	};
}


#endif
