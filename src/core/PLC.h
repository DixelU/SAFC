#pragma once
#ifndef SAF_PLC
#define SAF_PLC

#include <map>
#include <vector>

template<typename typekey, typename typevalue>
struct PLC 
{
	std::map<typekey, typevalue> ConversionMap;
	PLC(void) { return; }
	PLC(std::vector<std::pair<typekey, typevalue>> V)
	{
		auto Y = V.begin();
		while (Y != V.end()) 
		{
			ConversionMap[*Y.first] = *Y.second;
			++Y;
		}
	}
	void InsertNewPoint(typekey key, typevalue value) 
	{
		if (AskForValue(key) != value || ConversionMap.empty())
			ConversionMap[key] = value;
	}
	typevalue AskForValue(typekey key) 
	{

		typename std::map<typekey, typevalue>::iterator L, U;
		if (ConversionMap.empty())return typevalue(key);
		if (ConversionMap.size() == 1)return (*ConversionMap.begin()).second;
		if ((L = ConversionMap.find(key)) != ConversionMap.end()) {
			return L->second;
		}

		L = ConversionMap.lower_bound(key);
		U = ConversionMap.upper_bound(key);

		if (U == ConversionMap.end())
			--U;
		if (L == ConversionMap.end()) {
			L = U;
			if (L != ConversionMap.begin())
				--L;
			else
				++U;
		}
		if (U == L) {
			if (U == ConversionMap.begin())
				++U;
			else --L;
		}
		//           x(y1 - y0) + (x1y0 - x0y1) 
		//  y   =    --------------------------
		//                   (x1 - x0)
		return (key * (U->second - L->second) + (U->first * L->second - L->first * U->second)) / (U->first - L->first);
	}
};

struct BYTE_PLC_Core 
{
	std::uint8_t Core[256];
	BYTE_PLC_Core(std::shared_ptr<PLC<std::uint8_t, std::uint8_t>> PLC_bb) 
	{
		for (int i = 0; i < 256; i++) {
			Core[i] = PLC_bb->AskForValue(i);
		}	
	}
	inline std::uint8_t& operator[](std::uint8_t Index) 
	{
		return Core[Index];
	}
};

struct _14BIT_PLC_Core
{
	std::uint16_t Core[1 << 14];
	_14BIT_PLC_Core(std::shared_ptr<PLC<std::uint16_t, std::uint16_t>> PLC_bb)
	{
		constexpr std::uint16_t _14bit = 1 << 14;
		for (int i = 0; i < (_14bit); i++) 
		{
			Core[i] = PLC_bb->AskForValue(i);
			if (Core[i] >= _14bit)
				Core[i] = 0x2000;
		}
	}
	inline std::uint16_t& operator[](std::uint16_t Index)
	{
		constexpr std::uint16_t _14bit = 1 << 14;
		if (Index < _14bit)
			return Core[Index];
		else
			return Core[0x2000];
	}
};


#endif