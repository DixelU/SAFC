#pragma once
#ifndef SAFGUIF_HUP
#define SAFGUIF_HUP

#include <mutex>

#include "header_utils.h"

struct handleable_ui_part
{
	std::recursive_mutex lock;
	bool enabled;

	handleable_ui_part() { enabled = true; }

	virtual ~handleable_ui_part() = default;

	[[nodiscard]] virtual bool mouse_handler(float mx, float my, char button/*-1 left, 1 right*/, char state /*-1 down, 1 up*/) = 0;
	
	virtual void draw() = 0;
	virtual void safe_move(float, float) = 0;
	virtual void safe_change_position(float, float) = 0;
	virtual void safe_change_position_argumented(std::uint8_t, float, float) = 0;
	virtual void safe_string_replace(std::string) = 0;
	virtual void keyboard_handler(char ch) = 0;

	void enable()
	{
		enabled = true;
	}

	void disable()
	{
		enabled = false;
	}

	void invert_enable()
	{
		enabled ^= true;
	}

	[[nodiscard]] virtual bool is_resizeable()
	{
		return false;
	}

	/* relative to right-bottom corner */
	virtual void safe_resize(float new_height, float new_width)
	{
		return;
	}

	[[nodiscard]] inline virtual std::uint32_t tell_type()
	{
		return TT_UNSPECIFIED;
	}
};

#endif
