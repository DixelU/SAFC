#pragma once
#ifndef SAFGUIF_FM
#define SAFGUIF_FM

#include <iostream>
#include "../WinReg.h"
#include "header_utils.h"

BIT RestoreIsFontedVar() {
	bool RK_OP = false;
	WinReg::RegKey RK;
	try {
		RK.Open(HKEY_CURRENT_USER, RegPath);
		RK_OP = true;
	}
	catch (...) {
		std::cout << "RK opening failed\n";
	}
	if (RK_OP) {
		try {
			is_fonted = RK.GetDwordValue(L"FONTS_ENABLED");
		}
		catch (...) { std::cout << "Exception thrown while restoring FONTS_ENABLED from registry\n"; }
	}
	if (RK_OP)
		RK.Close();
	return false;
}
void SetIsFontedVar(BIT VAL) {
	bool RK_OP = false;
	WinReg::RegKey RK;
	try {
		RK.Open(HKEY_CURRENT_USER, RegPath);
		RK_OP = true;
	}
	catch (...) {
		std::cout << "RK opening failed\n";
	}
	if (RK_OP) {
		try {
			RK.SetDwordValue(L"FONTS_ENABLED", VAL);
		}
		catch (...) { std::cout << "Exception thrown while saving FONTS_ENABLED from registry\n"; }
	}
	if (RK_OP)
		RK.Close();
}
BIT _______unused = RestoreIsFontedVar();

#endif 