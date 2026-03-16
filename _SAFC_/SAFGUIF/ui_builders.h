#pragma once
#ifndef SAFGUIF_UI_BUILDERS
#define SAFGUIF_UI_BUILDERS

#include "SAFGUIF.h"
#include <memory>
#include <functional>
#include <optional>

namespace safgui
{
	// ============================================================================
	// STYLE SYSTEM
	// ============================================================================

	// Common color palettes
	struct colors
	{
		// Material Design-inspired
		static constexpr std::uint32_t primary = 0x285685CF;
		static constexpr std::uint32_t primary_dark = 0x1A3A5AAF;
		static constexpr std::uint32_t accent = 0xFF7F00FF;
		static constexpr std::uint32_t success = 0x00FF007F;
		static constexpr std::uint32_t warning = 0xFFAF00AF;
		static constexpr std::uint32_t danger = 0xAF0000AF;
		static constexpr std::uint32_t info = 0x007FFFFF;

		// Neutrals
		static constexpr std::uint32_t white = 0xFFFFFFFF;
		static constexpr std::uint32_t white_dim = 0xFFFFFF7F;
		static constexpr std::uint32_t black = 0x000000FF;
		static constexpr std::uint32_t gray_light = 0xC0C0C0FF;
		static constexpr std::uint32_t gray = 0x808080FF;
		static constexpr std::uint32_t gray_dark = 0x404040FF;

		// Backgrounds
		static constexpr std::uint32_t bg_default = 0x070E16AF;
		static constexpr std::uint32_t bg_surface = 0x1A1A2EAF;
		static constexpr std::uint32_t bg_elevated = 0x2D2D44AF;

		// Legacy compatibility
		static constexpr std::uint32_t legacy_background = 0x070E16AF;
		static constexpr std::uint32_t legacy_border = 0xFFFFFF7F;
		static constexpr std::uint32_t legacy_header = 0x285685CF;
	};

	// Button styles
	struct button_style
	{
		std::uint32_t color = colors::white;
		std::uint32_t color_hovered = colors::white;
		std::uint32_t background = 0;
		std::uint32_t background_hovered = 0;
		std::uint32_t border = 0;
		std::uint32_t border_hovered = 0;
		std::uint8_t border_width = 1;
		float corner_radius = 0.f;

		// Preset styles
		static button_style default_style()
		{
			button_style s;
			s.color = 0x00003FAF;
			s.color_hovered = 0x00003FFF;
			s.background = 0;
			s.background_hovered = 0xFFFFFFFF;
			s.border = 0xFFFFFFFF;
			s.border_hovered = 0xFFFFFFFF;
			return s;
		}

		static button_style primary()
		{
			button_style s;
			s.color = colors::white;
			s.color_hovered = colors::white_dim;
			s.background = colors::primary;
			s.background_hovered = colors::primary_dark;
			s.border = colors::primary;
			s.border_hovered = colors::primary_dark;
			return s;
		}

		static button_style success()
		{
			button_style s;
			s.color = colors::white;
			s.color_hovered = colors::white_dim;
			s.background = 0x00AF00AF;
			s.background_hovered = 0x00FF00AF;
			s.border = 0x007F00FF;
			s.border_hovered = 0x00FFFF00;
			return s;
		}

		static button_style danger()
		{
			button_style s;
			s.color = colors::white;
			s.color_hovered = colors::white_dim;
			s.background = 0x3F0000AF;
			s.background_hovered = 0x7F0000AF;
			s.border = 0xFF0000FF;
			s.border_hovered = 0xFF0000FF;
			return s;
		}

		static button_style ghost()
		{
			button_style s;
			s.color = 0x5F5F5FAF;
			s.color_hovered = colors::white;
			s.background = 0;
			s.background_hovered = 0x1FFFFFFF;
			s.border = 0;
			s.border_hovered = 0x3FFFFFFF;
			return s;
		}

		static button_style text_only()
		{
			button_style s;
			s.color = colors::info;
			s.color_hovered = colors::white;
			s.background = 0;
			s.background_hovered = 0;
			s.border = 0;
			s.border_hovered = 0;
			return s;
		}
	};

	// Checkbox styles
	struct checkbox_style
	{
		std::uint32_t border = 0x007FFFFF;
		std::uint32_t background_unchecked = 0xFF7F00AF;
		std::uint32_t background_checked = 0x7FFF00AF;
		std::uint8_t border_width = 1;
	};

	// Window styles
	struct window_style
	{
		std::uint32_t background = colors::bg_default;
		std::uint32_t background_gradient = 0;
		std::uint32_t header = colors::primary;
		std::uint32_t border = colors::white_dim;
		float header_size = 15.f;
	};

	// ============================================================================
	// FLUENT BUILDERS
	// ============================================================================

	// Common base for positionable elements
	template<typename Derived>
	struct positionable_builder
	{
		float m_x = 0.f, m_y = 0.f;
		float m_width = 0.f, m_height = 0.f;

		Derived& at(float px, float py) { m_x = px; m_y = py; return static_cast<Derived&>(*this); }
		Derived& size(float w, float h) { m_width = w; m_height = h; return static_cast<Derived&>(*this); }
		Derived& width_val(float w) { m_width = w; return static_cast<Derived&>(*this); }
		Derived& height_val(float h) { m_height = h; return static_cast<Derived&>(*this); }
	};

	// ----------------------------------------------------------------------------
	// BUTTON BUILDER
	// ----------------------------------------------------------------------------
	class button_builder : public positionable_builder<button_builder>
	{
		std::string m_text;
		std::function<void()> m_on_click;
		button_style m_style = button_style::default_style();
		single_text_line_settings* m_tip_settings = nullptr;
		std::string m_tip_text = " ";
		float m_char_height = 7.5f;

	public:
		button_builder& text(const std::string& t) { m_text = t; return *this; }
		button_builder& on_click(std::function<void()> cb) { m_on_click = std::move(cb); return *this; }
		button_builder& style(const button_style& s) { m_style = s; return *this; }
		button_builder& tip(single_text_line_settings* stls, const std::string& t) { m_tip_settings = stls; m_tip_text = t; return *this; }
		button_builder& char_height(float h) { m_char_height = h; return *this; }

		// Preset style shortcuts
		button_builder& primary() { m_style = button_style::primary(); return *this; }
		button_builder& success() { m_style = button_style::success(); return *this; }
		button_builder& danger() { m_style = button_style::danger(); return *this; }
		button_builder& ghost() { m_style = button_style::ghost(); return *this; }
		button_builder& text_only() { m_style = button_style::text_only(); return *this; }

		std::unique_ptr<button> build()
		{
			return std::make_unique<button>(
				m_text,
				m_on_click,
				m_x, m_y,
				m_width, m_height,
				m_char_height,
				m_style.color,
				m_style.color_hovered,
				15,  // base_point (no gradient by default)
				15,  // grad_point
				m_style.border_width,
				m_style.background,
				m_style.border,
				m_style.color_hovered,
				m_style.background_hovered,
				m_style.border_hovered,
				m_tip_settings,
				m_tip_text
			);
		}

		button* build_into(moveable_window& window, const std::string& id)
		{
			auto ptr = build();
			auto* raw = ptr.get();
			window.add_ui_element(id, std::move(ptr));
			return raw;
		}
	};

	// ----------------------------------------------------------------------------
	// CHECKBOX BUILDER
	// ----------------------------------------------------------------------------
	class checkbox_builder : public positionable_builder<checkbox_builder>
	{
		checkbox_style m_style;
		bool m_start_state = false;
		single_text_line_settings* m_tip_settings = nullptr;
		_Align m_tip_align = _Align::left;
		std::string m_tip_text = " ";
		float m_side_size = 10.f;

	public:
		checkbox_builder& checked(bool state) { m_start_state = state; return *this; }
		checkbox_builder& style(const checkbox_style& s) { m_style = s; return *this; }
		checkbox_builder& tip(single_text_line_settings* stls, const std::string& t, _Align align = _Align::left) {
			m_tip_settings = stls; m_tip_text = t; m_tip_align = align; return *this;
		}
		checkbox_builder& size(float s) { m_side_size = s; return *this; }

		std::unique_ptr<checkbox> build()
		{
			return std::make_unique<checkbox>(
				m_x, m_y,
				m_side_size,
				m_style.border,
				m_style.background_unchecked,
				m_style.background_checked,
				m_style.border_width,
				m_start_state,
				m_tip_settings,
				m_tip_align,
				m_tip_text
			);
		}

		checkbox* build_into(moveable_window& window, const std::string& id)
		{
			auto ptr = build();
			auto* raw = ptr.get();
			window.add_ui_element(id, std::move(ptr));
			return raw;
		}
	};

	// ----------------------------------------------------------------------------
	// INPUT FIELD BUILDER
	// ----------------------------------------------------------------------------
	class input_field_builder : public positionable_builder<input_field_builder>
	{
		std::string m_default_text = "";
		input_field::Type m_type = input_field::Type::text;
		std::uint32_t m_max_chars = 0;
		_Align m_text_align = _Align::left;
		_Align m_tip_align = _Align::left;
		std::string m_tip_text = "";
		std::uint32_t m_border_color = 0x007FFFFF;
		std::uint32_t m_background = 0x0000003F;
		single_text_line_settings* m_tip_settings = nullptr;
		single_text_line_settings* m_text_settings = nullptr;

	public:
		input_field_builder& default_value(const std::string& v) { m_default_text = v; return *this; }
		input_field_builder& type(input_field::Type t) { m_type = t; return *this; }
		input_field_builder& max_chars(std::uint32_t n) { m_max_chars = n; return *this; }
		input_field_builder& align(_Align a) { m_text_align = a; return *this; }
		input_field_builder& tip(single_text_line_settings* stls, const std::string& t, _Align align = _Align::left) {
			m_tip_settings = stls; m_tip_text = t; m_tip_align = align; return *this;
		}
		input_field_builder& tip_text(const std::string& t) { m_tip_text = t; return *this; }

		// Preset types
		input_field_builder& natural_number() { m_type = input_field::Type::NaturalNumbers; return *this; }
		input_field_builder& whole_number() { m_type = input_field::Type::WholeNumbers; return *this; }
		input_field_builder& fp_number() { m_type = input_field::Type::FP_PositiveNumbers; return *this; }
		input_field_builder& any_number() { m_type = input_field::Type::FP_Any; return *this; }
		input_field_builder& text() { m_type = input_field::Type::text; return *this; }

		std::unique_ptr<input_field> build()
		{
			return std::make_unique<input_field>(
				m_default_text,
				m_x, m_y,
				m_height, m_width,
				m_text_settings ? *m_text_settings : system_white,
				nullptr,  // output_source
				m_border_color,
				m_tip_settings,
				m_tip_text,
				m_max_chars,
				m_text_align,
				m_tip_align,
				m_type
			);
		}

		input_field* build_into(moveable_window& window, const std::string& id)
		{
			auto ptr = build();
			auto* raw = ptr.get();
			window.add_ui_element(id, std::move(ptr));
			return raw;
		}
	};

	// ----------------------------------------------------------------------------
	// TEXT BOX BUILDER
	// ----------------------------------------------------------------------------
	class text_box_builder : public positionable_builder<text_box_builder>
	{
		std::string m_text = "_";
		single_text_line_settings* m_settings = &system_white;
		float m_char_height = 7.5f;
		std::uint8_t m_max_lines = 0;
		std::uint8_t m_line_spacing = 0;
		_Align m_align = _Align::left;
		text_box::VerticalOverflow m_overflow = text_box::VerticalOverflow::cut;
		std::uint32_t m_rgba_background = 0;
		std::uint32_t m_rgba_border = 0;
		std::uint8_t m_border_width = 0;

	public:
		text_box_builder& text(const std::string& t) { m_text = t; return *this; }
		text_box_builder& settings(single_text_line_settings* s) { m_settings = s; return *this; }
		text_box_builder& char_height(float h) { m_char_height = h; return *this; }
		text_box_builder& max_lines(std::uint8_t n) { m_max_lines = n; return *this; }
		text_box_builder& line_spacing(std::uint8_t s) { m_line_spacing = s; return *this; }
		text_box_builder& align(_Align a) { m_align = a; return *this; }
		text_box_builder& overflow(text_box::VerticalOverflow o) { m_overflow = o; return *this; }
		text_box_builder& background(std::uint32_t rgba) { m_rgba_background = rgba; return *this; }
		text_box_builder& border(std::uint32_t rgba, std::uint8_t width = 1) { m_rgba_border = rgba; m_border_width = width; return *this; }

		// Preset alignments
		text_box_builder& center() { m_align = _Align::center; return *this; }
		text_box_builder& right() { m_align = _Align::right; return *this; }
		text_box_builder& left() { m_align = _Align::left; return *this; }

		std::unique_ptr<text_box> build()
		{
			return std::make_unique<text_box>(
				m_text,
				m_settings ? *m_settings : system_white,
				m_x, m_y,
				m_char_height, m_width,
				0.f,  // vertical_offset
				m_rgba_background,
				m_rgba_border,
				m_border_width,
				m_align,
				m_overflow
			);
		}

		text_box* build_into(moveable_window& window, const std::string& id)
		{
			auto ptr = build();
			auto* raw = ptr.get();
			window.add_ui_element(id, std::move(ptr));
			return raw;
		}
	};

	// ----------------------------------------------------------------------------
	// WINDOW BUILDER
	// ----------------------------------------------------------------------------
	class window_builder : public positionable_builder<window_builder>
	{
		std::string m_title;
		window_style m_style;
		single_text_line_settings* m_title_settings = &system_white;
		float m_char_height = 2.5f;
		float m_headers_hat_width = 100.f;
		float m_headers_hat_height = 100.f;
		float m_top_handles_height = 50.f;
		float m_bottom_handles_height = 50.f;
		float m_handles_budge_depth = 5.f;

	public:
		window_builder& title(const std::string& t) { m_title = t; return *this; }
		window_builder& title_settings(single_text_line_settings* s) { m_title_settings = s; return *this; }
		window_builder& style(const window_style& s) { m_style = s; return *this; }
		window_builder& char_height(float h) { m_char_height = h; return *this; }
		window_builder& hat_size(float w, float h) { m_headers_hat_width = w; m_headers_hat_height = h; return *this; }
		window_builder& handles(float top, float bottom, float budge) { m_top_handles_height = top; m_bottom_handles_height = bottom; m_handles_budge_depth = budge; return *this; }

		// Preset styles
		window_builder& default_style()
		{
			m_style.background = colors::legacy_background;
			m_style.header = colors::legacy_header;
			m_style.border = colors::legacy_border;
			return *this;
		}

		std::unique_ptr<moveable_window> build()
		{
			return std::make_unique<moveable_fui_window>(
				m_title,
				m_title_settings ? *m_title_settings : system_white,
				m_x, m_y,
				m_width, m_height,
				m_headers_hat_width,
				m_headers_hat_height,
				m_top_handles_height,
				m_bottom_handles_height,
				m_handles_budge_depth,
				m_style.background,
				m_style.header,
				m_style.border
			);
		}

		moveable_window* build_into(windows_handler& handler, const std::string& id)
		{
			auto ptr = build();
			auto* raw = ptr.get();
			handler.win_map[id] = std::move(ptr);
			return raw;
		}
	};

	// ============================================================================
	// FACTORY FUNCTIONS
	// ============================================================================

	inline button_builder make_button() { return button_builder(); }
	inline checkbox_builder make_checkbox() { return checkbox_builder(); }
	inline input_field_builder make_input() { return input_field_builder(); }
	inline text_box_builder make_text_box() { return text_box_builder(); }
	inline window_builder make_window() { return window_builder(); }

	// Quick creation helpers
	inline std::unique_ptr<button> button(
		const std::string& text,
		std::function<void()> on_click,
		float x, float y,
		float width = 75.f, float height = 12.f,
		const button_style& style = button_style::default_style())
	{
		return make_button()
			.text(text)
			.on_click(std::move(on_click))
			.at(x, y)
			.size(width, height)
			.style(style)
			.build();
	}

	inline std::unique_ptr<checkbox> checkbox(
		float x, float y,
		bool checked = false,
		float size = 10.f,
		const checkbox_style& style = checkbox_style{})
	{
		return make_checkbox()
			.at(x, y)
			.size(size)
			.checked(checked)
			.build();
	}

} // namespace safgui

#endif // !SAFGUIF_UI_BUILDERS
