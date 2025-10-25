#pragma once

#ifndef SAF_MIDI_FILE_META
#define SAF_MIDI_FILE_META

#include "header_utils.h"
#include "single_midi_processor_2.h"

struct midi_file_meta
{
	using processing_data_ptr_t = std::shared_ptr<single_midi_processor_2::processing_data>;

	std_unicode_string file;
	std::string visible_path;
	std::string visible_name;
	processing_data_ptr_t processing_data;
	
	midi_file_meta() = default;
	bool set(std_unicode_string file);

	// whatever, probably a bunch of settings
};

#endif // !SAF_MIDI_FILE_META