#ifndef SAF_MRWF_H
#define SAF_MRWF_H

#include <sstream>
#include <iostream>
#include <memory>
#include <fstream>
#include <vector>
#include <string>
#include <stack>
#include <array>

#include <coroutine>

#include "bbb_ffio.h"

#define SAF_MRWF_MULTICHANNEL_CASE(channel) case (channel<<8)|0x0: case (channel<<8)|0x1: case (channel<<8)|0x2: case (channel<<8)|0x3: case (channel<<8)|0x4: case (channel<<8)|0x5: case (channel<<8)|0x6: case (channel<<8)|0x7: case (channel<<8)|0x8: case (channel<<8)|0x9: case (channel<<8)|0xA: case (channel<<8)|0xB: case (channel<<8)|0xC: case (channel<<8)|0xD: case (channel<<8)|0xE: case (channel<<8)|0xF:

namespace details
{
	enum type
	{
		null = 0,
		sysex = 7, noteoff = 8, noteon = 9, note_aftertouch = 10, controller = 11, program_change = 12, channel_aftertouch = 13, pitch_change = 14, meta = 15,
		color = 20 + 0xA, endoftrack = 20 + 0x2F, tempo = 20 + 0x51, grad_color = 0xA + 128 + 20
	};

	constexpr uint32_t expected_track_header = 1297379947;
	constexpr uint32_t expected_file_header = 1297377380;

	template<typename bbb_get_obj>
	[[nodiscard("Reading VLV will shift file pointer, value might not be reachable anymore.")]]
	inline std::pair<uint32_t, uint8_t> get_vlv(bbb_get_obj& inp)
	{
		uint32_t value = 0;
		uint8_t size = 0;
		uint8_t IO = 0;
		do
		{
			IO = inp.get();
			value = (value << 7) | (IO & 0x7F);
			size++;
		} while (IO & 0x80);
		return { value, size };
	}

	//returns the size of vlv
	inline uint8_t push_vlv(uint32_t value, std::vector<uint8_t>& vec)
	{
		uint8_t stack[5];
		uint8_t size = 0;
		uint8_t r_size = 0;
		do
		{
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
}

struct midi_event
{
	using type = details::type;
	std::int64_t tick;
	bool enabled;
	midi_event(std::int64_t tick): tick(tick) {	}
	virtual ~midi_event() {};
	virtual bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const;
	virtual type event_type() const { return type::null; };
	virtual std::int64_t event_size() const { return 0; }
};

struct noteon : midi_event {
	std::uint8_t key : 8;
	std::uint8_t volume : 8;
	std::uint8_t channel : 4;
	noteon(std::int64_t tick, std::uint8_t channel, std::uint8_t key, std::uint8_t volume): 
		midi_event(tick), key(key), volume(volume), channel(channel&0xF) 
	{

	}

	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override 
	{
		if (!enabled)
			return false;
		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = key;
		out[first_param_position + 2] = volume;
		return true;
	}
	std::int64_t event_size() const override { return 3; }
	type event_type() const override { return type::noteon; }
};

struct noteoff : midi_event {
	std::uint8_t key : 8;
	std::uint8_t volume : 8; //aftertouch?
	std::uint8_t channel : 4;
	noteoff(std::int64_t tick, std::uint8_t channel, std::uint8_t key, std::uint8_t volume) :
		midi_event(tick), key(key), volume(volume), channel(channel & 0xF) 
	{

	}
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override
	{
		if (!enabled)
			return false;
		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = key;
		out[first_param_position + 2] = volume;
		return true;
	}
	std::int64_t event_size() const override { return 3; }
	type event_type() const override { return type::noteoff; }
};

struct full_note_view 
{
	struct note_events_ordering
	{
		bool ordered_notes;
		bool ordered_events;
	};
	note_events_ordering* ordering;
	noteon* begin;
	noteoff* end;
	full_note_view(note_events_ordering* ordering, noteon* begin, noteoff* end) :begin(begin), end(end) { }
	std::int64_t size() const
	{
		return end->tick - begin->tick;
	}
	void resize(std::int64_t new_size)
	{
		end->tick += (new_size - size());
		ordering->ordered_events = false;
	}
	void shift(std::int64_t delta_ticks)
	{
		begin->tick += delta_ticks;
		end->tick += delta_ticks;
		if (begin->tick < 0)
			begin->enabled = end->enabled = false;

		ordering->ordered_events = false;
		ordering->ordered_notes = false;
	}
	std::uint8_t get_key() const
	{
		return begin->key;
	}
	std::uint8_t get_channel() const
	{
		return begin->channel;
	}
	std::uint8_t get_volume() const
	{
		return begin->volume;
	}
	void set_key(std::uint8_t key) 
	{
		begin->key = key;
		end->key = key;
	}
	void set_channel(std::uint8_t channel)
	{
		channel &= 0xF;
		begin->channel = channel;
		end->channel = channel;
	}
	void set_volume(std::uint8_t volume)
	{
		begin->volume = volume;
		end->volume = volume;
	}
};


struct note_aftertouch : midi_event
{
	std::uint8_t key, value;
	std::uint8_t channel : 4;
	note_aftertouch(uint64_t tick, uint8_t channel, uint8_t key, uint8_t value) : midi_event(tick), channel(channel), key(key), value(value) { }
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override
	{
		if (!enabled)
			return false;
		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = key;
		out[first_param_position + 2] = value;
		return true;
	}
	std::int64_t event_size() const override { return 3; }
	type event_type() const override { return type::note_aftertouch; }
};

struct controller : midi_event 
{
	uint8_t channel, controller_no, value;
	controller(uint64_t absolute_tick, uint8_t channel, uint8_t controller_no, uint8_t value) : midi_event(absolute_tick), channel(channel), controller_no(controller_no), value(value) { }
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override
	{
		if (!enabled)
			return false;

		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = controller_no;
		out[first_param_position + 2] = value;
		return true;
	}
	std::int64_t event_size() const override { return 3; }
	type event_type() const override { return type::controller; }
};

struct program_change : midi_event
{
	std::uint8_t program_no;
	std::uint8_t channel : 4;
	program_change(uint64_t absolute_tick, uint8_t channel, uint8_t program_no) : midi_event(absolute_tick), channel(channel), program_no(program_no) { }
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override
	{
		if (!enabled)
			return false;

		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = program_no;
		return true;
	}
	std::int64_t event_size() const override { return 2; }
	type event_type() const override { return type::program_change; }
};

struct channel_aftertouch : midi_event
{
	std::uint8_t value;
	std::uint8_t channel : 4;
	channel_aftertouch(uint64_t absolute_tick, uint8_t channel, uint8_t value) : midi_event(absolute_tick), channel(channel), value(value) { }
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override
	{
		if (!enabled)
			return false;

		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = value;
		return true;
	}
	std::int64_t event_size() const override { return 2; }
	type event_type() const override { return type::channel_aftertouch; }
};

struct pitch_change : midi_event
{
	std::uint8_t lsb, msb;
	std::uint8_t channel : 4;
	pitch_change(uint64_t absolute_tick, uint8_t channel, uint8_t lsb, uint8_t msb) : midi_event(absolute_tick), channel(channel), lsb(lsb), msb(msb) { }
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override 
	{
		if (!enabled)
			return false;

		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.resize(first_param_position + event_size());
		out[first_param_position] = (channel & 0xF) | (((uint8_t)event_type()) << 4);
		out[first_param_position + 1] = lsb;
		out[first_param_position + 2] = msb;
		return true;
	}
	std::int64_t event_size() const override { return 3; }
	type event_type() const override { return type::pitch_change; }

	inline operator uint16_t() const 
	{
		return ((msb & 0x7F) << 7) | (lsb & 0x7F);
	}
	inline uint16_t operator=(uint16_t value) 
	{
		lsb = value & 0x7F;
		msb = (value >> 7) & 0x7F;
		return value;
	}
};

struct meta : midi_event
{
	uint8_t* data;
	uint32_t meta_size;
	uint8_t meta_type;
	meta(uint64_t absolute_tick, uint8_t meta_type, uint32_t meta_size) : 
		midi_event(absolute_tick), meta_type(meta_type), data(new uint8_t[meta_size]), meta_size(meta_size) {	}
	~meta() override
	{
		delete[] data;
	}
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override 
	{
		if (!enabled)
			return false;
		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.push_back(0xFF);
		out.push_back(meta_type);
		details::push_vlv(meta_size, out);
		out.insert(out.end(), data, data + meta_size);
		return true;
	}
	std::int64_t event_size() const override { return 3 + (log((double)meta_size) / log(128.)) + meta_size; }
	type event_type() const override { return deduct_type(); }

	inline type deduct_type() const
	{
		switch (meta_type) {
		case 0x2F:
			return type::endoftrack;
		case 0x51:
			return type::tempo;
		case 0x0A:
			if (meta_size == 0x8 && data[0] == 0)
				return type::color;
			if (meta_size == 0xC && data[0] == 0)
				return type::grad_color;
		default:
			return type::meta;
		}
	}
};

struct sysex : midi_event
{
	uint8_t* data;
	uint32_t sysex_size;
	uint8_t first_byte;

	sysex(uint64_t absolute_tick, uint8_t first_byte, uint32_t sysex_size /*size of meta data*/) :
		midi_event(absolute_tick), first_byte(first_byte),
		data(new uint8_t[sysex_size]), sysex_size(sysex_size) { }
	~sysex() override 
	{
		delete[] data;
	}
	bool write_to_vector(std::int64_t prev_tick, std::vector<std::uint8_t>& out) const override 
	{
		if (!enabled)
			return false;
		auto delta_time = tick - prev_tick;
		if (delta_time < 0)
			throw std::runtime_error("Negative delta time");
		details::push_vlv(delta_time, out);
		auto first_param_position = out.size();
		out.push_back(first_byte);
		details::push_vlv(sysex_size, out);
		out.insert(out.end(), data, data + sysex_size);
		return true;
	}
	std::int64_t event_size() const override { return 3 + (log((double)sysex_size) / log(128.)) + sysex_size; } 
	type event_type() const override { return type::sysex; }
};

struct tempo : meta
{
	tempo(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
	inline operator float() 
	{
		uint32_t t = (data[0] << 16) | (data[1] << 8) | data[2];
		return (60000000.f / t);
	}
	inline const float& operator=(const float& tempo)
	{
		uint32_t t = (60000000 / tempo);
		data[0] = (t >> 16) & 0xFF;
		data[1] = (t >> 8) & 0xFF;
		data[2] = (t) & 0xFF;
		return tempo;
	}
};

struct single_color_event : meta {
	single_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
	uint8_t& r() { return data[4]; }
	uint8_t& g() { return data[5]; }
	uint8_t& b() { return data[6]; }
	uint8_t& a() { return data[7]; }
};

struct gradient_color_event : meta {
	gradient_color_event(uint64_t absolute_tick, uint8_t meta_type, uint32_t size) : meta(absolute_tick, meta_type, size) {}
	uint8_t& r1() { return data[4]; }
	uint8_t& g1() { return data[5]; }
	uint8_t& b1() { return data[6]; }
	uint8_t& a1() { return data[7]; }
	uint8_t& r2() { return data[8]; }
	uint8_t& g2() { return data[9]; }
	uint8_t& b2() { return data[10]; }
	uint8_t& a2() { return data[11]; }
};

struct events_reprocessor 
{
	virtual midi_event* operator()(midi_event* event);
};

/* constructs a view of midi track */
struct midi_track {

	struct parse_error_reporter 
	{
		virtual void report_warning(std::string str) { std::clog << str << std::endl; };
		virtual void report_error(std::string str) { std::cerr << str << std::endl; };
	};

	struct track_reading_settings
	{
		bool legacy_rsb_handling;
		bool throw_on_negative_polyphony;
		bool report_on_negative_polyhony;
		bool continue_parsing_after_end_of_track;
		bool report_on_corruption_detection;
	};

	struct lazy_direct_track_reader_data
	{
		uint64_t file_MTrk_position;
		uint64_t expected_track_size;
		uint64_t current_tick;
		uint8_t running_status_byte;
		midi_track::parse_error_reporter* error_reporter;
		bool active_track;
		bool first_run;
		lazy_direct_track_reader_data(midi_track::parse_error_reporter* error_reporter) :
			error_reporter(error_reporter)
		{
			file_MTrk_position = 0;
			expected_track_size = 0;
			current_tick = 0;
			running_status_byte = 0;
			active_track = true;
			first_run = true;
		}

	};

	std::vector<midi_event*> events;
	std::vector<full_note_view> note_views;
	full_note_view::note_events_ordering events_ordering;

	parse_error_reporter* error_reporter;
	bool is_view_of_track_data;

	midi_track():
		error_reporter(new parse_error_reporter)
	{

	}

	midi_track(parse_error_reporter* error_reporter) 
		:error_reporter(error_reporter)
	{

	}

	~midi_track()
	{
		delete error_reporter;
		if (!is_view_of_track_data)
		{
			release_track_data();
		}
	}

	void release_track_data()
	{
		note_views.clear();
		for (auto ptr : events)
		{
			delete ptr;
		}
	}

	/* nullptr signs the end of track */
	inline static midi_event* lazy_direct_track_reading(
		bbb_ffr* file_input, 
		track_reading_settings* reading_settings,
		lazy_direct_track_reader_data* lazy_data
	)
	{
		auto first_run_initialiser = [&]() -> bool
		{
			uint32_t header = 0;

			while (header != details::expected_track_header && file_input->good() && !file_input->eof())
				header = (header << 8) | file_input->get();

			lazy_data->file_MTrk_position = file_input->tellg() - 4;

			/*passing through tracksize*/
			for (int i = 0; i < 4; i++)
			{
				auto track_size_byte = file_input->get();
				lazy_data->expected_track_size = (lazy_data->expected_track_size << 8) | track_size_byte;
			}

			if (file_input->eof())
				return false;

			lazy_data->first_run = false;
			return true;
		};

		if (lazy_data->first_run) {
			auto init_state = first_run_initialiser();
			if (!init_state)
				return nullptr;
		}

		bool track_state = 
			(lazy_data->active_track || reading_settings->continue_parsing_after_end_of_track) 
			&& file_input->good() && !file_input->eof();

		if (track_state)
			return nullptr;

		uint8_t event_type = 0;
		auto [new_delta_time, delta_time_length] = details::get_vlv(*file_input);
		lazy_data->current_tick += new_delta_time;
		event_type = file_input->get();

		if (reading_settings->continue_parsing_after_end_of_track &&
			!lazy_data->active_track && new_delta_time == 0x4D && event_type == 0x54)
		{
			auto supposed_r = file_input->get();
			auto supposed_k = file_input->get();
			bool MTrk_found = false;
			if (supposed_k == 0x72 && supposed_r == 0x6B)
				MTrk_found = true;
			file_input->seekg(file_input->tellg() - 4);
			if (MTrk_found)
				return nullptr;
		}

		switch (event_type) {
			SAF_MRWF_MULTICHANNEL_CASE(0x8)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t key = file_input->get();
				uint8_t velocity = file_input->get();
				uint8_t channel = event_type & 0xF;
				uint16_t index = ((uint16_t)key << 4) | (channel);

				auto event_ptr = new noteoff(lazy_data->current_tick, channel, key, velocity);
				event_ptr->enabled = lazy_data->active_track;

				return event_ptr;
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0x9)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t key = file_input->get();
				uint8_t velocity = file_input->get();
				uint8_t channel = event_type & 0xF;
				uint16_t index = ((uint16_t)key << 4) | (channel);
				if (velocity) {
					auto event_ptr = new noteon(lazy_data->current_tick, channel, key, velocity);
					event_ptr->enabled = lazy_data->active_track;
					return event_ptr;
				}
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0xA)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t key = file_input->get();
				uint8_t value = file_input->get();
				uint8_t channel = event_type & 0xF;
				auto event_ptr = new note_aftertouch(lazy_data->current_tick, channel, key, value);
				event_ptr->enabled = lazy_data->active_track;
				return event_ptr;
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0xB)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t contr_no = file_input->get();
				uint8_t value = file_input->get();
				uint8_t channel = event_type & 0xF;
				auto event_ptr = (new controller(lazy_data->current_tick, channel, contr_no, value));
				event_ptr->enabled = lazy_data->active_track;
				return event_ptr;
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0xC)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t prog_no = file_input->get();
				uint8_t channel = event_type & 0xF;
				auto event_ptr = (new program_change(lazy_data->current_tick, channel, prog_no));
				event_ptr->enabled = lazy_data->active_track;
				return event_ptr;
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0xD)
			{
				lazy_data->running_status_byte = event_type;
				uint8_t value = file_input->get();
				uint8_t channel = event_type & 0xF;
				auto event_ptr = (new channel_aftertouch(lazy_data->current_tick, channel, value));
				event_ptr->enabled = lazy_data->active_track;
				return event_ptr;
			}
			break;
			SAF_MRWF_MULTICHANNEL_CASE(0xE) 
			{
				lazy_data->running_status_byte = event_type;
				uint8_t pitch_lsb = file_input->get();
				uint8_t pitch_msb = file_input->get();
				uint8_t channel = event_type & 0xF;
				auto event_ptr = (new pitch_change(lazy_data->current_tick, channel, pitch_lsb, pitch_msb));
				event_ptr->enabled = lazy_data->active_track;
				return event_ptr;
			}
			break;
			case(0xFF): 
			{
				if (!reading_settings->legacy_rsb_handling)
					lazy_data->running_status_byte = 0;
				uint8_t meta_type = file_input->get();
				auto [meta_length, meta_length_length] = details::get_vlv(*file_input);
				auto event_ptr = new meta(lazy_data->current_tick, meta_type, meta_length);
				event_ptr->enabled = lazy_data->active_track;
				for (size_t i = 0; i < meta_length; i++)
					event_ptr->data[i] = file_input->get();
				if (event_ptr->deduct_type() == midi_event::type::endoftrack)
				{
					event_ptr->enabled = false;
					lazy_data->active_track = false;
					if (!reading_settings->continue_parsing_after_end_of_track)
						return nullptr;
				}
				return event_ptr;
			}
			break;
			case(0xF7): case(0xF0): 
			{
				if (!reading_settings->legacy_rsb_handling)
					lazy_data->running_status_byte = 0;
				auto [sysex_length, sysex_length_length] = details::get_vlv(*file_input);
				auto event_ptr = new sysex(lazy_data->current_tick, event_type, sysex_length);
				for (size_t i = 0; i < sysex_length; i++)
					event_ptr->data[i] = file_input->get();
				event_ptr->enabled = false;
			}
			break;
			default: 
			{
				switch (lazy_data->running_status_byte) 
				{
					SAF_MRWF_MULTICHANNEL_CASE(0x8) 
					{
						uint8_t key = lazy_data->running_status_byte;
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel);
						auto event_ptr = new noteoff(lazy_data->current_tick, channel, key, velocity);
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0x9)
					{
						uint8_t key = lazy_data->running_status_byte;
						uint8_t velocity = file_input->get();
						uint8_t channel = event_type & 0xF;
						uint16_t index = ((uint16_t)key << 4) | (channel);
						if (velocity) {
							auto event_ptr = new noteon(lazy_data->current_tick, channel, key, velocity);
							event_ptr->enabled = lazy_data->active_track;
							return event_ptr;
						}
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0xA)
					{
						uint8_t key = lazy_data->running_status_byte;
						uint8_t value = file_input->get();
						uint8_t channel = event_type & 0xF;
						auto event_ptr = (new note_aftertouch(lazy_data->current_tick, channel, key, value));
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0xB) 
					{
						uint8_t contr_no = lazy_data->running_status_byte;
						uint8_t value = file_input->get();
						uint8_t channel = event_type & 0xF;
						auto event_ptr = (new controller(lazy_data->current_tick, channel, contr_no, value));
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0xC) 
					{
						uint8_t prog_no = lazy_data->running_status_byte;
						uint8_t channel = event_type & 0xF;
						auto event_ptr = (new program_change(lazy_data->current_tick, channel, prog_no));
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0xD)
					{
						uint8_t value = lazy_data->running_status_byte;
						uint8_t channel = event_type & 0xF;
						auto event_ptr = (new channel_aftertouch(lazy_data->current_tick, channel, value));
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					SAF_MRWF_MULTICHANNEL_CASE(0xE)
					{
						uint8_t pitch_lsb = lazy_data->running_status_byte;
						uint8_t pitch_msb = file_input->get();
						uint8_t channel = event_type & 0xF;
						auto event_ptr = (new pitch_change(lazy_data->current_tick, channel, pitch_lsb, pitch_msb));
						event_ptr->enabled = lazy_data->active_track;
						return event_ptr;
					}
					break;
					default: 
					{
						if (reading_settings->report_on_corruption_detection) 
						{
							std::stringstream message_ss;
							message_ss << std::hex << "Detected chunk of corrupted data. Event " << event_type << " with RSB " << lazy_data->running_status_byte << " at " << file_input->tellg() << std::dec << ", tick " << lazy_data->current_tick << "." << std::endl;
							lazy_data->error_reporter->report_error(message_ss.str());
						}
						return nullptr;
					}	
				}//switch(rsb)
			}//default
			break;
		}//switch(event_type)
	}

	void read_track(bbb_ffr* file_input, track_reading_settings* reading_settings)
	{
		std::array<std::stack<full_note_view>, 4096> polyphony;

		lazy_direct_track_reader_data lazy_data(error_reporter);
		midi_event* event_ptr = nullptr;

		do
		{
			event_ptr = lazy_direct_track_reading(file_input, reading_settings, &lazy_data);
			events.push_back(event_ptr);
			switch (event_ptr->event_type()) 
			{
				case midi_event::type::noteon: 
				{
					auto noteon_ptr = (noteon*)event_ptr;

					uint16_t index = ((uint16_t)noteon_ptr->key << 4) | (noteon_ptr->channel);

					polyphony[index].push({ &events_ordering , noteon_ptr, nullptr });
				}
				break;
				case midi_event::type::noteoff:
				{
					auto noteoff_ptr = (noteoff*)event_ptr;

					uint16_t index = ((uint16_t)noteoff_ptr->key << 4) | (noteoff_ptr->channel);

					if (polyphony[index].size()) 
					{
						note_views.push_back(polyphony[index].top());
						polyphony[index].pop();
						note_views.back().end = noteoff_ptr;
					}
					else if (reading_settings->report_on_negative_polyhony) 
					{
						auto message = "Negative polyphony at " + std::to_string(lazy_data.current_tick);
						error_reporter->report_warning(message);
						if (reading_settings->throw_on_negative_polyphony)
							throw std::runtime_error(message);
					}
				}
				break;
			}
		} while (event_ptr);


		for (auto unmet_note_view_stack : polyphony) 
		{
			while (unmet_note_view_stack.size()) 
			{
				auto new_noteoff = new noteoff(events.back()->tick,
					unmet_note_view_stack.top().get_channel(),
					unmet_note_view_stack.top().get_key(),
					unmet_note_view_stack.top().get_volume());
				events.push_back(new_noteoff);
				note_views.push_back(unmet_note_view_stack.top());
				unmet_note_view_stack.pop();
				note_views.back().end = new_noteoff;
			}
		}
	}
};

struct track_to_track_lazy_merger
{

};

#endif