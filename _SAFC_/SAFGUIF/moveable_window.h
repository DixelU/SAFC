#pragma once
#ifndef SAFGUIF_MW
#define SAFGUIF_MW

#include <map>
#include <memory>

#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

// Proxy returned by moveable_window::operator[] to allow both ownership transfer
// (proxy = new Widget / proxy = std::make_unique<Widget>()) and transparent access
// (proxy->method(), proxy.get(), (Type*)proxy).
struct ui_element_proxy
{
	std::unique_ptr<handleable_ui_part>& ref;
	explicit ui_element_proxy(std::unique_ptr<handleable_ui_part>& r) : ref(r) {}
	ui_element_proxy(const ui_element_proxy& o) : ref(o.ref) {}
	ui_element_proxy& operator=(const ui_element_proxy&) = delete;

	// Ownership transfer from raw pointer: proxy = new Widget(...)
	template<typename T, std::enable_if_t<std::is_base_of_v<handleable_ui_part, T>, int> = 0>
	T* operator=(T* raw) { ref.reset(raw); return raw; }

	// Ownership transfer from unique_ptr: proxy = std::make_unique<Widget>(...)
	template<typename T, std::enable_if_t<std::is_base_of_v<handleable_ui_part, T>, int> = 0>
	void operator=(std::unique_ptr<T>&& uptr) { ref = std::move(uptr); }

	// Transparent access
	handleable_ui_part* get() const { return ref.get(); }
	operator handleable_ui_part*() const { return ref.get(); }
	handleable_ui_part* operator->() const { return ref.get(); }
	handleable_ui_part& operator*() const { return *ref; }

	// Cast to any derived type: (Widget*)proxy
	template<typename T, std::enable_if_t<std::is_base_of_v<handleable_ui_part, std::remove_cv_t<T>> && !std::is_same_v<std::remove_cv_t<T>, handleable_ui_part>, int> = 0>
	operator T*() const { return static_cast<T*>(ref.get()); }

	// Access the underlying unique_ptr directly
	std::unique_ptr<handleable_ui_part>& get_ptr() { return ref; }
};

struct moveable_window : handleable_ui_part
{
	inline static constexpr int window_header_size = 15;

	float x_window_pos, y_window_pos; // left-up corner coordinates
	float width, height;
	std::uint32_t rgba_background, rgba_theme_color, rgba_grad_background;
	std::unique_ptr<single_text_line> window_name;
	std::map<std::string, std::unique_ptr<handleable_ui_part>> window_activities;
	bool drawable;
	bool hovered_close_button;
	bool cursor_follow_mode;
	bool map_was_changed;
	float pc_cur_x, pc_cur_y;
	// Backward-compat aliases
	float& XWindowPos = x_window_pos;
	float& YWindowPos = y_window_pos;
	float& Width = width;
	float& Height = height;
	std::uint32_t& RGBABackground = rgba_background;
	std::unique_ptr<single_text_line>& WindowName = window_name;
	std::map<std::string, std::unique_ptr<handleable_ui_part>>& WindowActivities = window_activities;

	~moveable_window() override
	{
		std::lock_guard locker(lock);
		window_activities.clear();
	}

	moveable_window(std::string win_name, single_text_line_settings* win_name_settings,
		float x_pos, float y_pos, float width, float height,
		std::uint32_t rgba_background, std::uint32_t rgba_theme_color, std::uint32_t rgba_grad_background = 0)
	{
		if (win_name_settings)
		{
			win_name_settings->set_new_pos(x_pos, y_pos);
			this->window_name.reset(win_name_settings->create_one(win_name));
			this->window_name->safe_move(this->window_name->calculated_width * 0.5f + window_header_size * 0.5f, 0.f - window_header_size * 0.5f);
		}
		this->map_was_changed = false;
		this->x_window_pos = x_pos;
		this->y_window_pos = y_pos;
		this->width = width;
		this->height = (height < window_header_size) ? (float)window_header_size : height;
		this->rgba_background = rgba_background;
		this->rgba_theme_color = rgba_theme_color;
		this->rgba_grad_background = rgba_grad_background;
		this->cursor_follow_mode = false;
		this->hovered_close_button = false;
		this->drawable = true;
		this->pc_cur_x = 0.f;
		this->pc_cur_y = 0.f;
	}

	void keyboard_handler(char ch) override
	{
		std::lock_guard locker(lock);
		for (auto& [_, elem] : window_activities)
		{
			if (elem) elem->keyboard_handler(ch);
			if (map_was_changed)
			{
				map_was_changed = false;
				break;
			}
		}
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char button_btn, char state) override
	{
		std::lock_guard locker(lock);
		if (!drawable) return false;
		hovered_close_button = false;
		if (mx > x_window_pos + width - window_header_size && mx < x_window_pos + width
			&& my < y_window_pos && my > y_window_pos - window_header_size)
		{
			// close button
			if (button_btn && state == 1)
			{
				drawable = false;
				cursor_follow_mode = false;
				return true;
			}
			else if (!button_btn)
				hovered_close_button = true;
		}
		else if (mx - x_window_pos < width && mx - x_window_pos > 0
			&& my < y_window_pos && my > y_window_pos - window_header_size)
		{
			if (button_btn == -1)
			{
				// window header drag
				if (state == -1)
				{
					cursor_follow_mode = !cursor_follow_mode;
					pc_cur_x = mx;
					pc_cur_y = my;
				}
				else if (state == 1)
					cursor_follow_mode = !cursor_follow_mode;
			}
		}
		if (cursor_follow_mode)
		{
			safe_move(mx - pc_cur_x, my - pc_cur_y);
			pc_cur_x = mx;
			pc_cur_y = my;
			return true;
		}

		bool flag = false;
		auto it = window_activities.begin();
		while (it != window_activities.end())
		{
			if (it->second)
				flag = it->second->mouse_handler(mx, my, button_btn, state);
			if (map_was_changed)
			{
				map_was_changed = false;
				break;
			}
			++it;
		}

		if (mx - x_window_pos < width && mx - x_window_pos > 0
			&& y_window_pos - my > 0 && y_window_pos - my < height)
		{
			if (button_btn) return true;
			else return flag;
		}
		else
			return flag;
	}

	void safe_change_position(float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		new_x -= x_window_pos;
		new_y -= y_window_pos;
		safe_move(new_x, new_y);
	}

	bool delete_ui_element_by_name(const std::string& element_name)
	{
		std::lock_guard locker(lock);
		map_was_changed = true;
		auto ptr = window_activities.find(element_name);
		if (ptr == window_activities.end()) return false;
		window_activities.erase(element_name);
		return true;
	}

	bool add_ui_element(const std::string& element_name, std::unique_ptr<handleable_ui_part> elem)
	{
		std::lock_guard locker(lock);
		map_was_changed = true;
		auto ans = window_activities.insert_or_assign(element_name, std::move(elem));
		return ans.second;
	}

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_window_pos += dx;
		y_window_pos += dy;
		if (window_name) window_name->safe_move(dx, dy);
		for (auto& [_, elem] : window_activities)
			if (elem) elem->safe_move(dx, dy);
	}

	void safe_change_position_argumented(std::uint8_t arg, float new_x, float new_y) override
	{
		std::lock_guard locker(lock);
		float cw = 0.5f * (
			(std::int32_t)(!!(GLOBAL_LEFT & arg))
			- (std::int32_t)(!!(GLOBAL_RIGHT & arg))
			- 1) * width,
			ch = 0.5f * (
				(std::int32_t)(!!(GLOBAL_BOTTOM & arg))
				- (std::int32_t)(!!(GLOBAL_TOP & arg))
				+ 1) * height;
		safe_change_position(new_x + cw, new_y + ch);
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		if (!drawable) return;
		__glcolor(rgba_background);
		glBegin(GL_QUADS);
		glVertex2f(x_window_pos, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos);
		if (rgba_grad_background) __glcolor(rgba_grad_background);
		glVertex2f(x_window_pos + width, y_window_pos - height);
		glVertex2f(x_window_pos, y_window_pos - height);
		glEnd();
		__glcolor(rgba_theme_color);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x_window_pos, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos - height);
		glVertex2f(x_window_pos, y_window_pos - height);
		glEnd();
		glBegin(GL_QUADS);
		glVertex2f(x_window_pos, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos - window_header_size);
		glVertex2f(x_window_pos, y_window_pos - window_header_size);
		glColor4ub(255, (GLubyte)(32 + 32 * hovered_close_button), (GLubyte)(32 + 32 * hovered_close_button), 255);
		glVertex2f(x_window_pos + width, y_window_pos);
		glVertex2f(x_window_pos + width, y_window_pos + 1 - window_header_size);
		glVertex2f(x_window_pos + width - window_header_size, y_window_pos + 1 - window_header_size);
		glVertex2f(x_window_pos + width - window_header_size, y_window_pos);
		glEnd();

		if (window_name) window_name->draw();

		for (auto& [_, elem] : window_activities)
			if (elem) elem->draw();
	}

	void not_safe_resize(float new_height, float new_width)
	{
		std::lock_guard locker(lock);
		height = new_height;
		width = new_width;
	}

	void not_safe_resize_centered(float new_height, float new_width)
	{
		std::lock_guard locker(lock);
		float dx, dy;
		x_window_pos += (dx = -0.5f * (new_width - width));
		y_window_pos += (dy = 0.5f * (new_height - height));
		if (window_name) window_name->safe_move(dx, dy);
		width = new_width;
		height = new_height;
	}

	void safe_string_replace(std::string new_window_title) override
	{
		std::lock_guard locker(lock);
		safe_window_rename(new_window_title);
	}

	virtual void safe_window_rename(const std::string& new_window_title, bool force_no_shift = false)
	{
		std::lock_guard locker(lock);
		if (!window_name) return;
		window_name->safe_string_replace(new_window_title);
		if (force_no_shift) return;
		window_name->safe_change_position_argumented(GLOBAL_LEFT, x_window_pos + window_header_size * 0.5f, y_window_pos - window_header_size * 0.5f);
	}

	ui_element_proxy operator[](const std::string& id)
	{
		return ui_element_proxy(window_activities[id]);
	}

	[[nodiscard]] inline std::uint32_t tell_type() override { return TT_MOVEABLE_WINDOW; }

	// Legacy method names for backward compat
	void _NotSafeResize(float new_height, float new_width) { not_safe_resize(new_height, new_width); }
	void _NotSafeResize_Centered(float new_height, float new_width) { not_safe_resize_centered(new_height, new_width); }
	void SafeWindowRename(const std::string& t, bool f = false) { safe_window_rename(t, f); }
	bool DeleteUIElementByName(const std::string& name) { return delete_ui_element_by_name(name); }
	bool AddUIElement(const std::string& name, handleable_ui_part* raw_elem) { return add_ui_element(name, std::unique_ptr<handleable_ui_part>(raw_elem)); }
};

using MoveableWindow = moveable_window;

// Backward-compat global constant (was a macro/global in old code)
inline constexpr int WindowHeaderSize = moveable_window::window_header_size;

#endif
