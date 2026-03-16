// UI Builders Example - Demonstrating the new fluent API
// Include: #include "SAFGUIF/ui_builders.h"

#include "SAFGUIF/ui_builders.h"

namespace ui_examples
{
	using namespace safgui;

	// ============================================================================
	// EXAMPLE 1: Simple Button Creation
	// ============================================================================

	void old_way_create_button(moveable_window& window)
	{
		// OLD: Verbose, 15+ parameters, magic numbers
		window.add_ui_element("MY_BUTTON", std::make_unique<button>(
			"Click Me",
			[]() { std::cout << "Clicked!" << std::endl; },
			150.f, 167.5f,  // position
			75.f, 12.f,     // size
			7.5f,           // char height
			0x00003FAF,     // color
			0xFFFFFFFF,     // gradient color
			15,             // base point
			15,             // grad point
			1,              // border width
			0,              // background
			0xFFFFFFFF,     // border
			0x00003FFF,     // hovered color
			0xFFFFFFFF,     // hovered background
			0xF7F7F7FF,     // hovered border
			nullptr,        // tip settings
			" "             // tip text
		));
	}

	void new_way_create_button(moveable_window& window)
	{
		// NEW: Fluent, readable, type-safe
		make_button()
			.text("Click Me")
			.on_click([]() { std::cout << "Clicked!" << std::endl; })
			.at(150.f, 167.5f)
			.size(75.f, 12.f)
			.primary()  // or .success(), .danger(), .ghost(), .text_only()
			.build_into(window, "MY_BUTTON");

		// Or even simpler with factory function:
		// window.add_ui_element("MY_BUTTON", button("Click Me", on_click, 150.f, 167.5f, 75.f, 12.f, button_style::primary()));
	}

	// ============================================================================
	// EXAMPLE 2: Multiple Buttons (like the MAIN window)
	// ============================================================================

	void old_way_create_button_row(moveable_window& window)
	{
		// OLD: Repetitive, error-prone
		window.add_ui_element("ADD_Butt", std::make_unique<button>(
			"Add MIDIs", &system_white, on_add, 150, 167.5, 75, 12, 1,
			0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " "));

		window.add_ui_element("REM_Butt", std::make_unique<button>(
			"Remove selected", &system_white, on_rem, 150, 155, 75, 12, 1,
			0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " "));

		window.add_ui_element("REM_ALL_Butt", std::make_unique<button>(
			"Remove all", &system_white, on_rem_all, 150, 142.5, 75, 12, 1,
			0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0xF7F7F7FF, &system_white, "May cause lag"));
	}

	void new_way_create_button_row(moveable_window& window,
		std::function<void()> on_add,
		std::function<void()> on_rem,
		std::function<void()> on_rem_all)
	{
		// NEW: Clean, consistent, easy to modify
		float x = 150.f, y = 167.5f, spacing = 12.5f;

		make_button()
			.text("Add MIDIs")
			.on_click(on_add)
			.at(x, y)
			.size(75.f, 12.f)
			.style(button_style::default_style())
			.build_into(window, "ADD_Butt");

		make_button()
			.text("Remove selected")
			.on_click(on_rem)
			.at(x, y - spacing)
			.size(75.f, 12.f)
			.danger()  // Preset danger style
			.build_into(window, "REM_Butt");

		make_button()
			.text("Remove all")
			.on_click(on_rem_all)
			.at(x, y - spacing * 2)
			.size(75.f, 12.f)
			.danger()
			.tip(&system_white, "May cause lag")
			.build_into(window, "REM_ALL_Butt");
	}

	// ============================================================================
	// EXAMPLE 3: Checkbox with Tooltip
	// ============================================================================

	void old_way_create_checkbox(moveable_window& window)
	{
		// OLD: Many parameters, unclear meaning
		window.add_ui_element("BOOL_REM_TRCKS", std::make_unique<checkbox>(
			-97.5f + moveable_window::window_header_size,
			55.f - moveable_window::window_header_size,
			10.f,               // side size
			0x007FFFFF,         // border color
			0xFF00007F,         // unchecked background
			0x00FF007F,         // checked background
			1,                  // border width
			1,                  // start state
			&system_white,      // tip settings
			_Align::left,       // tip align
			"Remove empty tracks"  // tip text
		));
	}

	void new_way_create_checkbox(moveable_window& window)
	{
		// NEW: Named parameters, readable
		make_checkbox()
			.at(-97.5f + moveable_window::window_header_size, 55.f - moveable_window::window_header_size)
			.size(10.f)
			.checked(true)
			.tip(&system_white, "Remove empty tracks", _Align::left)
			.build_into(window, "BOOL_REM_TRCKS");
	}

	// ============================================================================
	// EXAMPLE 4: Input Field
	// ============================================================================

	void old_way_create_input(moveable_window& window)
	{
		// OLD: Many parameters
		window.add_ui_element("PPQN", std::make_unique<input_field>(
			" ",
			-90 + moveable_window::window_header_size,
			75 - moveable_window::window_header_size,
			10.f, 25.f,
			&system_white,
			nullptr,
			0x007FFFFF,
			&system_white,
			"PPQN is lesser than 65536.",
			5,
			_Align::center,
			_Align::left,
			input_field::Type::NaturalNumbers
		));
	}

	void new_way_create_input(moveable_window& window)
	{
		// NEW: Fluent interface
		make_input()
			.at(-90 + moveable_window::window_header_size, 75 - moveable_window::window_header_size)
			.size(25.f, 10.f)
			.default_value(" ")
			.natural_number()  // or .whole_number(), .fp_number(), .text()
			.max_chars(5)
			.align(_Align::center)
			.tip(&system_white, "PPQN is lesser than 65536.", _Align::left)
			.build_into(window, "PPQN");
	}

	// ============================================================================
	// EXAMPLE 5: Complete Window Creation
	// ============================================================================

	void old_way_create_window(windows_handler& handler)
	{
		// OLD: Constructor with 12+ parameters
		constexpr unsigned BACKGROUND = 0x070E16AF;
		constexpr unsigned BORDER = 0xFFFFFF7F;
		constexpr unsigned HEADER = 0x285685CF;

		moveable_window* window = new moveable_fui_window(
			"Props. and sets.",
			&system_white,
			-100, 100, 200, 225,
			100, 2.5f, 75, 50, 3,
			BACKGROUND, HEADER, BORDER);

		// Then add elements one by one...
	}

	void new_way_create_window(windows_handler& handler)
	{
		// NEW: Builder pattern
		auto* window = make_window()
			.title("Props. and sets.")
			.at(-100.f, 100.f)
			.size(200.f, 225.f)
			.min_size(100.f, 100.f)
			.default_style()
			.build_into(handler, "SMPAS");

		// Add elements using the window pointer
		make_button()
			.text("Apply")
			.on_click([]() { /* apply settings */ })
			.at(87.5f - moveable_window::window_header_size, 15.f - moveable_window::window_header_size)
			.size(30.f, 10.f)
			.success()
			.build_into(*window, "APPLY");
	}

	// ============================================================================
	// EXAMPLE 6: Button Row Helper (Common Pattern)
	// ============================================================================

	namespace helpers
	{
		// Helper for creating evenly spaced button rows
		inline void create_button_row(
			moveable_window& window,
			float x, float y,
			float button_width, float button_height,
			float spacing,
			std::initializer_list<std::tuple<const char*, const char*, std::function<void()>, button_style>> buttons)
		{
			float current_y = y;
			for (const auto& [id, text, callback, style] : buttons)
			{
				make_button()
					.text(text)
					.on_click(callback)
					.at(x, current_y)
					.size(button_width, button_height)
					.style(style)
					.build_into(window, id);
				current_y -= spacing;
			}
		}
	}

	void using_button_row_helper(moveable_window& window)
	{
		using namespace helpers;

		create_button_row(
			window,
			150.f, 167.5f,  // start position
			75.f, 12.f,     // button size
			12.5f,          // spacing
			{
				{ "ADD_Butt", "Add MIDIs", on_add, button_style::default_style() },
				{ "REM_Butt", "Remove selected", on_rem, button_style::danger() },
				{ "REM_ALL_Butt", "Remove all", on_rem_all, button_style::danger() },
			}
		);
	}

	// ============================================================================
	// EXAMPLE 7: Settings Panel (Composite Component Pattern)
	// ============================================================================

	class settings_panel
	{
		moveable_window& window;
		float cursor_y = 100.f;
		const float left_x = -90.f;
		const float right_x = 20.f;
		const float row_spacing = 20.f;

	public:
		settings_panel(moveable_window& w) : window(w) {}

		settings_panel& add_checkbox(const std::string& id, const std::string& label, bool default_checked = false)
		{
			make_checkbox()
				.at(left_x, cursor_y)
				.size(10.f)
				.checked(default_checked)
				.tip(&system_white, label, _Align::left)
				.build_into(window, id);
			cursor_y -= row_spacing;
			return *this;
		}

		settings_panel& add_input(const std::string& id, const std::string& label, float x_offset = 0)
		{
			make_input()
				.at(left_x + x_offset, cursor_y)
				.size(25.f, 10.f)
				.tip(&system_white, label, _Align::left)
				.build_into(window, id);
			return *this;
		}

		settings_panel& add_button(const std::string& id, const std::string& text, std::function<void()> cb, const button_style& style = button_style::default_style())
		{
			make_button()
				.text(text)
				.on_click(std::move(cb))
				.at(left_x, cursor_y)
				.size(75.f, 12.f)
				.style(style)
				.build_into(window, id);
			cursor_y -= row_spacing;
			return *this;
		}

		settings_panel& spacer(float amount = row_spacing)
		{
			cursor_y -= amount;
			return *this;
		}
	};

	void using_settings_panel(moveable_window& window)
	{
		settings_panel panel(window);

		panel
			.add_checkbox("BOOL_REM_TRCKS", "Remove empty tracks", true)
			.add_checkbox("BOOL_REM_REM", "Remove merge remnants", true)
			.spacer(10.f)
			.add_checkbox("SPLIT_TRACKS", "Multichannel split", false)
			.add_checkbox("RSB_COMPRESS", "Enable RSB compression", false)
			.spacer(10.f)
			.add_button("OTHER_CHECKBOXES", "Other settings", on_other_settings, button_style::ghost())
			.add_button("APPLY", "Apply", on_apply_settings, button_style::success());
	}

} // namespace ui_examples

// ============================================================================
// MIGRATION GUIDE
// ============================================================================
/*
Quick Reference:

OLD WAY                                              NEW WAY
--------------------------------------------------------------------------------
new button("Text", ...)                              make_button().text("Text")...
  15+ parameters in specific order                   Named methods, any order
  Magic numbers (colors, sizes)                      Preset styles (.primary(), .danger())
  Manual position calculation                        Fluent positioning (.at(x, y))
  No type safety                                     Compile-time checks
  Repetitive boilerplate                             Reusable builders

COLOR PRESETS:
  0x00003FAF (default button)         →  button_style::default_style()
  0x3F0000AF (remove/danger)          →  button_style::danger()
  0x00AF00AF (success/apply)          →  button_style::success()
  0x5F5F5FAF (settings/ghost)         →  button_style::ghost()
  0x007FFF3F (accent)                 →  button_style::text_only()

COMMON PATTERNS:

1. Button with click handler:
   make_button()
     .text("Click Me")
     .on_click([]() { /* your code *\/ })
     .at(x, y)
     .size(width, height)
     .primary()  // or .danger(), .success(), .ghost()
     .build_into(window, "ID");

2. Checkbox with tooltip:
   make_checkbox()
     .at(x, y)
     .size(10.f)
     .checked(true)
     .tip(&system_white, "Tooltip text", _Align::left)
     .build_into(window, "ID");

3. Input field:
   make_input()
     .at(x, y)
     .size(height, width)
     .default_value("initial")
     .natural_number()  // or .whole_number(), .fp_number(), .text()
     .max_chars(10)
     .tip(&system_white, "Help text")
     .build_into(window, "ID");

4. Text box:
   make_text_box()
     .text("Content")
     .at(x, y)
     .size(width, height)
     .char_height(7.5f)
     .center()  // or .left(), .right()
     .build_into(window, "ID");

BENEFITS:
✓ 60-80% less boilerplate code
✓ Self-documenting code (named parameters)
✓ Consistent styling across the app
✓ Easier to refactor and maintain
✓ Compile-time type safety
✓ Reusable components and patterns
*/
