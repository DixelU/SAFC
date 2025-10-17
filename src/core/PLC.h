#pragma once
#ifndef SAF_PLC
#define SAF_PLC

#include <map>
#include <vector>

template<typename typekey, typename typevalue>
struct polyline_converter 
{
	std::map<typekey, typevalue> conversion_map;

	polyline_converter(void) noexcept = default;
	polyline_converter(std::vector<std::pair<typekey, typevalue>> values)
	{
		for (auto& value : values)
			conversion_map[value.first] = value.second;
	}

	void insert(typekey key, typevalue value)
	{
		if (conversion_map.empty() || at(key) != value)
			conversion_map[key] = value;
	}

	typevalue at(typekey key) const
	{
		if (conversion_map.empty())
			return typevalue(key);
		
		if (conversion_map.size() == 1)
			return (*conversion_map.begin()).second;

		auto value_it = conversion_map.find(key);
		if (value_it != conversion_map.end()) 
			return value_it->second;

		auto lower_it = conversion_map.lower_bound(key);
		auto upper_it = conversion_map.upper_bound(key);

		if (upper_it == conversion_map.end())
			--upper_it;
		if (lower_it == conversion_map.end()) {
			lower_it = upper_it;
			if (lower_it != conversion_map.begin())
				--lower_it;
			else
				++upper_it;
		}
		if (upper_it == lower_it) {
			if (upper_it == conversion_map.begin())
				++upper_it;
			else --lower_it;
		}

		//           x(y1 - y0) + (x1y0 - x0y1) 
		//  y   =    --------------------------
		//                   (x1 - x0)

		auto left_top = key * (upper_it->second - lower_it->second);
		auto right_top = (upper_it->first * lower_it->second - lower_it->first * upper_it->second);
		return (left_top + right_top) / (upper_it->first - lower_it->first);
	}
};

struct byte_plc_core 
{
	std::uint8_t core[256];

	byte_plc_core(std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> converter_ptr) noexcept
	{
		for (int i = 0; i < 256; i++) 
			core[i] = converter_ptr->at(i);
	}

	inline std::uint8_t& operator[](std::uint8_t index) noexcept { return core[index]; }
};

struct _14bit_plc_core
{
	std::uint16_t core[1 << 14];
	static constexpr std::uint16_t _14bit = 1 << 14;

	_14bit_plc_core(std::shared_ptr<polyline_converter<std::uint16_t, std::uint16_t>> converter_ptr) noexcept
	{
		for (int i = 0; i < (_14bit); i++) 
		{
			core[i] = converter_ptr->at(i);
			if (core[i] >= _14bit)
				core[i] = 0x4000;
		}
	}
	
	inline std::uint16_t& operator[](std::uint16_t index) noexcept
	{
		if (index < _14bit)
			return core[index];
		else
			return core[0x2000];
	}
};


#endif