#pragma once
#ifndef SAF_MRWF
#define SAF_MRWF 

#include <algorithm>
#include <iostream>
#include <vector>
#include <tuple>
#include <list>

namespace mrwf {
	enum type {
		null = 0,
		sysex = 7, noteoff = 8, noteon = 9, note_aftertouch = 10, controller = 11, program = 12, channel_aftertouch = 13, pitch = 14, meta = 15, 
		deltatime = 16,
		color = 20+0xA, endoftrack = 20+0x2F, tempo = 20+0x51
	};

	struct midi_event {
	protected:
		uint64_t absolute_tick;
		const uint32_t event_size;
		uint8_t junk_var;
	public:
		midi_event(uint64_t absolute_tick, uint32_t event_size) : event_size(event_size), absolute_tick(absolute_tick) {}
		inline virtual uint8_t& get_parameter(int param_no);
		inline consteval virtual mrwf::type get_type() const;
		inline consteval virtual uint32_t get_event_size() const;
		inline uint64_t& get_absolute_tick() {
			return absolute_tick;
		}
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::noteon;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 3;
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::noteoff;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 3;
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::note_aftertouch;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 3;
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::controller;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 3;
		}
	};

	struct program : midi_event {
		uint8_t channel, program_no;
		program(uint64_t absolute_tick, uint8_t channel, uint8_t program_no) : midi_event(absolute_tick, 2), channel(channel), program_no(program_no) { }
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::program;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 2;
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::channel_aftertouch;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 2;
		}
	};

	struct pitch : midi_event {
		uint8_t channel, lsb, msb;
		pitch(uint64_t absolute_tick, uint8_t channel, uint8_t lsb, uint8_t value) : midi_event(absolute_tick, 3), channel(channel), lsb(lsb), msb(msb) { }
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
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::pitch;
		}
		inline consteval virtual uint32_t get_event_size() const override {
			return 3;
		}
	};

	struct meta : midi_event {
		uint8_t meta_type;
		uint8_t* data;
		meta(uint64_t absolute_tick, uint8_t meta_type, uint32_t size /*size of meta data*/, uint8_t* data) : midi_event(absolute_tick, size), meta_type(meta_type), data(data) { }
		inline uint8_t& get_parameter(int param_no) override {
			switch (param_no) {
			case 0:
				return meta_type;
			case 1:
				return *data;
			}
			junk_var = 0;
			return junk_var;
		}
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::meta;
		}
		inline consteval uint32_t get_event_size() const override {
			return event_size;
		}
	};

	struct tempo : meta {
		tempo(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, uint8_t* data) : meta(absolute_tick, meta_type, size, data) {}
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::meta;
		}
		inline consteval uint32_t get_event_size() const override {
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
		single_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, uint8_t* data) : meta(absolute_tick, meta_type, size, data) {}
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::color;
		}
		inline consteval uint32_t get_event_size() const override {
			return 0x8;
		}
		uint8_t& r() { return data[4]; }
		uint8_t& g() { return data[5]; }
		uint8_t& b() { return data[6]; }
		uint8_t& a() { return data[7]; }
	};

	struct gradient_color_event : meta {
		gradient_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size, uint8_t* data) : meta(absolute_tick, meta_type, size, data) {}
		inline consteval mrwf::type get_type() const override {
			return mrwf::type::color;
		}
		inline consteval uint32_t get_event_size() const override {
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

	template<typename bbb_get_obj>
	std::tuple<uint32_t, uint8_t> get_vlv(bbb_get_obj& inp) {
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

	struct midi_track {
	protected:
		std::vector<uint8_t> meta_events_data;
	public:
	};
}


#endif
