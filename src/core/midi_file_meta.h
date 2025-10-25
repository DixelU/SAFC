#pragma once

#ifndef SAF_MIDI_FILE_META
#define SAF_MIDI_FILE_META

#include "header_utils.h"
#include "single_midi_processor_2.h"

struct midi_file_meta
{
	std_unicode_string file;
	std::string visible_path;
	std::string visible_name;
	std::shared_ptr<single_midi_processor_2::processing_data> processing_data;
	
	midi_file_meta() = default;
	bool set(std_unicode_string file);

	// whatever, probably a bunch of settings
};

#endif // !SAF_MIDI_FILE_META