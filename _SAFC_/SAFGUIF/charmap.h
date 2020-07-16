#pragma once
#ifndef SAFGUIF_CHARMAP
#define SAFGUIF_CHARMAP

#include "header_utils.h"
#include <unordered_map>
#include <fstream>

std::unordered_map<char, std::string> ASCII;
void InitASCIIMap() {
	ASCII.clear();
	std::ifstream file("ascii.dotmap", std::ios::in);
	std::string T;
	if (true) {
		for (int i = 0; i <= 32; i++)ASCII[i] = " ";
		ASCII['!'] = "85 2";
		ASCII['"'] = "74 96";
		ASCII['#'] = "81 92 64";
		ASCII['$'] = "974631 82";
		ASCII['%'] = "7487 3623 91";
		ASCII['&'] = "37954126";
		ASCII['\''] = "85";
		ASCII['('] = "842~";
		ASCII[')'] = "862~";
		ASCII['*'] = "67 58 94~";
		ASCII['+'] = "46 82";
		ASCII[','] = "15~";
		ASCII['-'] = "46";
		ASCII['.'] = "2";
		ASCII['/'] = "81";
		ASCII['0'] = "97139";
		ASCII['1'] = "482 13";
		ASCII['2'] = "796413";
		ASCII['3'] = "7965 631";
		ASCII['4'] = "746 93";
		ASCII['5'] = "974631";
		ASCII['6'] = "9741364";
		ASCII['7'] = "7952";
		ASCII['8'] = "17931 46";
		ASCII['9'] = "1369746";
		ASCII[':'] = "8 5~";
		ASCII[';'] = "8 15~";
		ASCII['<'] = "943";
		ASCII['='] = "46 79~";
		ASCII['>'] = "761";
		ASCII['?'] = "795 2";
		ASCII['@'] = "317962486";
		ASCII['A'] = "1793 46";
		ASCII['B'] = "17954 531";
		ASCII['C'] = "9713";
		ASCII['D'] = "178621";
		ASCII['E'] = "9713 54";
		ASCII['F'] = "971 54";
		ASCII['G'] = "971365";
		ASCII['H'] = "9641 74 63";
		ASCII['I'] = "79 82 13";
		ASCII['J'] = "79 821";
		ASCII['K'] = "71 954 53";
		ASCII['L'] = "713";
		ASCII['M'] = "17593";
		ASCII['N'] = "1739";
		ASCII['O'] = "97139";
		ASCII['P'] = "17964";
		ASCII['Q'] = "179621 53";
		ASCII['R'] = "17964 53";
		ASCII['S'] = "974631";
		ASCII['T'] = "79 82";
		ASCII['U'] = "712693";
		ASCII['V'] = "729";
		ASCII['W'] = "71539";
		ASCII['X'] = "73 91";
		ASCII['Y'] = "752 95";
		ASCII['Z'] = "7913 46";
		ASCII['['] = "9823~";
		ASCII['\\'] = "72";
		ASCII[']'] = "7821~";
		ASCII['^'] = "486";
		ASCII['_'] = "13";
		ASCII['`'] = "75";
		ASCII['a'] = "153";
		ASCII['b'] = "71364";
		ASCII['c'] = "6413";
		ASCII['d'] = "93146";
		ASCII['e'] = "31461";
		ASCII['f'] = "289 56";
		ASCII['g'] = "139746#";
		ASCII['h'] = "71 463";
		ASCII['i'] = "52 8";
		ASCII['j'] = "521 8";
		ASCII['k'] = "716 35";
		ASCII['l'] = "82";
		ASCII['m'] = "1452 563";
		ASCII['n'] = "1463";
		ASCII['o'] = "14631";
		ASCII['p'] = "17964#";
		ASCII['q'] = "39746#";
		ASCII['r'] = "146";
		ASCII['s'] = "6431";
		ASCII['t'] = "823 56";
		ASCII['u'] = "4136";
		ASCII['v'] = "426";
		ASCII['w'] = "4125 236";
		ASCII['x'] = "16 34";
		ASCII['y'] = "75 91#";
		ASCII['z'] = "4613";
		ASCII['{'] = "9854523~";
		ASCII['|'] = "82";
		ASCII['}'] = "7856521";
		ASCII['~'] = "4859~";
		ASCII[127] = "71937 97 31";
		for (int i = 128; i < 256; i++)ASCII[i] = " ";
	}
	else {
		for (int i = 0; i < 256; i++) {
			getline(file, T);
			ASCII[(char)i] = T;
		}
	}
	file.close();
}

#endif