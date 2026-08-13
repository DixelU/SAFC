#pragma once
#ifndef SAF_SMPR_LEAN
#define SAF_SMPR_LEAN

#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <atomic>
#include <memory>

#include <versions/bbb_ffio/safc/bbb_ffio.h>
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

	// Streaming track writer. Bytes accumulate in a fixed-size buffer; when it
	// fills, the MTrk header is committed (with a placeholder length on the
	// first overflow) and the buffer is drained to the file. finish() either
	// patches the length field via seekp on a previously-committed track, or
	// emits the header + buffer in one shot for tracks that never overflowed.
	struct track_writer
	{
		static constexpr std::size_t capacity = 1u << 20; // 1 MiB

		std::vector<base_type>& buffer;
		std::ofstream& out;
		std::size_t pos = 0;
		std::uint64_t flushed = 0;
		std::ofstream::pos_type mtrk_start;
		bool header_written = false;

		track_writer(std::vector<base_type>& buf, std::ofstream& os)
			: buffer(buf), out(os), mtrk_start(os.tellp()) {}

		FORCEDINLINE void push(base_type b)
		{
			if (pos == capacity) [[unlikely]]
				commit_buffer();
			buffer[pos++] = b;
		}

		void commit_buffer()
		{
			if (!header_written)
			{
				base_type hdr[8] = { 'M','T','r','k', 0, 0, 0, 0 };
				out.write((const char*)hdr, 8);
				header_written = true;
			}
			if (pos)
			{
				out.write((const char*)buffer.data(), pos);
				flushed += pos;
				pos = 0;
			}
		}

		// Returns true if the track was emitted to the output file.
		bool finish(bool skip)
		{
			if (skip)
			{
				// any_event=false implies only the 4-byte synthesized EOT was
				// pushed, which can't have overflowed a 1 MiB buffer, so the
				// header has not been written yet — just discard.
				pos = 0;
				return false;
			}

			if (!header_written)
			{
				std::uint32_t size = std::uint32_t(pos);
				base_type hdr[8] = {
					'M','T','r','k',
					base_type((size >> 24) & 0xFF),
					base_type((size >> 16) & 0xFF),
					base_type((size >>  8) & 0xFF),
					base_type((size      ) & 0xFF)
				};
				out.write((const char*)hdr, 8);
				out.write((const char*)buffer.data(), pos);
				pos = 0;
				return true;
			}

			commit_buffer();
			std::uint64_t size = flushed;
			auto end_pos = out.tellp();
			out.seekp(mtrk_start + std::ofstream::off_type(4));
			base_type sz[4] = {
				base_type((size >> 24) & 0xFF),
				base_type((size >> 16) & 0xFF),
				base_type((size >>  8) & 0xFF),
				base_type((size      ) & 0xFF)
			};
			out.write((const char*)sz, 4);
			out.seekp(end_pos);
			return true;
		}
	};

	FORCEDINLINE static std::uint8_t push_vlv(std::uint64_t value, track_writer& w)
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
			w.push(stack[i]);

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

	// Writes delta-time VLV through `w`, splitting across dummy meta events when
	// it overflows the 4-byte VLV ceiling. Each split emits FF 7F 01 00 with
	// limit-delta. rsb_out is cleared because meta events reset running status.
	FORCEDINLINE static void emit_delta(
		track_writer& w,
		tick_type delta,
		base_type& rsb_out,
		bool force_overflow_correction)
	{
		if (force_overflow_correction)
		{
			while (delta > deltatime_limit) [[unlikely]]
			{
				push_vlv(deltatime_limit, w);
				w.push(0xFF);
				w.push(0x7F);
				w.push(0x01);
				w.push(0x00);
				rsb_out = 0;
				delta -= deltatime_limit;
			}
		}
		push_vlv(delta, w);
	}

	// Channel-voice status byte emission. When compression is on and the status
	// matches the running one, the status is elided; otherwise it is written.
	// 0xFn events never participate in running status.
	FORCEDINLINE static void emit_channel_status(
		track_writer& w,
		base_type status,
		base_type& rsb_out,
		bool compression)
	{
		if (!compression || status != rsb_out)
			w.push(status);
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
		if (!s.filter.pass_tempo)               return false;
		if (!s.filter.pass_pitch)               return false;
		if (!s.filter.pass_notes)               return false;
		if (!s.filter.pass_other)               return false;
		return true;
	}

	static bool process_track(
		bbb_ffr& in,
		std::ofstream& out,
		std::vector<base_type>& track_buffer,
		const settings_obj& settings,
		message_buffers& buffers,
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

		track_writer writer(track_buffer, out);

		tick_type current_tick = 0;
		tick_type previous_tick = 0;

		base_type rsb_in  = 0; // input running status
		base_type rsb_out = 0; // output running status (for compression)

		bool track_ended = false;
		bool any_event   = false;

		const bool compression = settings.legacy.rsb_compression;
		const bool overflow_fix = settings.proc_details.force_delta_overflow_correction;

		while (in.good() && !track_ended)
		{
			auto delta = get_vlv(in);
			if (in.eof())
				break;

			current_tick += delta;
			tick_type new_abs = compute_new_abs_tick(current_tick, settings);
			if (new_abs < previous_tick)
				new_abs = previous_tick;
			tick_type new_delta = new_abs - previous_tick;

			base_type cmd = in.get();
			base_type p1 = 0;
			bool p1_consumed = false;

			if (cmd < 0x80)
			{
				if (rsb_in < 0x80) [[unlikely]]
				{
					(*buffers.error) << log_event{log_event_type::unexpected_zero_rsb, (uint64_t)(std::streamoff)in.tellg()};
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
					if (!settings.legacy.enable_zero_velocity) [[likely]]
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

					emit_delta(writer, new_delta, rsb_out, overflow_fix);
					emit_channel_status(writer, cmd, rsb_out, compression);
					writer.push(key);
					writer.push(vel);

					previous_tick = new_abs;
					any_event = true;
					break;
				}
				case 0xA: case 0xB: case 0xE:
				{
					rsb_in = cmd;
					base_type a = p1_consumed ? p1 : in.get();
					base_type b = in.get();

					emit_delta(writer, new_delta, rsb_out, overflow_fix);
					emit_channel_status(writer, cmd, rsb_out, compression);
					writer.push(a);
					writer.push(b);

					previous_tick = new_abs;
					any_event = true;
					break;
				}
				case 0xC:
				{
					if (settings.filter.piano_only)
					{
						rsb_in = cmd;
						base_type read_param = p1_consumed ? p1 : in.get();
						break;
					}

					[[fallthrough]];
				}
				case 0xD:
				{
					rsb_in = cmd;
					base_type read_param1 = p1_consumed ? p1 : in.get();

					emit_delta(writer, new_delta, rsb_out, overflow_fix);
					emit_channel_status(writer, cmd, rsb_out, compression);
					writer.push(read_param1);

					previous_tick = new_abs;
					any_event = true;
					break;
				}
				case 0xF:
				{
					if (!settings.legacy.ignore_meta_rsb)
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

							std::uint32_t new_tempo = settings.tempo.process(tempo);
							if (!new_tempo)
								continue;

							emit_delta(writer, new_delta, rsb_out, overflow_fix);
							writer.push(0xFF);
							writer.push(0x51);
							writer.push(0x03);
							writer.push(base_type((new_tempo >> 16) & 0xFF));
							writer.push(base_type((new_tempo >>  8) & 0xFF));
							writer.push(base_type((new_tempo      ) & 0xFF));
							rsb_out = 0;

							previous_tick = new_abs;
							any_event = true;
							break;
						}

						emit_delta(writer, new_delta, rsb_out, overflow_fix);
						writer.push(0xFF);
						writer.push(meta_type);
						push_vlv(len, writer);
						for (std::uint64_t i = 0; i < len && in.good(); ++i)
							writer.push(in.get());
						rsb_out = 0;

						previous_tick = new_abs;
						any_event = true;
						break;
					}
					else if (cmd == 0xF0 || cmd == 0xF7)
					{
						if (!settings.filter.pass_sysex)
						{
							std::uint64_t len = get_vlv(in);
							for (std::uint64_t i = 0; i < len && in.good(); ++i)
								in.get();
							break;
						}

						std::uint64_t len = get_vlv(in);

						emit_delta(writer, new_delta, rsb_out, overflow_fix);
						writer.push(cmd);
						push_vlv(len, writer);

						for (std::uint64_t i = 0; i < len && in.good(); ++i)
							writer.push(in.get());
						rsb_out = 0;

						previous_tick = new_abs;
						any_event = true;
						break;
					}
					else
					{
						(*buffers.error) << log_event{log_event_type::unknown_event_type, (uint64_t)(std::streamoff)in.tellg(), (uint64_t)cmd};
						return false;
					}
				}
				default: {
					(*buffers.error) << log_event{log_event_type::unknown_event_type, (uint64_t)(std::streamoff)in.tellg(), (uint64_t)cmd};
					return false;
				}
			}
		}

		writer.push(0x00);
		writer.push(0xFF);
		writer.push(0x2F);
		writer.push(0x00);

		const bool skip = settings.proc_details.remove_empty_tracks && !any_event;
		if (writer.finish(skip))
			++tracks_written;

		return true;
	}

	static void sync_processing(processing_data& data, message_buffers& loggers)
	{
		loggers.processing = true;

		std::vector<base_type> track(track_writer::capacity); // 1 MiB fixed buffer

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
			(*loggers.log) << log_event{log_event_type::tracks_processed, (uint64_t)tracks_written, 1};
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
