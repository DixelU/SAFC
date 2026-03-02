#pragma once
#ifndef SAFGUIF_HUP
#define SAFGUIF_HUP

#include <mutex>
#include "header_utils.h"

struct handleable_ui_part
{
	std::recursive_mutex lock;
	bool enabled;
	// Backward-compat aliases
	bool& Enabled = enabled;
	std::recursive_mutex& Lock = lock;

	handleable_ui_part() { enabled = true; }
	virtual ~handleable_ui_part() {}
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

	// Backward-compat method wrappers
	void SafeMove(float dx, float dy) { safe_move(dx, dy); }
	void SafeStringReplace(std::string s) { safe_string_replace(std::move(s)); }
	void SafeChangePosition(float x, float y) { safe_change_position(x, y); }
	void SafeChangePosition_Argumented(std::uint8_t a, float x, float y) { safe_change_position_argumented(a, x, y); }
	void SafeResize(float h, float w) { safe_resize(h, w); }
	void Enable() { enable(); }
	void Disable() { disable(); }
};


using HandleableUIPart = handleable_ui_part;

#endif
