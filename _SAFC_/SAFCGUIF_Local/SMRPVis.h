#pragma once
#ifndef SAFGUIF_L_SMRPV
#define SAFGUIF_L_SMRPV

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/include_all.h"

struct SMRP_Vis : handleable_ui_part
{
	std::pair<
		midi_collection_threaded_merger::proc_data_ptr,
		midi_collection_threaded_merger::message_buffer_ptr> SMRP;

	float x_pos, y_pos;
	bool processing, finished, hovered;

	std::unique_ptr<single_text_line> stl_log, stl_warn, stl_err, stl_info;

	SMRP_Vis(float xpos, float ypos, single_text_line_settings* stls)
	{
		std::lock_guard locker(lock);
		this->processing = this->hovered = this->finished = false;
		this->x_pos = xpos;
		this->y_pos = ypos;
		auto base_rgba = stls->rgba_color;
		stls->rgba_color = 0xFFFFFFFF;
		stls->set_new_pos(xpos, ypos - 20);
		this->stl_log.reset(stls->create_one("_"));
		stls->rgba_color = 0xFFFF00FF;
		stls->set_new_pos(xpos, ypos - 30);
		this->stl_warn.reset(stls->create_one("_"));
		stls->rgba_color = 0xFF3F00FF;
		stls->set_new_pos(xpos, ypos - 40);
		this->stl_err.reset(stls->create_one("_"));
		stls->rgba_color = 0xFFFFFFFF;
		stls->set_new_pos(xpos, ypos + 40);
		this->stl_info.reset(stls->create_one("_"));
		stls->rgba_color = base_rgba;
	}

	~SMRP_Vis() override = default;

	void safe_move(float dx, float dy) override
	{
		std::lock_guard locker(lock);
		x_pos += dx;
		y_pos += dy;
		stl_err->safe_move(dx, dy);
		stl_warn->safe_move(dx, dy);
		stl_log->safe_move(dx, dy);
		stl_info->safe_move(dx, dy);
	}

	void safe_change_position(float new_x, float new_y) override
	{
		new_x -= x_pos;
		new_y -= y_pos;
		safe_move(new_x, new_y);
	}

	void safe_change_position_argumented(std::uint8_t, float, float) override
	{
		return;
	}

	void keyboard_handler(char) override
	{
		return;
	}

	void safe_string_replace(std::string) override
	{
		return;
	}

	void set_info_string(const std::string& new_info_string)
	{
		std::lock_guard locker(lock);
		this->stl_info->safe_string_replace(new_info_string);
	}

	void set_smrp(std::pair<
		midi_collection_threaded_merger::proc_data_ptr,
		midi_collection_threaded_merger::message_buffer_ptr>& smrp)
	{
		std::lock_guard locker(lock);
		SMRP = smrp;
	}

	// Backward-compat method wrappers
	void SetSMRP(std::pair<
		midi_collection_threaded_merger::proc_data_ptr,
		midi_collection_threaded_merger::message_buffer_ptr>& smrp) { set_smrp(smrp); }
	void SetInfoString(const std::string& s) { set_info_string(s); }

	void update_info()
	{
		if (!SMRP.first || !SMRP.second)
			return;

		std::lock_guard locker(lock);
		auto& logger_ptr = SMRP.second;

		auto last_logger_line = logger_ptr->log->getLast();
		if (last_logger_line != stl_log->current_text)
			stl_log->safe_string_replace(last_logger_line);

		auto last_warning_line = logger_ptr->warning->getLast();
		if (last_warning_line != stl_warn->current_text)
			stl_warn->safe_string_replace(last_warning_line);

		auto last_error_line = logger_ptr->error->getLast();
		if (last_error_line != stl_err->current_text)
			stl_err->safe_string_replace(last_error_line);

		if (processing != SMRP.second->processing)
			processing = SMRP.second->processing;
		if (finished != SMRP.second->finished)
			finished = SMRP.second->finished;
		auto t = std::to_string((SMRP.second->last_input_position * 100.) / (SMRP.first->settings.details.initial_filesize)).substr(0, 5) + "%";

		if (hovered && processing)
		{
			t = SMRP.first->appearance_filename.substr(0, 30) + " " + t;
			stl_info->safe_color_change(0x9FCFFFFF);
		}
		else
		{
			stl_info->safe_color_change(0xFFFFFFFF);
		}

		if (stl_info->current_text != t)
			stl_info->safe_string_replace(t);
	}

	void draw() override
	{
		std::lock_guard locker(lock);
		if (!SMRP.first || !SMRP.second)
		{
			if (stl_err->current_text != "SMRP is NULL!")
				stl_err->safe_string_replace("SMRP is NULL!");
		}
		else
		{
			if (stl_err->current_text == "SMRP is NULL!")
				stl_err->safe_string_replace("_");
			update_info();
		}

		this->stl_err->draw();
		this->stl_warn->draw();
		this->stl_log->draw();
		this->stl_info->draw();

		if (processing && !finished)
			special_signs::draw_wait(x_pos, y_pos, 15, 0x007FFF7F, 20);
		else
		{
			if (finished)
				special_signs::draw_ok(x_pos, y_pos, 20, 0x00FFFFFF);
			else special_signs::draw_no(x_pos, y_pos, 20, 0xFF3F00FF);
		}
	}

	[[nodiscard]] bool mouse_handler(float mx, float my, char, char) override
	{
		std::lock_guard locker(lock);
		mx -= x_pos;
		my -= y_pos;
		if (mx * mx + my * my < 900)
			hovered = true;
		else
			hovered = false;
		return false;
	}
};

#endif // !SAFGUIF_L_SMRPV
