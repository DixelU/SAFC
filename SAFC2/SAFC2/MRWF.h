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

#define NOMINMAX

#include "bbb_ffio.h"
#include "exprtk_wrapper.h"

namespace mrwf {
#define MULTICHANNEL_CASE(channel) case (channel<<8)|0x0:case (channel<<8)|0x1:case (channel<<8)|0x2:case (channel<<8)|0x3:case (channel<<8)|0x4:case (channel<<8)|0x5:case (channel<<8)|0x6:case (channel<<8)|0x7:case (channel<<8)|0x8:case (channel<<8)|0x9:case (channel<<8)|0xA:case (channel<<8)|0xB:case (channel<<8)|0xC:case (channel<<8)|0xD:case (channel<<8)|0xE:case (channel<<8)|0xF:

	struct modifiers_wrapper {
		exprtk_wrapper
			* tick_modifier,
			* notes_modifier,
			* noteaftertouch_modifier,
			* channelaftertocuh_modifier,
			* program_modifier,
			* pitch_modifier,
			* controller_modifier,
			* tempo_modifier;
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
		virtual ~midi_event() {	}
		inline virtual uint8_t& get_parameter(int param_no) { return junk_var; };
		inline virtual mrwf::type get_type() const { return mrwf::null; };
		inline virtual uint32_t get_event_size() const { return 0; };
		inline virtual void push_back_to_track(uint64_t previous_tick, std::vector<uint8_t>& output) const {};
		inline double& get_absolute_tick() { return absolute_tick; }
		inline const uint64_t& get_absolute_tick() const { return absolute_tick; }
		inline const uint8_t& is_enabled() const { return is_event_enabled; }
		inline uint8_t& is_enabled() { return is_event_enabled; }
	};

	struct noteon : midi_event {
		uint8_t channel, key, velocity;
		noteon(uint64_t absolute_tick, uint8_t channel, uint8_t key, uint8_t velocity) : midi_event(absolute_tick, 3), channel(channel), key(key), velocity(velocity) { }
		~noteon() {};
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
			if (!is_enabled())
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
		~noteoff() {}
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
			if (!is_enabled())
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
		~note_aftertouch() {}
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
			if (!is_enabled())
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
		~controller() {}
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
			if (!is_enabled())
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
		~program_change() {}
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
		~channel_aftertouch() {}
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
			if (!is_enabled())
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
		~pitch_change() {}
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
			if (!is_enabled())
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
		uint8_t* data;
		meta(uint64_t absolute_tick, uint8_t meta_type, uint32_t size /*size of meta data*/) : midi_event(absolute_tick, size), meta_type(meta_type), data(new uint8_t[size]) {	}
		~meta() {
			delete[] data;
		}
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
				if (event_size == 0x8 && data[0] == 0)
					return mrwf::type::color;
				if (event_size == 0xC && data[0] == 0)
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
			if (!is_enabled())
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
		uint8_t* data;
		sysex(uint64_t absolute_tick, uint8_t sysex_head, uint32_t size /*size of meta data*/) : midi_event(absolute_tick, size), sysex_head(sysex_head), data(new uint8_t[size]) { }
		~sysex() {
			delete[] data;
		}
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
			if (!is_enabled())
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
		tempo(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
		inline mrwf::type get_type() const override {
			return mrwf::type::meta;
		}
		inline uint32_t get_event_size() const override {
			return 6;
		}
		inline operator float() {
			uint32_t t = (data[0] << 16) | (data[1] << 8) | data[2];
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
		single_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
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
		gradient_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
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

	struct midi_track {
	protected:
		std::vector<uint8_t> meta_events_data;

#define insert_here(EV_TYPENAME, EV) { auto ptr = EV; events_list.push_back(ptr); EV_TYPENAME##_ptrs.push_back(ptr); }

		std::vector<midi_event*> events_list;

		std::vector<noteon*> noteon_ptrs;
		std::vector<noteoff*> noteoff_ptrs;
		std::vector<note_aftertouch*> note_aftertouch_ptrs;
		std::vector<controller*> controller_ptrs;
		std::vector<channel_aftertouch*> channel_aftertouch_ptrs;
		std::vector<program_change*> program_change_ptrs;
		std::vector<pitch_change*> pitch_change_ptrs;
		std::vector<meta*> meta_ptrs;

		std::array<uint32_t, 4096> polyphonies;

		bbb_ffr* file_input;
		/*sysex data is not allowed*/

		modifiers_wrapper modifiers;
		std::recursive_mutex locker;
	public:
		midi_track(modifiers_wrapper modifiers, bbb_ffr* file_input) : modifiers(modifiers), file_input(file_input) {

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
			bool active_track = true;

			/*header*/
			while (header != mrwf::expected_file_header && file_input->good() && !file_input->eof())
				header = (header << 8) | file_input->get();

			/*passing through tracksize*/
			for (int i = 0; i < 4; i++)
				file_input->get();

			if (file_input->eof())
				return;

			while (active_track && file_input->good() && !file_input->eof()) {
				uint8_t event_type = 0;
				auto [new_delta_time, delta_time_length] = get_vlv(*file_input);
				current_tick += new_delta_time;
				event_type = file_input->get();
				switch (event_type) {
					MULTICHANNEL_CASE(0x8) {
						running_status_byte = event_type;
						uint8_t key = file_input->get();
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel);
						if (polyphonies[index]) {
							polyphonies[index]--;
							insert_here(noteoff, new noteoff(current_tick, channel, key, velocity));
						}
					}
					break;
					MULTICHANNEL_CASE(0x9) {
						running_status_byte = event_type;
						uint8_t key = file_input->get();
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel);
						if (velocity) {
							polyphonies[index]++;
							insert_here(noteon, new noteon(current_tick, channel, key, velocity));
						}
						else if (polyphonies[index]) {
							polyphonies[index]--;
							insert_here(noteoff, new noteoff(current_tick, channel, key, velocity));
						}
					}
					break;
					MULTICHANNEL_CASE(0xA) {
						running_status_byte = event_type;
						uint8_t key = file_input->get();
						uint8_t value = file_input->get();
						uint8_t channel = event_type & 0xF;
						insert_here(note_aftertouch, new note_aftertouch(current_tick, channel, key, value));
					}
					break;
					MULTICHANNEL_CASE(0xB) {
						running_status_byte = event_type;
						uint8_t contr_no = file_input->get();
						uint8_t value = file_input->get();
						uint8_t channel = event_type & 0xF;
						insert_here(controller, new controller(current_tick, channel, contr_no, value));
					}
					break;
					MULTICHANNEL_CASE(0xC) {
						running_status_byte = event_type;
						uint8_t prog_no = file_input->get();
						uint8_t channel = event_type & 0xF;
						insert_here(program_change, new program_change(current_tick, channel, prog_no));
					}
					break;
					MULTICHANNEL_CASE(0xD) {
						running_status_byte = event_type;
						uint8_t value = file_input->get();
						uint8_t channel = event_type & 0xF;
						insert_here(channel_aftertouch, new channel_aftertouch(current_tick, channel, value));
					}
					break;
					MULTICHANNEL_CASE(0xE) {
						running_status_byte = event_type;
						uint8_t pitch_lsb = file_input->get();
						uint8_t pitch_msb = file_input->get();
						uint8_t channel = event_type & 0xF;
						insert_here(pitch_change, new pitch_change(current_tick, channel, pitch_lsb, pitch_msb));
					}
					break;
					case(0xFF): {
						running_status_byte = 0;
						uint8_t meta_type = file_input->get();
						auto [meta_length, meta_length_length] = get_vlv(*file_input);
						insert_here(meta, new meta(current_tick, meta_type, meta_length));
						for (size_t i = 0; i < meta_length; i++) 
							meta_ptrs.back()->data[i] = file_input->get();
						if (meta_ptrs.back()->deduct_type() == mrwf::type::endoftrack) {
							meta_ptrs.back()->is_enabled() = false;
							active_track = false;
						}
					}
					break;
					case(0xF7):case(0xF0): {
						running_status_byte = 0;
						auto [sysex_length, sysex_length_length] = get_vlv(*file_input);
						for (size_t i = 0; i < sysex_length; i++)
							file_input->get();
					}
					break;
					default: {
						switch (running_status_byte) {
							MULTICHANNEL_CASE(0x8) {
								uint8_t key = running_status_byte;
								uint8_t velocity = file_input->get();
								uint8_t channel = event_type & 0xF;
								uint16_t index = ((uint16_t)key << 4) | (channel);
								if (polyphonies[index]) {
									polyphonies[index]--;
									insert_here(noteoff, new noteoff(current_tick, channel, key, velocity));
								}
							}
							break;
							MULTICHANNEL_CASE(0x9) {
								uint8_t key = running_status_byte;
								uint8_t velocity = file_input->get();
								uint8_t channel = event_type & 0xF;
								uint16_t index = ((uint16_t)key << 4) | (channel);
								if (velocity) {
									polyphonies[index]++;
									insert_here(noteon, new noteon(current_tick, channel, key, velocity));
								}
								else if (polyphonies[index]) {
									polyphonies[index]--;
									insert_here(noteoff, new noteoff(current_tick, channel, key, velocity));
								}
							}
							break;
							MULTICHANNEL_CASE(0xA) {
								uint8_t key = running_status_byte;
								uint8_t value = file_input->get();
								uint8_t channel = event_type & 0xF;
								insert_here(note_aftertouch, new note_aftertouch(current_tick, channel, key, value));
							}
							break;
							MULTICHANNEL_CASE(0xB) {
								uint8_t contr_no = running_status_byte;
								uint8_t value = file_input->get();
								uint8_t channel = event_type & 0xF;
								insert_here(controller, new controller(current_tick, channel, contr_no, value));
							}
							break;
							MULTICHANNEL_CASE(0xC) {
								uint8_t prog_no = running_status_byte;
								uint8_t channel = event_type & 0xF;
								insert_here(program_change, new program_change(current_tick, channel, prog_no));
							}
							break;
							MULTICHANNEL_CASE(0xD) {
								uint8_t value = running_status_byte;
								uint8_t channel = event_type & 0xF;
								insert_here(channel_aftertouch, new channel_aftertouch(current_tick, channel, value));
							}
							break;
							MULTICHANNEL_CASE(0xE) {
								uint8_t pitch_lsb = running_status_byte;
								uint8_t pitch_msb = file_input->get();
								uint8_t channel = event_type & 0xF;
								insert_here(pitch_change, new pitch_change(current_tick, channel, pitch_lsb, pitch_msb));
							}
							break;
						}
					}
					break;
				}

			}
		}
	};


}


#endif
