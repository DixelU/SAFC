#pragma once

#ifndef SAF_BWMR
#define SAF_BWMR

#include <vector>

#include "BS.h"
#include "../bbb_ffio.h"

struct HeaderlessTrackDataBackwardReader {
	enum class datatype {
		dtime, ox8, ox9, oxa, oxb, oxc, oxd, oxe, oxf
	};
	struct entry {
		uint8_t* begin;
		uint8_t* end;
		datatype possibility;
		entry* previous;
		entry** nexts;
		entry(datatype dt, entry* previous=nullptr) : possibility(dt), previous(previous) {
			switch (dt)	{
				case datatype::dtime:
					break;
				case datatype::ox8:
					break;
				case datatype::ox9:
					break;
				case datatype::oxa:
					break;
				case datatype::oxb:
					break;
				case datatype::oxc:
					break;
				case datatype::oxd:
					break;
				case datatype::oxe:
					break;
				case datatype::oxf:
					break;
				default:
					break;
			}
		}
	};
	static entry* parse(std::vector<uint8_t>& headerless_data) {
		entry* root;
		std::vector<entry*> current_entries;
		
		for (auto Y = headerless_data.crbegin(); Y != headerless_data.crend(); Y++) {

		}
	}
};

#endif