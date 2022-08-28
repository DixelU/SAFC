#pragma once
#ifndef SAFGUIF_L_SMRPV
#define SAFGUIF_L_SMRPV

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"

struct SMRP_Vis : HandleableUIPart 
{
	std::pair<
		MIDICollectionThreadedMerger::proc_data_ptr, 
		MIDICollectionThreadedMerger::message_buffer_ptr> SMRP;

	float _xpos, _ypos;
	bool _processing, _finished, _hovered;

	std::unique_ptr<SingleTextLine> _stl_log, _stl_warn, _stl_err, _stl_info;

	SMRP_Vis(float XPos, float YPos, SingleTextLineSettings* STLS)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->_processing = this->_hovered = this->_finished = 0;
		this->_xpos = XPos;
		this->_ypos = YPos;
		auto base_rgba = STLS->RGBAColor;
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos - 20);
		this->_stl_log.reset(STLS->CreateOne("_"));
		STLS->RGBAColor = 0xFFFF00FF;
		STLS->SetNewPos(XPos, YPos - 30);
		this->_stl_warn.reset(STLS->CreateOne("_"));
		STLS->RGBAColor = 0xFF3F00FF;
		STLS->SetNewPos(XPos, YPos - 40);
		this->_stl_err.reset(STLS->CreateOne("_"));
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos + 40);
		this->_stl_info.reset(STLS->CreateOne("_"));
		STLS->RGBAColor = base_rgba;
	}
	~SMRP_Vis() override 
	{
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		_xpos += dx;
		_ypos += dy;
		_stl_err->SafeMove(dx, dy);
		_stl_warn->SafeMove(dx, dy);
		_stl_log->SafeMove(dx, dy);
		_stl_info->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override 
	{
		NewX -= _xpos;
		NewY -= _ypos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override 
	{
		return;
	}
	void KeyboardHandler(CHAR CH) override
	{
		return;
	}
	void SafeStringReplace(std::string Meaningless) override
	{
		return;
	}
	void SetInfoString(std::string NewInfoString) 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->_stl_info->SafeStringReplace(NewInfoString);
	}
	void SetSMRP(std::pair<
		MIDICollectionThreadedMerger::proc_data_ptr,
		MIDICollectionThreadedMerger::message_buffer_ptr>& smrp)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		SMRP = smrp;
	}
	void UpdateInfo()
	{
		if (!SMRP.first || !SMRP.second)
			return;

		std::lock_guard<std::recursive_mutex> locker(Lock);
		auto& logger_ptr = SMRP.second;

		auto last_logger_line = logger_ptr->log->getLast();
		if (last_logger_line != _stl_log->_CurrentText)
			_stl_log->SafeStringReplace(last_logger_line);

		auto last_warning_line = logger_ptr->warning->getLast();
		if (last_warning_line != _stl_warn->_CurrentText)
			_stl_warn->SafeStringReplace(last_warning_line);

		auto last_error_line = logger_ptr->error->getLast();
		if (last_error_line != _stl_err->_CurrentText)
			_stl_err->SafeStringReplace(last_error_line);

		if (_processing != SMRP.second->processing)
			_processing = SMRP.second->processing;
		if (_finished != SMRP.second->finished)
			_finished = SMRP.second->finished;
		auto T = std::to_string((SMRP.second->last_input_position * 100.) / (SMRP.first->settings.details.initial_filesize)).substr(0, 5) + "%";

		if (_hovered && _processing)
		{
			T = SMRP.first->appearance_filename.substr(0, 30) + " " + T;
			_stl_info->SafeColorChange(0x9FCFFFFF);
		}
		else
		{
			_stl_info->SafeColorChange(0xFFFFFFFF);
		}

		if (_stl_info->_CurrentText != T)
			_stl_info->SafeStringReplace(T);
	}
	void Draw() override 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!SMRP.first || !SMRP.second)
		{
			if (_stl_err->_CurrentText != "SMRP is NULL!")
				_stl_err->SafeStringReplace("SMRP is NULL!");
		}
		else
		{
			if (_stl_err->_CurrentText == "SMRP is NULL!")
				_stl_err->SafeStringReplace("_");
			UpdateInfo();
		}

		this->_stl_err->Draw();
		this->_stl_warn->Draw();
		this->_stl_log->Draw();
		this->_stl_info->Draw();

		if (_processing && !_finished)
			SpecialSigns::DrawWait(_xpos, _ypos, 15, 0x007FFF7F, 20);
		else 
		{
			if (_finished)
				SpecialSigns::DrawOK(_xpos, _ypos, 20, 0x00FFFFFF);
			else SpecialSigns::DrawNo(_xpos, _ypos, 20, 0xFF3F00FF);
		}
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		mx -= _xpos;
		my -= _ypos;
		if (mx * mx + my * my < 900)
			_hovered = 1;
		else
			_hovered = 0;
		return 0;
	}
};

#endif // !SAFGUIF_L_SMRPV
