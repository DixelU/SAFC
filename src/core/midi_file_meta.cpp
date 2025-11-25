#include "midi_file_meta.h"

#include <cmath>

#include "fast_midi_checker.h"
#include "../globals.h"

bool midi_file_meta::set(std_unicode_string file)
{
	fast_midi_checker checker(file);
	if (!checker.is_accessible || !checker.is_midi)
		return false;

	this->file = std::move(file);
	this->visible_path = std::string(this->file.begin(), this->file.end());

	size_t slash = this->visible_path.find_last_of("/\\");
	this->visible_name = (slash == std::string::npos) ? this->visible_path : this->visible_path.substr(slash + 1);

	this->processing_data = std::make_shared<single_midi_processor_2::processing_data>();

	processing_data->filename = this->file;
	processing_data->tracks_count = checker.expected_track_number;
	processing_data->visible_filename = this->visible_name;
	processing_data->postfix = to_cchar_t("_sp.mid").operator std_unicode_string(); // no postfix by default

	auto& settings = processing_data->settings;
	settings.old_ppqn = checker.ppqn;
	settings.new_ppqn = safc_globals::global_ppq;

	// apply current global settings

	settings.filter.pass_notes = !safc_globals::cb_ignore_note_events;
	settings.filter.pass_tempo = !safc_globals::cb_ignore_tempo_events;
	settings.filter.pass_pitch = !safc_globals::cb_ignore_pitchbend_events;
	settings.filter.pass_sysex = safc_globals::cb_allow_sysex_events;
	settings.filter.piano_only = !safc_globals::cb_ignore_all_instruments_2_piano;
	settings.filter.pass_other = !safc_globals::cb_ignore_except_everythig_spec;

	settings.proc_details.remove_remnants = safc_globals::cb_rm_merge_remnants;
	settings.proc_details.remove_empty_tracks = safc_globals::cb_rm_empty_tracks;
	settings.proc_details.channel_split = safc_globals::cb_multichannel_split;
	settings.proc_details.whole_midi_collapse = safc_globals::cb_collapse_trk_into_1;
	settings.proc_details.apply_offset_after = safc_globals::cb_apply_offset_after_ppq_change;
	settings.legacy.rsb_compression = safc_globals::cb_rsb_compression;
	settings.legacy.enable_zero_velocity = false; // is prohibited by standard -> only maual override
	settings.legacy.ignore_meta_rsb = false;

	settings.details.inplace_mergable = safc_globals::cb_implace_merge && !safc_globals::cb_rsb_compression;
	settings.details.initial_filesize = checker.file_size;
	settings.offset = 0;
	settings.thread_id = 0;

	return true;
}

void subdivide_into_equal_groups(std::deque<midi_file_meta>& midis, uint32_t threads_count)
{
	struct _SFD_RSP
	{
		std::uint32_t idx;
		std::uint64_t size;

		_SFD_RSP(std::uint32_t idx, std::uint64_t size): 
			idx(idx), size(size) {}

		inline bool operator<(const _SFD_RSP& a) const { return size < a.size || (size == a.size && idx < a.idx); }
	};

	if (midis.size() < 2)
		return;

	std::vector<_SFD_RSP> sizes;
	std::vector<std::uint64_t> total_size;
	std::uint64_t accumulator = 0;

	for (int i = 0; i < midis.size(); i++)
		sizes.emplace_back(i, midis[i].processing_data->settings.details.initial_filesize);

	std::sort(sizes.begin(), sizes.end());

	for (int i = 0; i < sizes.size(); i++)
		total_size.push_back((accumulator += sizes[i].size));

	for (int i = 0; i < total_size.size(); i++)
	{
		// basically rounds up fraction of cumulative midis size to total size up to this midi
		// and assigns a group id just to that times number of threads // bruhst // legacy safc code but works good
		midis[sizes[i].idx].processing_data->settings.details.group_id = 
			(std::uint16_t)(std::ceil(((float)total_size[i] / ((float)total_size.back())) * threads_count) - 1.f);
	}
}