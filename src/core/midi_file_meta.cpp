#include "midi_file_meta.h"

void midi_file_meta::set(std_unicode_string file)
{
	this->file = std::move(file);
	this->visible_path = std::string(this->file.begin(), this->file.end());

	size_t slash = this->visible_path.find_last_of("/\\");
	this->visible_name = (slash == std::string::npos) ? this->visible_path : this->visible_path.substr(slash + 1);
}
