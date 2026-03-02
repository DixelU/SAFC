#pragma once
#ifndef SAFGUIF_FM
#define SAFGUIF_FM

#include <iostream>

#include "../WinRegWrappers.h"
#include "header_utils.h"

bool restore_is_fonted_var()
{
	bool rk_op = false;
	WinReg::RegKey rk;
	try
	{
		rk.Open(HKEY_CURRENT_USER, default_reg_path);
		rk_op = true;
	}
	catch (...)
	{
		std::cout << "RK opening failed\n";
	}

	if (rk_op)
	{
		try
		{
			is_fonted = rk.GetDwordValue(L"FONTS_ENABLED_POST1P4");
		}
		catch (...) { std::cout << "Exception thrown while restoring FONTS_ENABLED from registry\n"; }
	}

	rk.Close();

	return false;
}

void set_is_fonted_var(bool val)
{
	bool rk_op = false;
	WinReg::RegKey rk;
	try
	{
		rk.Open(HKEY_CURRENT_USER, default_reg_path);
		rk_op = true;
	}
	catch (...)
	{
		std::cout << "RK opening failed\n";
	}
	if (rk_op)
	{
		try
		{
			rk.SetDwordValue(L"FONTS_ENABLED_POST1P4", val);
		}
		catch (...) { std::cout << "Exception thrown while saving FONTS_ENABLED from registry\n"; }
	}

	rk.Close();
}

// Legacy names
inline bool RestoreIsFontedVar() { return restore_is_fonted_var(); }
inline void SetIsFontedVar(bool val) { set_is_fonted_var(val); }

bool _______unused = restore_is_fonted_var();

#endif
