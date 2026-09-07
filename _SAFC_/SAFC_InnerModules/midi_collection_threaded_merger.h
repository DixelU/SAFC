#pragma once
#ifndef SAF_MCTM
#define SAF_MCTM

void throw_alert_error(std::string&& AlertText);
void throw_alert_warning(std::string&& AlertText);

#include <set>
#include <atomic>
#include <exception>
#include <cerrno>
#include <system_error>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <syncstream>

#include "midi_file_reader.h"

#include "single_midi_processor_2.h"

struct midi_track_iterator
{
	const std::uint8_t* track_data;
	std::uint64_t track_size;
	std::int64_t cur_position = 0;
	std::int64_t cur_tick = 0;
	std::array<std::int64_t, 4096> held{};
	bool processing = true;
	const std::atomic_bool* cancellation = nullptr;

	explicit midi_track_iterator(const std::vector<std::uint8_t>& vec, const std::atomic_bool* cancel = nullptr) :
		track_data(vec.data()),
		track_size(vec.size()), cancellation(cancel)
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
		if (cancellation && cancellation->load(std::memory_order_relaxed)) throw midi_processing_cancelled{};
		if (!processing || cur_position >= (std::int64_t)track_size)
		{
			processing = false;
			return;
		}

		std::uint32_t delta_time = 0, last_byte = 0;
		do
		{
			last_byte = track_data[cur_position++];
			delta_time = (delta_time << 7) | (last_byte & 0x7F);
		} while (last_byte & 0x80);

		cur_tick += delta_time;
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
				std::uint32_t size = 0, last_byte = 0;
				cur_position++;

				do
				{
					last_byte = track_data[cur_position++];
					size = (size << 7) | (last_byte & 0x7F);
				} while (last_byte & 0x80);

				for (std::uint32_t index = 0; index < size; index++)
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
				if (cancellation && cancellation->load(std::memory_order_relaxed)) throw midi_processing_cancelled{};
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

struct midi_collection_threaded_merger :
	std::enable_shared_from_this<midi_collection_threaded_merger>
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

	~midi_collection_threaded_merger()
	{
		request_cancel();
		wait_processing();
		if (inplace_merge_future_.valid()) inplace_merge_future_.wait();
		if (regular_merge_future_.valid()) regular_merge_future_.wait();
		if (final_merge_future_.valid()) final_merge_future_.wait();
	}

	void request_cancel() noexcept
	{
		cancellation_requested_.store(true, std::memory_order_release);
		for (auto& [_, buffers] : midi_processing_data_)
			buffers->cancel_requested.store(true, std::memory_order_release);
	}

	bool cancelled() const noexcept
	{
		return cancellation_requested_.load(std::memory_order_acquire);
	}

	void wait_processing()
	{
		for (auto& worker : processing_workers_)
			if (worker.joinable()) worker.join();
		processing_workers_.clear();
	}

	void start_processing() noexcept
	{
		if (cancelled()) { mark_all_processing_finished(); return; }
		try
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
				processing_workers_.emplace_back([this](
				std::vector<std::pair<proc_data_ptr, message_buffer_ptr>> data,
				std::uint32_t id,
				std::uint32_t idx)
			{
				try
				{
					for (auto& el : data)
					{
						el.second->check_cancelled();
						if (el.first->settings.details.group_id != id)
							continue;

						{
							std::lock_guard lock(currently_processed_mutex_);
							currently_processed_[idx] = el;
						}

						if (single_midi_processor_lean::can_handle(el.first->settings))
							single_midi_processor_lean::sync_processing(*el.first, *el.second);
						else if (el.first->settings.proc_details.channel_split)
							single_midi_processor_2::sync_processing<true>(*el.first, *el.second);
						else
							single_midi_processor_2::sync_processing<false>(*el.first, *el.second);
					}
				}
				catch (...)
				{
					record_failure("MIDI processing", std::current_exception());
					for (auto& el : data)
					{
						if (el.first->settings.details.group_id == id)
						{
							el.second->processing.store(false, std::memory_order_release);
							el.second->finished.store(true, std::memory_order_release);
						}
					}
				}
				}, midi_processing_data_, group_id, thread_index);
				++thread_index;
			}
		}
		catch (...)
		{
			record_failure("MIDI processing startup", std::current_exception());
			mark_all_processing_finished();
		}
	}

	bool is_smrp_complete() const
	{
		if (has_failed() || cancelled())
			return true;
		for (auto& [_, mbuf] : midi_processing_data_)
			if (!mbuf->finished || mbuf->processing)
				return false;
		return true;
	}

	void start_ri_merge() noexcept
	{
		if (has_failed() || cancelled())
		{
			inplace_merge_complete.store(true, std::memory_order_release);
			regular_merge_complete.store(true, std::memory_order_release);
			return;
		}

		bool inplace_started = false;
		try
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
				try
				{
					auto count = do_inplace_merge_impl(candidates, final_ppqn_, save_to_);
					inplace_track_count.store(count, std::memory_order_release);
					inplace_merge_complete.store(true, std::memory_order_release);
					return count;
				}
				catch (...)
				{
					record_failure("in-place merge", std::current_exception());
					inplace_merge_complete.store(true, std::memory_order_release);
					return 0;
				}
			});
			inplace_started = true;

			regular_merge_future_ = std::async(std::launch::async,
			[this, candidates = std::move(regular_candidates)]() -> std::uint64_t
			{
				try
				{
					auto count = do_regular_merge_impl(candidates, final_ppqn_, save_to_);
					regular_track_count.store(count, std::memory_order_release);
					regular_merge_complete.store(true, std::memory_order_release);
					return count;
				}
				catch (...)
				{
					record_failure("regular merge", std::current_exception());
					regular_merge_complete.store(true, std::memory_order_release);
					return 0;
				}
			});
		}
		catch (...)
		{
			record_failure("merge startup", std::current_exception());
			if (!inplace_started)
				inplace_merge_complete.store(true, std::memory_order_release);
			regular_merge_complete.store(true, std::memory_order_release);
		}
	}

	bool is_ri_merge_complete() const
	{
		return inplace_merge_complete.load() && regular_merge_complete.load();
	}

	void start_final_merge() noexcept
	{
		if (cancelled()) { complete.store(true, std::memory_order_release); return; }
		try
		{
			if (has_failed() && (!inplace_merge_future_.valid() || !regular_merge_future_.valid()))
			{
				complete.store(true, std::memory_order_release);
				return;
			}

			auto ii_count = inplace_merge_future_.get();
			auto ir_count = regular_merge_future_.get();
			if (has_failed())
			{
				complete.store(true, std::memory_order_release);
				return;
			}

			final_merge_future_ = std::async(std::launch::async,
			[this, ii_count, ir_count]()
			{
				try
				{
					do_final_merge_impl(save_to_, ii_count, ir_count, remnants_remove_);
				}
				catch (...)
				{
					record_failure("final merge", std::current_exception());
				}
				complete.store(true, std::memory_order_release);
			});
		}
		catch (...)
		{
			record_failure("final merge startup", std::current_exception());
			complete.store(true, std::memory_order_release);
		}
	}

	bool has_failed() const noexcept
	{
		return failure_recorded_.load(std::memory_order_acquire);
	}

	std::string failure_message() const
	{
		std::lock_guard lock(failure_mutex_);
		return failure_message_.empty() ? "MIDI merge failed" : failure_message_;
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
	std::vector<std::thread> processing_workers_;
	std::atomic_bool failure_recorded_{ false };
	std::atomic_bool cancellation_requested_{ false };
	mutable std::mutex failure_mutex_;
	std::string failure_message_;

	void mark_all_processing_finished() noexcept
	{
		for (auto& [_, buffers] : midi_processing_data_)
		{
			buffers->processing.store(false, std::memory_order_release);
			buffers->finished.store(true, std::memory_order_release);
		}
	}

	void record_failure(const char* stage, std::exception_ptr exception) noexcept
	{
		if (cancelled()) return;
		try
		{
			std::string detail = stage;
			detail += " failed";
			try
			{
				std::rethrow_exception(exception);
			}
			catch (const std::exception& error)
			{
				detail += ": ";
				detail += error.what();
			}
			catch (...)
			{
				detail += " with an unknown error";
			}

			std::lock_guard lock(failure_mutex_);
			if (!failure_recorded_.load(std::memory_order_relaxed))
			{
				failure_message_ = std::move(detail);
				failure_recorded_.store(true, std::memory_order_release);
			}
		}
		catch (...)
		{
			failure_recorded_.store(true, std::memory_order_release);
		}
	}

	static void replace_intermediate(const std::wstring& source, const std::wstring& destination)
	{
		if (_wremove(destination.c_str()) != 0 && errno != ENOENT)
			throw std::system_error(errno, std::generic_category(), "Cannot replace intermediate MIDI output");
		if (_wrename(source.c_str(), destination.c_str()) != 0)
			throw std::system_error(errno, std::generic_category(), "Cannot move intermediate MIDI output");
	}

	void check_cancelled() const
	{
		if (cancelled()) throw midi_processing_cancelled{};
	}

	static std::uint32_t read_vlv(midi_file_reader& f)
	{
		std::uint32_t result = 0;
		std::uint8_t b;
		do
		{
			b = read_midi_byte(f);
			result = (result << 7) | (b & 0x7F);
		} while (b & 0x80);
		return result;
	}

	std::uint64_t do_inplace_merge_impl(
		const std::vector<std::pair<proc_data_ptr, message_buffer_ptr>>& candidates,
		std::uint16_t ppqn,
		const std::wstring& save_to)
	{
		if (candidates.empty())
			return 0;

		std::uint64_t track_count = 0;

		std::vector<std::unique_ptr<midi_file_reader>> streams;
		streams.reserve(candidates.size());
		for (auto& [pdata, _] : candidates)
		{
			auto& s = streams.emplace_back(std::make_unique<midi_file_reader>(
				pdata->output_path()));
			s->set_cancellation(&cancellation_requested_);
			if (!s->is_open() || s->size() < 14) throw std::runtime_error("Cannot read processed MIDI for in-place merge");
			for (int i = 0; i < 14; i++)
				static_cast<void>(read_midi_byte(*s));
		}

		std::ofstream out(save_to + L".I.mid", std::ios::binary | std::ios::out);
		out.exceptions(std::ios::failbit | std::ios::badbit);
		out << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
		out.put(0); out.put(0); // track count placeholder, updated at the end
		out.put((char)(ppqn >> 8));
		out.put((char)ppqn);

		std::vector<std::uint8_t> track, front_edge, back_edge;
		track.reserve(1'000'000);
		std::vector<std::int64_t> delta_times(streams.size(), -1);

		bool active_stream = true;
		while (active_stream)
		{
			check_cancelled();
			for (std::size_t i = 0; i < streams.size(); i++)
			{
				auto& reader = *streams[i];

				std::uint32_t header = 0;
				while (reader.good() && header != single_midi_processor_2::MTrk_header)
					header = (header << 8) | read_midi_byte(reader);

				for (int w = 0; w < 4; w++)
					static_cast<void>(read_midi_byte(reader)); // skip track size

				if (reader.good())
				{
					delta_times[i] = (std::int64_t)read_vlv(reader);
					if (reader.eof())
						delta_times[i] = -1;
				}
				else
					delta_times[i] = -1;
			}

			bool active_track = true;
			std::uint64_t in_track_delta = 0;

			while (active_track)
			{
				check_cancelled();
				active_track = false;
				active_stream = false;

				for (std::size_t i = 0; i < streams.size(); i++)
				{
					auto& reader = *streams[i];
					auto& delta_time = delta_times[i];

					if (delta_time < 0)
					{
						if (reader.good())
							active_stream = true;
						continue;
					}

					while (delta_time == 0)
					{
						check_cancelled();
						std::uint8_t event_type = read_midi_byte(reader);
						auto delta_len = single_midi_processor_2::push_vlv(in_track_delta, track);
						in_track_delta = 0;

						bool track_ended = false;
						if (event_type == 0xFF)
						{
							std::uint8_t meta_type = read_midi_byte(reader);
							if (meta_type == 0x2F)
							{
								static_cast<void>(read_midi_byte(reader)); // skip 0x00 length byte
								for (int l = 0; l < (int)delta_len; l++)
									track.pop_back();
								track_ended = true;
							}
							else
							{
								track.push_back(event_type);
								track.push_back(meta_type);
								std::uint32_t meta_len = 0;
								std::uint8_t last_byte;
								do
								{
									last_byte = read_midi_byte(reader);
									track.push_back(last_byte);
									meta_len = (meta_len << 7) | (last_byte & 0x7F);
								} while (last_byte & 0x80);

								for (std::uint32_t j = 0; j < meta_len; j++)
									track.push_back(read_midi_byte(reader));
							}
						}
						else if (event_type == 0xF0 || event_type == 0xF7)
						{
							track.push_back(event_type);
							std::uint32_t sysex_len = 0;
							std::uint8_t last_byte;

							do
							{
								last_byte = read_midi_byte(reader);
								track.push_back(last_byte);
								sysex_len = (sysex_len << 7) | (last_byte & 0x7F);
							} while (last_byte & 0x80);

							for (std::uint32_t j = 0; j < sysex_len; j++)
								track.push_back(read_midi_byte(reader));
						}
						else if ((event_type >= 0x80 && event_type <= 0xBF) ||
						         (event_type >= 0xE0 && event_type <= 0xEF))
						{
							track.push_back(event_type);
							track.push_back(read_midi_byte(reader));
							track.push_back(read_midi_byte(reader));
						}
						else if (event_type >= 0xC0 && event_type <= 0xDF)
						{
							track.push_back(event_type);
							track.push_back(read_midi_byte(reader));
						}
						else
						{
							auto pos = reader.position();
							throw_alert_error("DTI Failure at " + std::to_string(pos) + ". Type: " +
								std::to_string(event_type) + ". Tell developer about it and give him source midi.\n");
							track.push_back(0xCA);
							track.push_back(0);
							track_ended = true;
						}

						if (track_ended)
						{
							delta_time = -1;
							if (reader.good())
								active_stream = true;
							break;
						}

						auto next_delta = read_vlv(reader);
						if (reader.eof())
						{
							delta_time = -1;
							if (reader.good())
								active_stream = true;
							break;
						}
						delta_time = (std::int64_t)next_delta;
						if (delta_time > 0)
						{
							active_track = true;
							break;
						}
						// delta_time == 0: loop and process next event immediately
					}

					if (delta_time > 0)
					{
						active_track = true;
						delta_time--;
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
				midi_track_iterator dti(track, &cancellation_requested_);
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

					single_midi_processor_2::ostream_write(front_edge, out, &cancellation_requested_);
					single_midi_processor_2::ostream_write(track,
						track.begin() + prev_edge, track.begin() + dti.cur_position, out, &cancellation_requested_);
					single_midi_processor_2::ostream_write(back_edge,
						back_edge.begin(), back_edge.end(), out, &cancellation_requested_);

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

				single_midi_processor_2::ostream_write(front_edge, out, &cancellation_requested_);
				single_midi_processor_2::ostream_write(track,
					track.begin() + prev_edge, track.end(), out, &cancellation_requested_);

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
				single_midi_processor_2::ostream_write(track, out, &cancellation_requested_);
				++track_count;
			}

			track.clear();
		}

		for (std::size_t i = 0; i < streams.size(); i++)
		{
			if (streams[i]->failed()) throw std::runtime_error("In-place merge input read failed");
			streams[i]->close();
			if (candidates[i].first->settings.proc_details.remove_remnants)
				_wremove((candidates[i].first->output_path()).c_str());
		}

		out.seekp(10, std::ios::beg);
		out.put((char)(track_count >> 8));
		out.put((char)(track_count & 0xFF));
		out.flush();
		out.close();

		std::osyncstream(std::cout) << "Inplace: finished\n";
		return track_count;
	}

	std::uint64_t do_regular_merge_impl(
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
			std::wstring src = pd.output_path();
			check_cancelled();
			if (pd.settings.proc_details.remove_remnants)
				replace_intermediate(src, save_path);
			else
			{
				midi_file_reader retained_input(src);
				retained_input.set_cancellation(&cancellation_requested_);
				if (!retained_input.is_open() || retained_input.size() < 14)
					throw std::runtime_error("Cannot read retained processed MIDI");
				std::ofstream copied_output(save_path, std::ios::binary | std::ios::out);
				copied_output.exceptions(std::ios::failbit | std::ios::badbit);
				retained_input.copy_to(copied_output);
				if (retained_input.failed()) throw std::runtime_error("Retained processed MIDI read failed");
				copied_output.flush();
				copied_output.close();
			}
			return pd.tracks_count.load();
		}

		constexpr std::size_t buffer_size = 20'000'000;
		auto buffer = std::make_unique<std::uint8_t[]>(buffer_size);

		std::ofstream out(save_path, std::ios::binary | std::ios::out);
		out.exceptions(std::ios::failbit | std::ios::badbit);
		out.rdbuf()->pubsetbuf((char*)buffer.get(), buffer_size);

		out << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
		out.put(0); out.put(0);
		out.put((char)(ppqn >> 8));
		out.put((char)ppqn);

		midi_file_reader file_input(
			candidates.front().first->output_path());
		file_input.set_cancellation(&cancellation_requested_);

		for (auto it = candidates.begin(); it != candidates.end(); ++it)
		{
			check_cancelled();
			auto& pd = *it->first;
			std::wstring src = pd.output_path();
			if (it != candidates.begin())
				file_input.reopen(src);
			if (!file_input.is_open() || file_input.size() < 14) throw std::runtime_error("Cannot read processed MIDI for regular merge");
			for (int i = 0; i < 14; i++)
				static_cast<void>(read_midi_byte(file_input));
			file_input.copy_to(out);
			if (file_input.failed()) throw std::runtime_error("Regular merge input read failed");
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

	void do_final_merge_impl(
		const std::wstring& save_to,
		std::uint64_t ii_count,
		std::uint64_t ir_count,
		bool remove_remnants)
	{
		auto inplace_path = save_to + L".I.mid";
		auto regular_path = save_to + L".R.mid";

		auto im = std::make_unique<midi_file_reader>(inplace_path);
		auto rm = std::make_unique<midi_file_reader>(regular_path);
		im->set_cancellation(&cancellation_requested_);
		rm->set_cancellation(&cancellation_requested_);
		check_cancelled();

		// A missing intermediate file is not at EOF: it was never opened.
		// Require readable input before copying its header, including the PPQ.
		bool im_good = im->good();
		bool rm_good = rm->good();

		if ((ii_count && !im_good) || (ir_count && !rm_good) || (!im_good && !rm_good))
			throw std::runtime_error("A completed merge stage has no readable MIDI output");
		if (!im_good || !rm_good)
		{
			im->close();
			rm->close();
			check_cancelled();
			replace_intermediate(im_good ? inplace_path : regular_path, save_to);
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
		out.exceptions(std::ios::failbit | std::ios::badbit);
		out << "MThd";
		out.put(0); out.put(0); out.put(0); out.put(6);
		out.put(0); out.put(1);

		for (int i = 0; i < 12; i++) static_cast<void>(read_midi_byte(*im));
		for (int i = 0; i < 12; i++) static_cast<void>(read_midi_byte(*rm));

		out.put((char)(total_tracks >> 8));
		out.put((char)(total_tracks & 0xFF));

		static_cast<void>(read_midi_byte(*im));
		static_cast<void>(read_midi_byte(*im)); // skip inplace timing
		out.put(static_cast<char>(read_midi_byte(*rm))); // use regular timing
		out.put(static_cast<char>(read_midi_byte(*rm)));

		im->copy_to(out);
		rm->copy_to(out);
		if (im->failed() || rm->failed()) throw std::runtime_error("Final merge input read failed");
		out.flush();
		out.close();

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
