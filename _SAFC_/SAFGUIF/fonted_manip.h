#pragma once
#ifndef SAFGUIF_FM
#define SAFGUIF_FM

#include <iostream>
#include "../WinRegWrappers.h"
#include "header_utils.h"

bool RestoreIsFontedVar()
{
	bool RK_OP = false;
	WinReg::RegKey RK;
	try
	{
		RK.Open(HKEY_CURRENT_USER, default_reg_path);
		RK_OP = true;
	}
	catch (...)
	{
		std::cout << "RK opening failed\n";
	}

	if (RK_OP)
	{
		try
		{
			is_fonted = RK.GetDwordValue(L"FONTS_ENABLED_POST1P4");
		}
		catch (...) { std::cout << "Exception thrown while restoring FONTS_ENABLED from registry\n"; }
	}

	RK.Close();

	return false;
}

void SetIsFontedVar(bool VAL)
{
	bool RK_OP = false;
	WinReg::RegKey RK;
	try
	{
		RK.Open(HKEY_CURRENT_USER, default_reg_path);
		RK_OP = true;
	}
	catch (...)
	{
		std::cout << "RK opening failed\n";
	}
	if (RK_OP)
	{
		try
		{
			RK.SetDwordValue(L"FONTS_ENABLED_POST1P4", VAL);
		}
		catch (...) { std::cout << "Exception thrown while saving FONTS_ENABLED from registry\n"; }
	}

	RK.Close();
}
bool _______unused = RestoreIsFontedVar();

#endif 