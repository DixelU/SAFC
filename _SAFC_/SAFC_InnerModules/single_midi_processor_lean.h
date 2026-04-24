#pragma once
#ifndef SAF_SMPR_LEAN
#define SAF_SMPR_LEAN

#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <atomic>
#include <memory>

#include "../bbb_ffio.h"
#include "../SAFGUIF/header_utils.h"

#include "single_midi_processor_2.h"

struct single_midi_processor_lean
{
	using base_type = std::uint8_t;
	using tick_type = std::uint64_t;
	using sgtick_type = std::int64_t;
	using ppq_type = std::uint16_t;

	inline static constexpr std::uint32_t MTrk_header = 1297379947;
	inline static constexpr std::uint32_t MThd_header = 1297377380;

	// Max 4-byte VLV absolute value (0x0FFFFFFF). Anything bigger must be
	// split with dummy meta events between.
	inline static constexpr std::uint32_t deltatime_limit = (1u << 28) - 1;

	// Settings / IO types reused from the feature-rich processor so the merger
	// can call either path with the same processing_data.
	using settings_obj     = single_midi_processor_2::settings_obj;
	using processing_data  = single_midi_processor_2::processing_data;
	using message_buffers  = single_midi_processor_2::message_buffers;

	FORCEDINLINE static std::uint8_t push_vlv(std::uint64_t value, std::vector<base_type>& vec)
	{
		base_type stack[11];
		std::uint8_t size = 0;
		do
		{
			stack[size++] = base_type(value & 0x7F);
			value >>= 7;
		} while (value);

		for (std::uint8_t i = 1; i < size; ++i)
			stack[i] |= 0x80;

		for (std::uint8_t i = size; i-- > 0; )
			vec.push_back(stack[i]);

		return size;
	}

	FORCEDINLINE static std::uint64_t get_vlv(bbb_ffr& in)
	{
		std::uint64_t value = 0;
		base_type byte;
		do
		{
			byte = in.get();
			value = (value << 7) | (byte & 0x7F);
		} while ((byte & 0x80) && !in.eof());
		return value;
	}

	FORCEDINLINE static tick_type convert_ppq(tick_type value, ppq_type from, ppq_type to)
	{
		if (from == to)
			return value;

		constexpr auto radix = 1ull << 32;
		auto hi = value >> 32;
		auto lo = value & (~0u);
		return (hi * to / from) * radix + (lo * to / from);
	}

	FORCEDINLINE static tick_type compute_new_abs_tick(
		tick_type old_abs,
		const settings_obj& s)
	{
		if (s.proc_details.apply_offset_after)
		{
			sgtick_type t = sgtick_type(convert_ppq(old_abs, s.old_ppqn, s.new_ppqn)) + s.offset;
			return t < 0 ? 0 : tick_type(t);
		}

		sgtick_type t = sgtick_type(old_abs) + s.offset;
		if (t < 0)
			t = 0;

		return convert_ppq(tick_type(t), s.old_ppqn, s.new_ppqn);
	}

	// Writes delta-time VLV into `track`, splitting across dummy meta events when
	// it overflows the 4-byte VLV ceiling. Each split emits FF 7F 01 00 with
	// limit-delta. rsb_out is cleared because meta events reset running status.
	FORCEDINLINE static void emit_delta(
		std::vector<base_type>& track,
		tick_type delta,
		base_type& rsb_out,
		bool force_overflow_correction)
	{
		if (force_overflow_correction)
		{
			while (delta > deltatime_limit) [[unlikely]]
			{
				push_vlv(deltatime_limit, track);
				track.push_back(0xFF);
				track.push_back(0x7F);
				track.push_back(0x01);
				track.push_back(0x00);
				rsb_out = 0;
				delta -= deltatime_limit;
			}
		}
		push_vlv(delta, track);
	}

	// Channel-voice status byte emission. When compression is on and the status
	// matches the running one, the status is elided; otherwise it is written.
	// 0xFn events never participate in running status.
	FORCEDINLINE static void emit_channel_status(
		std::vector<base_type>& track,
		base_type status,
		base_type& rsb_out,
		bool compression)
	{
		if (!compression || status != rsb_out)
			track.push_back(status);
		rsb_out = status;
	}

	// True when every setting the caller has turned on is one the lean path
	// actually implements. The feature-rich processor covers the rest.
	static bool can_handle(const settings_obj& s)
	{
		if (s.selection_data.begin != 0 || s.selection_data.length != ~0ULL)
			return false;
		if (s.flatten)                          return false;
		if (s.key_converter)                    return false;
		if (s.volume_map)                       return false;
		if (s.pitch_map)                        return false;
		if (s.proc_details.channel_split)       return false;
		if (s.proc_details.whole_midi_collapse) return false;
		if (s.enable_imp_events_filter)         return false;
		if (s.filter.piano_only)                return false;
		if (!s.filter.pass_tempo)               return false;
		if (!s.filter.pass_pitch)               return false;
		if (!s.filter.pass_notes)               return false;
		if (!s.filter.pass_other)               return false;
		return true;
	}

	static bool process_track(
		bbb_ffr& in,
		std::ofstream& out,
		std::vector<base_type>& track,
		const settings_obj& s,
		message_buffers& msg,
		bool& reached_eof,
		std::uint64_t& tracks_written)
	{
		std::uint32_t hdr = 0;
		while (hdr != MTrk_header && in.good() && !in.eof())
			hdr = (hdr << 8) | in.get();

		if (in.eof())
		{
			reached_eof = true;
			return false;
		}

		for (int i = 0; i < 4 && in.good(); ++i)
			in.get();

		if (in.eof())
		{
			reached_eof = true;
			return false;
		}

		track.clear();

		tick_type old_abs = 0;
		tick_type prev_new_abs = 0;

		base_type rsb_in  = 0; // input running status
		base_type rsb_out = 0; // output running status (for compression)

		bool track_ended = false;
		bool any_event   = false;
		std::size_t noteoff_misses = 0;

		const bool compression = s.legacy.rsb_compression;
		const bool overflow_fix = s.proc_details.force_delta_overflow_correction;

		while (in.good() && !track_ended)
		{
			auto delta = get_vlv(in);
			if (in.eof())
				break;

			old_abs += delta;
			tick_type new_abs = compute_new_abs_tick(old_abs, s);
			if (new_abs < prev_new_abs)
				new_abs = prev_new_abs;
			tick_type new_delta = new_abs - prev_new_abs;

			base_type cmd = in.get();
			base_type p1 = 0;
			bool p1_consumed = false;

			if (cmd < 0x80)
			{
				if (rsb_in < 0x80) [[unlikely]]
				{
					(*msg.error) << (std::to_string(in.tellg()) +
						": Unexpected data byte with no running status");
					return false;
				}

				p1 = cmd;
				cmd = rsb_in;
				p1_consumed = true;
			}

			switch (cmd >> 4)
			{
				case 0x8: case 0x9:
				{
					rsb_in = cmd;
					base_type key = p1_consumed ? p1 : in.get();
					base_type vel = in.get();

					// 0x9x vel=0 normalisation: collapse to 0x8x vel=0 unless the
					// legacy flag wants to keep 0x9x (in which case vel=0 is bumped
					// to 1 to keep it as a note-on).
					if (!s.legacy.enable_zero_velocity) [[likely]]
					{
						if ((cmd & 0x10) && !vel)
							cmd &= ~0x10;
					}
					else if ((cmd & 0x10) && !vel)
					{
						vel = 1;
					}

					// RSB compression switch-up: convert 0x8x back to 0x9x vel=0 so
					// all note events share the 0x9x status byte.
					if (compression && (cmd & 0xF0) == 0x80)
					{
						cmd = 0x90 | (cmd & 0x0F);
						vel = 0;
					}

					emit_delta(track, new_delta, rsb_out, overflow_fix);
					emit_channel_status(track, cmd, rsb_out, compression);
					track.push_back(key);
					track.push_back(vel);

					prev_new_abs = new_abs;
					any_event = true;
					break;
				}
				case 0xA: case 0xB: case 0xE:
				{
					rsb_in = cmd;
					base_type a = p1_consumed ? p1 : in.get();
					base_type b = in.get();

					emit_delta(track, new_delta, rsb_out, overflow_fix);
					emit_channel_status(track, cmd, rsb_out, compression);
					track.push_back(a);
					track.push_back(b);

					prev_new_abs = new_abs;
					any_event = true;
					break;
				}
				case 0xC: case 0xD:
				{
					rsb_in = cmd;
					base_type a = p1_consumed ? p1 : in.get();

					emit_delta(track, new_delta, rsb_out, overflow_fix);
					emit_channel_status(track, cmd, rsb_out, compression);
					track.push_back(a);

					prev_new_abs = new_abs;
					any_event = true;
					break;
				}
				case 0xF:
				{
					if (!s.legacy.ignore_meta_rsb)
						rsb_in = 0;

					if (cmd == 0xFF)
					{
						// Meta event. p1_consumed can't be true here because 0xFF
						// is never reached via running status.
						base_type meta_type = in.get();

						if (meta_type == 0x2F)
						{
							std::uint64_t eot_len = get_vlv(in);
							for (std::uint64_t i = 0; i < eot_len; ++i)
								in.get();
							track_ended = true;
							break;
						}

						std::uint64_t len = get_vlv(in);

						if (meta_type == 0x51 && len == 3)
						{
							base_type tb1 = in.get();
							base_type tb2 = in.get();
							base_type tb3 = in.get();
							std::uint32_t tempo =
								(std::uint32_t(tb1) << 16) |
								(std::uint32_t(tb2) << 8)  |
								 std::uint32_t(tb3);

							std::uint32_t new_tempo = s.tempo.process(tempo);
							if (!new_tempo)
								continue;

							emit_delta(track, new_delta, rsb_out, overflow_fix);
							track.push_back(0xFF);
							track.push_back(0x51);
							track.push_back(0x03);
							track.push_back(base_type((new_tempo >> 16) & 0xFF));
							track.push_back(base_type((new_tempo >>  8) & 0xFF));
							track.push_back(base_type((new_tempo      ) & 0xFF));
							rsb_out = 0;

							prev_new_abs = new_abs;
							any_event = true;
							break;
						}

						emit_delta(track, new_delta, rsb_out, overflow_fix);
						track.push_back(0xFF);
						track.push_back(meta_type);
						push_vlv(len, track);
						for (std::uint64_t i = 0; i < len && in.good(); ++i)
							track.push_back(in.get());
						rsb_out = 0;

						prev_new_abs = new_abs;
						any_event = true;
						break;
					}
					else if (cmd == 0xF0 || cmd == 0xF7)
					{
						if (!s.filter.pass_sysex)
						{
							std::uint64_t len = get_vlv(in);
							for (std::uint64_t i = 0; i < len && in.good(); ++i)
								in.get();
							break;
						}

						std::uint64_t len = get_vlv(in);
						emit_delta(track, new_delta, rsb_out, overflow_fix);
						track.push_back(cmd);
						push_vlv(len, track);
						for (std::uint64_t i = 0; i < len && in.good(); ++i)
							track.push_back(in.get());
						rsb_out = 0;

						prev_new_abs = new_abs;
						any_event = true;
						break;
					}
					else
					{
						(*msg.error) << (std::to_string(in.tellg()) +
							": Unsupported 0xFx status " + std::to_string(cmd));
						return false;
					}
				}
				default: {
					(*msg.error) << (std::to_string(in.tellg()) +
						": Unknown event type " + std::to_string(cmd));
					return false;
				}
			}
		}

		if (!track_ended)
		{
			track.push_back(0x00);
			track.push_back(0xFF);
			track.push_back(0x2F);
			track.push_back(0x00);
			(*msg.warning) << "Track ended without explicit EOT — synthesized";
		}
		else
		{
			track.push_back(0x00);
			track.push_back(0xFF);
			track.push_back(0x2F);
			track.push_back(0x00);
		}

		const bool skip = s.proc_details.remove_empty_tracks && !any_event;
		if (!skip)
		{
			base_type header[8] = {
				'M','T','r','k',
				base_type((track.size() >> 24) & 0xFF),
				base_type((track.size() >> 16) & 0xFF),
				base_type((track.size() >>  8) & 0xFF),
				base_type((track.size()      ) & 0xFF)
			};
			out.write((const char*)header, 8);
			out.write((const char*)track.data(), track.size());
			++tracks_written;
		}

		return true;
	}

	static void sync_processing(processing_data& data, message_buffers& loggers)
	{
		loggers.processing = true;

		std::vector<base_type> track;
		track.reserve(1ull << 22); // 4 MiB, grows as needed

		bbb_ffr file_input(data.filename.c_str());
		std::ofstream file_output(data.filename + data.postfix,
			std::ios::binary | std::ios::out);

		for (int i = 0; i < 12 && file_input.good(); ++i)
			file_output.put(file_input.get());

		data.settings.old_ppqn  = std::uint16_t(file_input.get()) << 8;
		data.settings.old_ppqn |= std::uint16_t(file_input.get());
		file_output.put(char(data.settings.new_ppqn >> 8));
		file_output.put(char(data.settings.new_ppqn & 0xFF));

		std::uint64_t tracks_written = 0;
		bool reached_eof = false;

		while (file_input.good() && !reached_eof)
		{
			process_track(
				file_input,
				file_output,
				track,
				data.settings,
				loggers,
				reached_eof,
				tracks_written);

			loggers.last_input_position = file_input.tellg();
			(*loggers.log) << (std::to_string(tracks_written) + " tracks written");
		}

		file_input.close();
		file_output.seekp(10, std::ios::beg);
		file_output.put(char(tracks_written >> 8));
		file_output.put(char(tracks_written & 0xFF));
		file_output.close();

		data.tracks_count = tracks_written;
		loggers.processing = false;
		loggers.finished = true;
	}
};

#endif // SAF_SMPR_LEAN
