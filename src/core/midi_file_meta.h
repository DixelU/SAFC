#pragma once

#include "header_utils.h"

struct midi_file_meta
{
	std_unicode_string file;
	std::string visible_path;
	std::string visible_name;

	void set(std_unicode_string file);

	// whatever, probably a bunch of settings
};

