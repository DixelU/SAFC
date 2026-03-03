#pragma once
#ifndef SAFGUIF_CHARMAP
#define SAFGUIF_CHARMAP

#include <unordered_map>
#include <fstream>

#include "header_utils.h"

std::unordered_map<char, std::string> legacy_draw_map;
void init_legacy_draw_map()
{
	legacy_draw_map.clear();

	if (true)
	{
		legacy_draw_map['!'] = "85 2";
		legacy_draw_map['"'] = "74 96";
		legacy_draw_map['#'] = "81 92 64";
		legacy_draw_map['$'] = "974631 82";
		legacy_draw_map['%'] = "7487 3623 91";
		legacy_draw_map['&'] = "37954126";
		legacy_draw_map['\''] = "85";
		legacy_draw_map['('] = "842~";
		legacy_draw_map[')'] = "862~";
		legacy_draw_map['*'] = "67 58 94~";
		legacy_draw_map['+'] = "46 82";
		legacy_draw_map[','] = "15~";
		legacy_draw_map['-'] = "46";
		legacy_draw_map['.'] = "2";
		legacy_draw_map['/'] = "81";
		legacy_draw_map['0'] = "97139";
		legacy_draw_map['1'] = "482 13";
		legacy_draw_map['2'] = "796413";
		legacy_draw_map['3'] = "7965 631";
		legacy_draw_map['4'] = "746 93";
		legacy_draw_map['5'] = "974631";
		legacy_draw_map['6'] = "9741364";
		legacy_draw_map['7'] = "7952";
		legacy_draw_map['8'] = "17931 46";
		legacy_draw_map['9'] = "1369746";
		legacy_draw_map[':'] = "8 5~";
		legacy_draw_map[';'] = "8 15~";
		legacy_draw_map['<'] = "943";
		legacy_draw_map['='] = "46 79~";
		legacy_draw_map['>'] = "761";
		legacy_draw_map['?'] = "795 2";
		legacy_draw_map['@'] = "317962486";
		legacy_draw_map['A'] = "1793 46";
		legacy_draw_map['B'] = "17954 531";
		legacy_draw_map['C'] = "9713";
		legacy_draw_map['D'] = "178621";
		legacy_draw_map['E'] = "9713 54";
		legacy_draw_map['F'] = "971 54";
		legacy_draw_map['G'] = "971365";
		legacy_draw_map['H'] = "9641 74 63";
		legacy_draw_map['I'] = "79 82 13";
		legacy_draw_map['J'] = "79 821";
		legacy_draw_map['K'] = "71 954 53";
		legacy_draw_map['L'] = "713";
		legacy_draw_map['M'] = "17593";
		legacy_draw_map['N'] = "1739";
		legacy_draw_map['O'] = "97139";
		legacy_draw_map['P'] = "17964";
		legacy_draw_map['Q'] = "179621 53";
		legacy_draw_map['R'] = "17964 53";
		legacy_draw_map['S'] = "974631";
		legacy_draw_map['T'] = "79 82";
		legacy_draw_map['U'] = "712693";
		legacy_draw_map['V'] = "729";
		legacy_draw_map['W'] = "71539";
		legacy_draw_map['X'] = "73 91";
		legacy_draw_map['Y'] = "752 95";
		legacy_draw_map['Z'] = "7913 46";
		legacy_draw_map['['] = "9823~";
		legacy_draw_map['\\'] = "72";
		legacy_draw_map[']'] = "7821~";
		legacy_draw_map['^'] = "486";
		legacy_draw_map['_'] = "13";
		legacy_draw_map['`'] = "75";
		legacy_draw_map['a'] = "153";
		legacy_draw_map['b'] = "71364";
		legacy_draw_map['c'] = "6413";
		legacy_draw_map['d'] = "93146";
		legacy_draw_map['e'] = "31461";
		legacy_draw_map['f'] = "289 56";
		legacy_draw_map['g'] = "139746#";
		legacy_draw_map['h'] = "71 463";
		legacy_draw_map['i'] = "52 8";
		legacy_draw_map['j'] = "521 8";
		legacy_draw_map['k'] = "716 35";
		legacy_draw_map['l'] = "82";
		legacy_draw_map['m'] = "1452 563";
		legacy_draw_map['n'] = "1463";
		legacy_draw_map['o'] = "14631";
		legacy_draw_map['p'] = "17964#";
		legacy_draw_map['q'] = "39746#";
		legacy_draw_map['r'] = "146";
		legacy_draw_map['s'] = "6431";
		legacy_draw_map['t'] = "823 56";
		legacy_draw_map['u'] = "4136";
		legacy_draw_map['v'] = "426";
		legacy_draw_map['w'] = "4125 236";
		legacy_draw_map['x'] = "16 34";
		legacy_draw_map['y'] = "75 91#";
		legacy_draw_map['z'] = "4613";
		legacy_draw_map['{'] = "9854523~";
		legacy_draw_map['|'] = "82";
		legacy_draw_map['}'] = "7856521";
		legacy_draw_map['~'] = "4859~";
		legacy_draw_map['\177'] = "71937 97 31";

		legacy_draw_map['\200'] = "1761575651";
		legacy_draw_map['\201'] = "47964~";
		legacy_draw_map['\202'] = "17 93";

		for (int i = 0; i < 256; i++)
			if (legacy_draw_map[i].empty())
				legacy_draw_map[i].push_back(' ');
	}
	else
	{
		std::ifstream file("ascii.dotmap", std::ios::in);
		std::string T;

		for (int i = 0; i < 256; i++)
		{
			getline(file, T);
			legacy_draw_map[(char)i] = T;
		}

		file.close();
	}
}

#endif