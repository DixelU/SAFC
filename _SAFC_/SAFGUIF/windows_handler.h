#pragma once
#ifndef SAFGUIF_WH
#define SAFGUIF_WH

#include "handleable_ui_part.h"
#include "movable_resizeable_window.h"
#include "textbox.h"
#include "special_signs.h"
#include "input_field.h"
#include "button_settings.h"
#include "stls_presets.h"

// Proxy returned by windows_handler::operator[] to allow both ownership transfer
// (proxy = new MoveableWindow) and transparent access (proxy->method(), (Type*)proxy).
struct window_proxy
{
	std::unique_ptr<moveable_window>& ref;
	explicit window_proxy(std::unique_ptr<moveable_window>& r) : ref(r) {}
	window_proxy(const window_proxy& o) : ref(o.ref) {}
	window_proxy& operator=(const window_proxy&) = delete;

	// Ownership transfer from raw pointer: proxy = new MoveableFuiWindow(...)
	template<typename T, std::enable_if_t<std::is_base_of_v<moveable_window, T>, int> = 0>
	T* operator=(T* raw) { ref.reset(raw); return raw; }

	// Ownership transfer from unique_ptr: proxy = std::make_unique<MoveableFuiWindow>(...)
	template<typename T, std::enable_if_t<std::is_base_of_v<moveable_window, T>, int> = 0>
	void operator=(std::unique_ptr<T>&& uptr) { ref = std::move(uptr); }

	// Transparent access
	moveable_window* get() const { return ref.get(); }
	operator moveable_window*() const { return ref.get(); }
	moveable_window* operator->() const { return ref.get(); }
	moveable_window& operator*() const { return *ref; }

	// Cast to any derived type: (MoveableFuiWindow*)proxy
	template<typename T, std::enable_if_t<std::is_base_of_v<moveable_window, T> && !std::is_same_v<T, moveable_window>, int> = 0>
	operator T*() const { return static_cast<T*>(ref.get()); }

	// Access the underlying unique_ptr directly
	std::unique_ptr<moveable_window>& get_ptr() { return ref; }
};

struct windows_handler
{
	using win_map_t = std::map<std::string, std::unique_ptr<moveable_window>>;
	using active_list_t = std::list<win_map_t::iterator>;

	win_map_t win_map;
	active_list_t active_windows;
	std::string main_window_id, mw_id_holder;
	bool window_was_disabled_during_mouse_handling;
	std::recursive_mutex lock;
	// Backward-compat alias
	std::string& MainWindow_ID = main_window_id;

	static constexpr float alerttext_vert_pos = -7.5f, alertheight = 65.f;

	windows_handler()
	{
		constexpr unsigned BACKGROUND = 0x070E16AF;
		constexpr unsigned BORDER = 0xFFFFFF7F;
		constexpr unsigned HEADER_ALERT = 0x855628CF;
		constexpr unsigned HEADER_PROMPT = 0x288556CF;

		mw_id_holder.clear();
		main_window_id = "MAIN";
		window_was_disabled_during_mouse_handling = false;

		win_map["ALERT"] = std::make_unique<moveable_fui_window>(
			"Alert window", System_White,
			-100, alertheight / 2, 200, alertheight,
			150, 2.5f, alertheight / 8, alertheight / 8, 1.25f,
			BACKGROUND, HEADER_ALERT, BORDER);
		{
			auto& ptr = win_map["ALERT"];
			(*ptr)["AlertText"] = std::make_unique<text_box>(
				"_", System_White, 20.f, alerttext_vert_pos,
				alertheight - 12.5f, 155.f, 7.5f, 0, 0, 0,
				_Align::left, text_box::VerticalOverflow::recalibrate);
			(*ptr)["AlertSign"] = std::make_unique<special_sign_handler>(
				special_signs::draw_a_circle, -78.5f, -17.f, 12.f, 0x000000FF, 0x001FFFFF);
			ptr->window_name->safe_move(0, 2.5f);
		}

		win_map["PROMPT"] = std::make_unique<moveable_fui_window>(
			"prompt", System_White,
			-50, 50, 100, 100, 50, 2.5f, 25, 25, 2.5f,
			BACKGROUND, HEADER_PROMPT, BORDER);
		{
			auto& ptr = win_map["PROMPT"];
			(*ptr)["FLD"] = std::make_unique<input_field>(
				"", 0.f, 35.f - moveable_window::window_header_size,
				10.f, 80.f, System_White, nullptr, 0x007FFFFF, nullptr,
				"", 0, _Align::center);
			(*ptr)["TXT"] = std::make_unique<text_box>(
				"_abc_", System_White,
				0.f, 2.5f - moveable_window::window_header_size,
				10.f, 80.f, 7.5f, 0, 0, 2,
				_Align::center, text_box::VerticalOverflow::recalibrate);
			(*ptr)["BUTT"] = std::make_unique<button>(
				"Submit", System_White, nullptr,
				0.f, -20.f - moveable_window::window_header_size, 80.f, 10.f, 1,
				0x007FFF3F, 0x007FFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xFF7F00FF,
				nullptr, " ");
			ptr->window_name->safe_move(0, 2.5f);
		}
	}

	~windows_handler() = default;

	void mouse_handler(float mx, float my, char button_btn, char state)
	{
		std::lock_guard locker(lock);
		auto aw_iterator = active_windows.begin();
		auto current_aw = aw_iterator;
		if (!button_btn && !active_windows.empty())
			(*active_windows.begin())->second->mouse_handler(mx, my, 0, 0);
		else
		{
			while (aw_iterator != active_windows.end() &&
				!((*aw_iterator)->second->mouse_handler(mx, my, button_btn, state)) &&
				!window_was_disabled_during_mouse_handling)
				++aw_iterator;
			if (!window_was_disabled_during_mouse_handling && active_windows.size() > 1 &&
				aw_iterator != active_windows.end() && aw_iterator != active_windows.begin())
				if (current_aw == active_windows.begin())
					enable_window(*aw_iterator);
			if (window_was_disabled_during_mouse_handling)
				window_was_disabled_during_mouse_handling = false;
		}
	}

	void throw_prompt(
		const std::string& static_tip_text,
		const std::string& window_title,
		std::function<void()> on_submit,
		_Align s_tip_align,
		input_field::Type input_type,
		const std::string& default_string = "",
		std::uint32_t max_chars = 0)
	{
		std::lock_guard locker(lock);
		auto& wptr = win_map["PROMPT"];
		auto* ifptr = static_cast<input_field*>((*wptr)["FLD"].get());
		auto* tbptr = static_cast<text_box*>((*wptr)["TXT"].get());
		ifptr->input_type = input_type;
		ifptr->max_chars = max_chars;
		ifptr->update_input_string(default_string);
		tbptr->text_align = s_tip_align;
		tbptr->safe_string_replace(static_tip_text);
		wptr->safe_window_rename(window_title, false);

		static_cast<button*>((*wptr)["BUTT"].get())->on_click = std::move(on_submit);

		wptr->safe_change_position(-50, 50);
		enable_window("PROMPT");
	}

	void throw_alert(
		const std::string& alert_text,
		const std::string& alert_header,
		std::function<void(float, float, float, std::uint32_t, std::uint32_t)> special_signs_draw_func,
		bool update = false,
		std::uint32_t frgba = 0,
		std::uint32_t srgba = 0)
	{
		std::lock_guard locker(lock);
		auto& alert_wptr = win_map["ALERT"];
		alert_wptr->safe_change_position_argumented(0, 0, 0);
		alert_wptr->not_safe_resize_centered(alertheight, 200);
		auto* alert_wt_ptr = static_cast<text_box*>((*alert_wptr)["AlertText"].get());
		alert_wt_ptr->safe_string_replace(alert_text);
		alert_wptr->safe_window_rename(alert_header, false);

		auto* alert_ws_ptr = static_cast<special_sign_handler*>((*alert_wptr)["AlertSign"].get());
		alert_ws_ptr->replace_draw_func(std::move(special_signs_draw_func));
		if (update)
		{
			alert_ws_ptr->frgba = frgba;
			alert_ws_ptr->srgba = srgba;
		}
		enable_window("ALERT");
	}

	// Legacy overload: accepts raw function pointer for ThrowAlert
	void ThrowAlert(
		const std::string& alert_text,
		const std::string& alert_header,
		void(*special_signs_draw_func)(float, float, float, std::uint32_t, std::uint32_t),
		bool update = false,
		std::uint32_t frgba = 0,
		std::uint32_t srgba = 0)
	{
		throw_alert(alert_text, alert_header,
			special_signs_draw_func ? std::function<void(float, float, float, std::uint32_t, std::uint32_t)>(special_signs_draw_func) : nullptr,
			update, frgba, srgba);
	}

	// Legacy overload: accepts raw function pointer for ThrowPrompt
	void ThrowPrompt(
		const std::string& static_tip_text,
		const std::string& window_title,
		void(*on_submit)(),
		_Align s_tip_align,
		input_field::Type input_type,
		const std::string& default_string = "",
		std::uint32_t max_chars = 0)
	{
		throw_prompt(static_tip_text, window_title,
			on_submit ? std::function<void()>(on_submit) : nullptr,
			s_tip_align, input_type, default_string, max_chars);
	}

	void disable_window(const std::string& id)
	{
		std::lock_guard locker(lock);
		auto y = win_map.find(id);
		if (y != win_map.end())
		{
			window_was_disabled_during_mouse_handling = true;
			y->second->drawable = true;
			active_windows.remove(y);
		}
	}

	void DisableAllWindows()
	{
		std::lock_guard locker(lock);
		window_was_disabled_during_mouse_handling = true;
		active_windows.clear();
		enable_window(main_window_id);
	}

	void TurnOnMainWindow()
	{
		std::lock_guard locker(lock);
		if (!this->mw_id_holder.empty())
			swap(this->main_window_id, this->mw_id_holder);
		this->enable_window(main_window_id);
	}

	void TurnOffMainWindow()
	{
		std::lock_guard locker(lock);
		if (this->mw_id_holder.empty())
			swap(this->main_window_id, this->mw_id_holder);
		this->disable_window(main_window_id);
	}

	void disable_window(active_list_t::iterator window)
	{
		std::lock_guard locker(lock);
		if (window == active_windows.end())
			return;
		window_was_disabled_during_mouse_handling = true;
		(*window)->second->drawable = true;
		active_windows.erase(window);
	}

	void enable_window(win_map_t::iterator window)
	{
		std::lock_guard locker(lock);
		if (window == win_map.end())
			return;
		for (auto y = active_windows.begin(); y != active_windows.end(); ++y)
		{
			if (*y == window)
			{
				window->second->drawable = true;
				if (y != active_windows.begin())
				{
					auto q = y;
					--q;
					active_windows.erase(y);
					y = q;
				}
				else
				{
					active_windows.erase(y);
					if (active_windows.size())
						y = active_windows.begin();
					else
						break;
				}
			}
		}
		active_windows.push_front(window);
	}

	void enable_window(const std::string& id)
	{
		std::lock_guard locker(lock);
		this->enable_window(win_map.find(id));
	}

	void keyboard_handler(char ch)
	{
		std::lock_guard locker(lock);
		if (active_windows.size())
			(*active_windows.begin())->second->keyboard_handler(ch);
	}

	void draw()
	{
		std::lock_guard locker(lock);
		bool met_main = false;
		if (!active_windows.empty())
		{
			auto y = (++active_windows.rbegin()).base();
			while (true)
			{
				if (!((*y)->second->drawable))
				{
					if ((*y)->first != main_window_id)
						if (y == active_windows.begin())
						{
							disable_window(y);
							break;
						}
						else disable_window(y);
					else
						(*y)->second->drawable = true;
					continue;
				}
				(*y)->second->draw();
				if ((*y)->first == main_window_id)
					met_main = true;
				if (y == active_windows.begin())
					break;
				y--;
			}
		}
		if (!met_main)
			this->enable_window(main_window_id);
	}

	inline window_proxy operator[](const std::string& id)
	{
		return window_proxy(win_map[id]);
	}

	// Legacy method aliases
	void EnableWindow(const std::string& id) { enable_window(id); }
	void DisableWindow(const std::string& id) { disable_window(id); }
	void MouseHandler(float mx, float my, char b, char s) { mouse_handler(mx, my, b, s); }
	void KeyboardHandler(char ch) { keyboard_handler(ch); }
	void Draw() { draw(); }
};

inline std::shared_ptr<windows_handler> WH;

using WindowsHandler = windows_handler;

#endif // !SAFGUIF_WH
