#pragma once
#ifndef SAF_MCTM
#define SAF_MCTM

void throw_alert_error(std::string&& AlertText);
void throw_alert_warning(std::string&& AlertText);

#include <set>
#include <future>
#include <thread>
#include <vector>
#include <syncstream>

#include "single_midi_processor_2.h"

struct midi_track_iterator
{
	const std::uint8_t* track_data;
	std::uint64_t track_size;
	std::int64_t cur_position = 0;
	std::int64_t cur_tick = 0;
	std::array<std::int64_t, 4096> held{};
	bool processing = true;

	explicit midi_track_iterator(const std::vector<std::uint8_t>& vec) :
		track_data(vec.data()),
		track_size(vec.size())
	{}

	std::int64_t polyphony() const
	{
		std::int64_t total = 0;
		for (auto& v : held)
			total += v;
		return total;
	}

	void advance_single_event()
	{
		if (!processing || cur_position >= (std::int64_t)track_size)
		{
			processing = false;
			return;
		}
		std::uint32_t vlv = 0, io = 0;
		do
		{
			io = track_data[cur_position++];
			vlv = (vlv << 7) | (io & 0x7F);
		} while (io & 0x80);
		cur_tick += vlv;

		if (track_data[cur_position] == 0xFF)
		{
			cur_position++;
			if (track_data[cur_position] == 0x2F)
			{
				processing = false;
				cur_position += 2;
			}
			else
			{
				io = 0;
				cur_position++;
				do
				{
					io = track_data[cur_position++];
					vlv = (vlv << 7) | (io & 0x7F);
				} while (io & 0x80);
				for (std::uint32_t j = 0; j < io; j++)
					cur_position++;
			}
		}
		else if (track_data[cur_position] >= 0x80 && track_data[cur_position] <= 0x9F)
		{
			std::uint16_t ftd = track_data[cur_position++];
			std::uint16_t key = track_data[cur_position++];
			cur_position++; // velocity
			std::uint16_t index = (key << 4) | (ftd & 0xF);
			if (ftd & 0x10)
				held[index]++;
			else if (held[index] > 0)
				held[index]--;
		}
		else if ((track_data[cur_position] >= 0xA0 && track_data[cur_position] <= 0xBF) ||
		         (track_data[cur_position] >= 0xE0 && track_data[cur_position] <= 0xEF))
			cur_position += 3;
		else if (track_data[cur_position] >= 0xC0 && track_data[cur_position] <= 0xDF)
			cur_position += 2;
		else
			throw_alert_error("DTI Failure at " + std::to_string(cur_position) + ". Type: " +
				std::to_string(track_data[cur_position]) + ". Tell developer about it and give him source midi\n");
	}

	bool advance_to_position(std::int64_t position)
	{
		while (processing && cur_position < position)
			advance_single_event();
		return processing;
	}

	void emit_held_notes(std::vector<std::uint8_t>& out, bool output_noteon_wall)
	{
		constexpr std::uint32_t delta_trunk_edge = 0xF000000;
		std::uint64_t local_tick = cur_tick;
		while (output_noteon_wall && local_tick > delta_trunk_edge)
		{
			single_midi_processor_2::push_vlv_s(delta_trunk_edge, out);
			out.push_back(0xFF);
			out.push_back(0x01);
			out.push_back(0x00);
			local_tick -= delta_trunk_edge;
		}
		if (output_noteon_wall)
			single_midi_processor_2::push_vlv_s(local_tick, out);
		else
			out.push_back(0);
		for (int i = 0; i < 4096; i++)
		{
			std::int64_t key = held[i];
			while (key)
			{
				out.push_back((0x10 * output_noteon_wall) | (i & 0xF) | 0x80);
				out.push_back(i >> 4);
				out.push_back(1);
				out.push_back(0);
				key--;
			}
		}
		if (!out.empty())
			out.pop_back();
	}
};

struct midi_collection_threaded_merger
{
	using proc_data_ptr =
		std::shared_ptr<single_midi_processor_2::processing_data>;
	using message_buffer_ptr =
		std::shared_ptr<single_midi_processor_2::message_buffers>;

	// Observable stage flags — read by the GUI for progress display
	std::atomic_bool inplace_merge_complete{ false };
	std::atomic_bool regular_merge_complete{ false };
	std::atomic_bool complete{ false };
	std::atomic_uint64_t inplace_track_count{ 0 };
	std::atomic_uint64_t regular_track_count{ 0 };

	midi_collection_threaded_merger(
		std::vector<proc_data_ptr> processing_data,
		std::uint16_t final_ppqn,
		std::wstring save_to,
		bool is_console_oriented) :
		save_to_(std::move(save_to)),
		final_ppqn_(final_ppqn),
		remnants_remove_(true)
	{
		for (auto& d : processing_data)
			midi_processing_data_.emplace_back(
				d, std::make_shared<message_buffer_ptr::element_type>(is_console_oriented));
	}

	~midi_collection_threaded_merger() = default;

	void start_processing()
	{
		std::set<std::uint32_t> group_ids;
		for (auto& [pdata, _] : midi_processing_data_)
			group_ids.insert(pdata->settings.details.group_id);

		{
			std::lock_guard lock(currently_processed_mutex_);
			currently_processed_.assign(group_ids.size(), {});
		}

		std::uint32_t thread_index = 0;
		for (std::uint32_t group_id : group_ids)
		{
			std::thread([this](
				std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> data,
				std::uint32_t id,
				std::uint32_t idx)
			{
				for (auto& el : data)
				{
					if (el.first->settings.details.group_id != id)
						continue;

					{
						std::lock_guard lock(currently_processed_mutex_);
						currently_processed_[idx] = el;
					}

					if (el.first->settings.proc_details.channel_split)
						single_midi_processor_2::sync_processing<true>(*el.first, *el.second);
					else
						single_midi_processor_2::sync_processing<false>(*el.first, *el.second);
				}
			}, midi_processing_data_, group_id, thread_index).detach();
			++thread_index;
		}
	}

	bool is_smrp_complete() const
	{
		for (auto& [_, mbuf] : midi_processing_data_)
			if (!mbuf->finished || mbuf->processing)
				return false;
		return true;
	}

	void start_ri_merge()
	{
		using mpd_t = decltype(midi_processing_data_);
		mpd_t inplace_candidates, regular_candidates;
		for (auto& el : midi_processing_data_)
		{
			if (el.first->settings.details.inplace_mergable)
				inplace_candidates.push_back(el);
			else
				regular_candidates.push_back(el);
		}

		inplace_track_count = 0;
		regular_track_count = 0;

		inplace_merge_future_ = std::async(std::launch::async,
			[this, candidates = std::move(inplace_candidates)]() -> std::uint64_t
		{
			auto count = do_inplace_merge_impl(candidates, final_ppqn_, save_to_);
			inplace_track_count = count;
			inplace_merge_complete = true;
			return count;
		});

		regular_merge_future_ = std::async(std::launch::async,
			[this, candidates = std::move(regular_candidates)]() -> std::uint64_t
		{
			auto count = do_regular_merge_impl(candidates, final_ppqn_, save_to_);
			regular_track_count = count;
			regular_merge_complete = true;
			return count;
		});
	}

	bool is_ri_merge_complete() const
	{
		return inplace_merge_complete.load() && regular_merge_complete.load();
	}

	void start_final_merge()
	{
		auto ii_count = inplace_merge_future_.get();
		auto ir_count = regular_merge_future_.get();

		final_merge_future_ = std::async(std::launch::async,
			[this, ii_count, ir_count]()
		{
			do_final_merge_impl(save_to_, ii_count, ir_count, remnants_remove_);
			complete = true;
		});
	}

	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> snapshot_currently_processed() const
	{
		std::lock_guard lock(currently_processed_mutex_);
		return currently_processed_;
	}

	std::size_t currently_processed_count() const
	{
		std::lock_guard lock(currently_processed_mutex_);
		return currently_processed_.size();
	}

	template<typename F>
	void with_currently_processed_item(std::size_t idx, F&& func) const
	{
		std::lock_guard lock(currently_processed_mutex_);
		if (idx < currently_processed_.size())
			func(currently_processed_[idx]);
	}

private:
	std::wstring save_to_;
	std::uint16_t final_ppqn_;
	bool remnants_remove_;

	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> midi_processing_data_;

	mutable std::mutex currently_processed_mutex_;
	std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> currently_processed_;

	std::future<std::uint64_t> inplace_merge_future_;
	std::future<std::uint64_t> regular_merge_future_;
	std::future<void> final_merge_future_;

	static std::uint32_t read_vlv(bbb_ffr& f)
	{
		std::uint32_t result = 0;
		std::uint8_t b;
		do
		{
			b = f.get();
			result = (result << 7) | (b & 0x7F);
		} while (b & 0x80);
		return result;
	}

	static std::uint64_t do_inplace_merge_impl(
		const std::vector<std::pair<proc_data_ptr, message_buffer_ptr>>& candidates,
		std::uint16_t ppqn,
		const std::wstring& save_to)
	{
		if (candidates.empty())
			return 0;

		std::uint64_t track_count = 0;

		std::vector<std::unique_ptr<bbb_ffr>> streams;
		streams.reserve(candidates.size());
		for (auto& [pdata, _] : candidates)
		{
			auto& s = streams.emplace_back(std::make_unique<bbb_ffr>(
				(pdata->filename + pdata->postfix).c_str()));
			for (int i = 0; i < 14; i++)
				s->get();
		}

		std::ofstream out(save_to + L".I.mid", std::ios::binary | std::ios::out);
		out << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
		out.put(0); out.put(0); // track count placeholder, updated at end
		out.put((char)(ppqn >> 8));
		out.put((char)ppqn);

		std::vector<std::uint8_t> track, front_edge, back_edge;
		track.reserve(1'000'000);
		std::vector<std::int64_t> ddts(streams.size(), -1);

		bool active_stream = true;
		while (active_stream)
		{
			for (std::size_t i = 0; i < streams.size(); i++)
			{
				auto& s = *streams[i];
				std::uint32_t header = 0;
				while (s.good() && header != single_midi_processor_2::MTrk_header)
					header = (header << 8) | s.get();
				for (int w = 0; w < 4; w++)
					s.get(); // skip track size
				if (s.good())
				{
					ddts[i] = (std::int64_t)read_vlv(s);
					if (s.eof())
						ddts[i] = -1;
				}
				else
					ddts[i] = -1;
			}

			bool active_track = true;
			std::uint64_t in_track_delta = 0;

			while (active_track)
			{
				active_track = false;
				active_stream = false;

				for (std::size_t i = 0; i < streams.size(); i++)
				{
					auto& s = *streams[i];
					auto& ddt = ddts[i];

					if (ddt < 0)
					{
						if (s.good())
							active_stream = true;
						continue;
					}

					while (ddt == 0)
					{
						std::uint8_t event_type = s.get();
						auto delta_len = single_midi_processor_2::push_vlv(in_track_delta, track);
						in_track_delta = 0;

						bool track_ended = false;
						if (event_type == 0xFF)
						{
							std::uint8_t meta_type = s.get();
							if (meta_type == 0x2F)
							{
								s.get(); // skip 0x00 length byte
								for (int l = 0; l < (int)delta_len; l++)
									track.pop_back();
								track_ended = true;
							}
							else
							{
								track.push_back(event_type);
								track.push_back(meta_type);
								std::uint32_t meta_len = 0;
								std::uint8_t b;
								do
								{
									b = s.get();
									track.push_back(b);
									meta_len = (meta_len << 7) | (b & 0x7F);
								} while (b & 0x80);
								for (std::uint32_t j = 0; j < meta_len; j++)
									track.push_back(s.get());
							}
						}
						else if (event_type == 0xF0 || event_type == 0xF7)
						{
							track.push_back(event_type);
							std::uint32_t sysex_len = 0;
							std::uint8_t b;
							do
							{
								b = s.get();
								track.push_back(b);
								sysex_len = (sysex_len << 7) | (b & 0x7F);
							} while (b & 0x80);
							for (std::uint32_t j = 0; j < sysex_len; j++)
								track.push_back(s.get());
						}
						else if ((event_type >= 0x80 && event_type <= 0xBF) ||
						         (event_type >= 0xE0 && event_type <= 0xEF))
						{
							track.push_back(event_type);
							track.push_back(s.get());
							track.push_back(s.get());
						}
						else if (event_type >= 0xC0 && event_type <= 0xDF)
						{
							track.push_back(event_type);
							track.push_back(s.get());
						}
						else
						{
							auto pos = s.tellg();
							throw_alert_error("DTI Failure at " + std::to_string(pos) + ". Type: " +
								std::to_string(event_type) + ". Tell developer about it and give him source midis.\n");
							track.push_back(0xCA);
							track.push_back(0);
							track_ended = true;
						}

						if (track_ended)
						{
							ddt = -1;
							if (s.good())
								active_stream = true;
							break;
						}

						auto next_delta = read_vlv(s);
						if (s.eof())
						{
							ddt = -1;
							if (s.good())
								active_stream = true;
							break;
						}
						ddt = (std::int64_t)next_delta;
						if (ddt > 0)
						{
							ddt--;
							active_track = true;
							break;
						}
						// ddt == 0: loop and process next event immediately
					}

					if (ddt > 0)
					{
						active_track = true;
						ddt--;
					}
				}

				if (!active_track && !track.empty())
				{
					single_midi_processor_2::push_vlv(in_track_delta, track);
					in_track_delta = 0;
					track.push_back(0xFF);
					track.push_back(0x2F);
					track.push_back(0x00);
				}

				in_track_delta++;
			}

			if (track.empty())
				continue;

			if (track.size() > 0xFFFFFFFFu)
			{
				std::osyncstream(std::cout) << "TrackSize overflow!!!\n";
				track.clear();
				continue;
			}

			constexpr std::uint32_t split_edge = 0x7F000000;
			if (track.size() / 2 > split_edge)
			{
				std::int64_t total_shift = split_edge;
				std::int64_t prev_edge = 0;
				midi_track_iterator dti(track);
				front_edge.clear();

				while (dti.advance_to_position(total_shift))
				{
					total_shift = dti.cur_position + split_edge;
					back_edge.clear();
					dti.emit_held_notes(back_edge, false);

					std::uint64_t total_size =
						front_edge.size() +
						(dti.cur_position - prev_edge) +
						back_edge.size() + 4;

					out << "MTrk";
					out.put((char)(total_size >> 24));
					out.put((char)(total_size >> 16));
					out.put((char)(total_size >> 8));
					out.put((char)total_size);

					single_midi_processor_2::ostream_write(front_edge, out);
					single_midi_processor_2::ostream_write(track,
						track.begin() + prev_edge, track.begin() + dti.cur_position, out);
					single_midi_processor_2::ostream_write(back_edge,
						back_edge.begin(), back_edge.end(), out);

					out.put(0);
					out.put((char)0xFF);
					out.put((char)0x2F);
					out.put(0);
					++track_count;

					prev_edge = dti.cur_position;
					front_edge.clear();
					dti.emit_held_notes(front_edge, true);
				}

				std::uint64_t total_size = front_edge.size() + (dti.cur_position - prev_edge);
				out << "MTrk";
				out.put((char)(total_size >> 24));
				out.put((char)(total_size >> 16));
				out.put((char)(total_size >> 8));
				out.put((char)total_size);

				single_midi_processor_2::ostream_write(front_edge, out);
				single_midi_processor_2::ostream_write(track,
					track.begin() + prev_edge, track.end(), out);

				front_edge.clear();
				back_edge.clear();
				++track_count;
			}
			else
			{
				out << "MTrk";
				out.put((char)(track.size() >> 24));
				out.put((char)(track.size() >> 16));
				out.put((char)(track.size() >> 8));
				out.put((char)track.size());
				single_midi_processor_2::ostream_write(track, out);
				++track_count;
			}

			track.clear();
		}

		for (std::size_t i = 0; i < streams.size(); i++)
		{
			streams[i]->close();
			if (candidates[i].first->settings.proc_details.remove_remnants)
				_wremove((candidates[i].first->filename + candidates[i].first->postfix).c_str());
		}

		out.seekp(10, std::ios::beg);
		out.put((char)(track_count >> 8));
		out.put((char)(track_count & 0xFF));
		out.close();

		std::osyncstream(std::cout) << "Inplace: finished\n";
		return track_count;
	}

	static std::uint64_t do_regular_merge_impl(
		const std::vector<std::pair<proc_data_ptr, message_buffer_ptr>>& candidates,
		std::uint16_t ppqn,
		const std::wstring& save_to)
	{
		if (candidates.empty())
			return 0;

		std::uint64_t track_count = 0;
		auto save_path = save_to + L".R.mid";

		if (candidates.size() == 1)
		{
			auto& pd = *candidates.front().first;
			std::wstring src = pd.filename + pd.postfix;
			_wremove(save_path.c_str());
			_wrename(src.c_str(), save_path.c_str());
			return pd.tracks_count.load();
		}

		constexpr std::size_t buffer_size = 20'000'000;
		auto buffer = std::make_unique<std::uint8_t[]>(buffer_size);

		std::ofstream out(save_path, std::ios::binary | std::ios::out);
		out.rdbuf()->pubsetbuf((char*)buffer.get(), buffer_size);

		out << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
		out.put(0); out.put(0);
		out.put((char)(ppqn >> 8));
		out.put((char)ppqn);

		bbb_ffr file_input(
			(candidates.front().first->filename + candidates.front().first->postfix).c_str());

		for (auto it = candidates.begin(); it != candidates.end(); ++it)
		{
			auto& pd = *it->first;
			std::wstring src = pd.filename + pd.postfix;
			if (it != candidates.begin())
				file_input.reopen_next_file(src.c_str());
			for (int i = 0; i < 14; i++)
				file_input.get();
			file_input.put_into_ostream(out);
			track_count += pd.tracks_count;
			if (pd.settings.proc_details.remove_remnants)
				_wremove(src.c_str());
		}

		out.seekp(10, std::ios::beg);
		out.put((char)(track_count >> 8));
		out.put((char)track_count);
		out.flush();
		out.close();

		return track_count;
	}

	static void do_final_merge_impl(
		const std::wstring& save_to,
		std::uint64_t ii_count,
		std::uint64_t ir_count,
		bool remove_remnants)
	{
		auto inplace_path = save_to + L".I.mid";
		auto regular_path = save_to + L".R.mid";

		auto im = std::make_unique<bbb_ffr>(inplace_path.c_str());
		auto rm = std::make_unique<bbb_ffr>(regular_path.c_str());

		bool im_good = !im->eof();
		bool rm_good = !rm->eof();

		if (!im_good || !rm_good)
		{
			im->close();
			rm->close();

			auto r_del = _wremove(save_to.c_str());
			auto r_i   = _wrename(inplace_path.c_str(), save_to.c_str());
			auto r_r   = _wrename(regular_path.c_str(), save_to.c_str()); // one of these will not work

			std::osyncstream(std::cout)
				<< "S2 status: " << r_del
				<< "\nI status: " << r_i
				<< "\nR status: " << r_r << '\n';
			std::osyncstream(std::cout) << "Escaped last stage\n";
			return;
		}

		std::osyncstream(std::cout) << "Active merging at last stage (untested)\n";

		auto total_tracks = ii_count + ir_count;
		if (total_tracks & ~0xFFFFULL)
		{
			std::osyncstream(std::cout) << "Track count overflow: " << total_tracks << '\n';
			total_tracks = 0xFFFF;
		}

		std::ofstream out(save_to, std::ios::binary | std::ios::out);
		out << "MThd";
		out.put(0); out.put(0); out.put(0); out.put(6);
		out.put(0); out.put(1);

		for (int i = 0; i < 12; i++) im->get();
		for (int i = 0; i < 12; i++) rm->get();

		out.put((char)(total_tracks >> 8));
		out.put((char)(total_tracks & 0xFF));

		im->get(); im->get(); // skip inplace timing
		out.put((char)rm->get()); // use regular timing
		out.put((char)rm->get());

		im->put_into_ostream(out);
		rm->put_into_ostream(out);

		im->close();
		rm->close();

		if (remove_remnants)
		{
			_wremove(inplace_path.c_str());
			_wremove(regular_path.c_str());
		}
	}
};

#endif
