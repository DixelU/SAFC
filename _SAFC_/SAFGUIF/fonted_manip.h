#pragma once
#ifndef SAFGUIF_FM
#define SAFGUIF_FM

#include <iostream>

#include "../WinRegWrappers.h"
#include "header_utils.h"

inline void set_is_fonted_var(bool val)
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
#endif
