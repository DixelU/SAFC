#pragma once
#ifndef SAFGUIF_L_G
#define SAFGUIF_L_G

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"
/*
#ifndef __X64
using local_fp_type = float;
constexpr float __epsilon = FLT_EPSILON;
#else
*/
using local_fp_type = double;
constexpr double __epsilon = DBL_EPSILON;
//#endif

template<typename ordered_map_type = std::map<int, int>>
struct Graphing : HandleableUIPart {
	float CXpos, CYpos;
	float MYpos, MXpos;
	local_fp_type HorizontalScaling, CentralPoint;
	local_fp_type AssignedXSelection_ByKey;
	ordered_map_type* Graph;
	SingleTextLine* STL_Info;
	local_fp_type Width, TargetHeight, ScaleCoef, Shift;
	BIT AutoAdjusting, IsHovered;
	DWORD Color, NearestLineColor, PointColor, SelectionColor;
	Graphing(float CXpos, float CYpos, float Width, float TargetHeight, float ScaleCoef, BIT AutoAdjusting, DWORD Color, DWORD TextColor, DWORD NearestLineColor, DWORD PointColor, DWORD SelectionColor, ordered_map_type* Graph, SingleTextLineSettings* STLS, BIT Enabled) :
		CXpos(CXpos), CYpos(CYpos), Width(Width), TargetHeight(TargetHeight), ScaleCoef(ScaleCoef), Color(Color), AutoAdjusting(AutoAdjusting), Graph(Graph), NearestLineColor(NearestLineColor), PointColor(PointColor), SelectionColor(SelectionColor)
	{
		this->Enabled = Enabled;
		HorizontalScaling = 1;
		CentralPoint = 0;
		IsHovered = false;
		MYpos = MXpos = Shift = 0;
		AssignedXSelection_ByKey = 0;
		STLS->RGBAColor = TextColor;
		STLS->SetNewPos(CXpos, CYpos - 0.5f * TargetHeight + 5.f);
		STL_Info = STLS->CreateOne("_");
	}
	~Graphing() override {
		delete STL_Info;
	}
	void Reset() {
		HorizontalScaling = 1;
		CentralPoint = 0;
		IsHovered = false;
	}
	void Draw() override {
		Lock.lock();
		if (Enabled && Graph && Graph->size()) {
			local_fp_type begin = Graph->begin()->first;
			local_fp_type end = Graph->rbegin()->first;
			local_fp_type max_value = -1e31f, min_value = 1e31f;
			local_fp_type prev_value = CYpos - 0.5 * TargetHeight + (ScaleCoef * Graph->begin()->second + Shift) * TargetHeight;
			local_fp_type t_prev_value = prev_value;
			BIT IsFirstLine = true;
			BIT IsLastLoopComplete = false;
			GLCOLOR(Color);
			glBegin(GL_LINE_STRIP);
			for (auto T : *Graph) {
				IsLastLoopComplete = false;
				local_fp_type cur_pos = T.first;
				local_fp_type cur_value = T.second;
				local_fp_type cur_y = ScaleCoef * (cur_value)+Shift;
				local_fp_type cur_x = ((cur_pos - begin) / (end - begin) - 0.5f + CentralPoint) * HorizontalScaling;
				t_prev_value = std::clamp(prev_value, CYpos - 0.5f * TargetHeight, CYpos + 0.5f * TargetHeight);
				prev_value = CYpos - 0.5f * TargetHeight + cur_y * TargetHeight;
				if (cur_x < -0.5f)
					continue;
				if (cur_x > 0.5f)
					break;
				if (max_value < cur_y)
					max_value = cur_y;
				if (min_value > cur_y)
					min_value = cur_y;
				if (IsFirstLine) {
					IsFirstLine = false;
					glVertex2f(CXpos - 0.5f * Width, t_prev_value);
				}
				glVertex2f(CXpos + cur_x * Width, t_prev_value);
				glVertex2f(CXpos + cur_x * Width, prev_value);
				IsLastLoopComplete = true;
			}
			glVertex2f(CXpos + 0.5f * Width, (IsLastLoopComplete) ? prev_value : t_prev_value);
			glEnd();
			if (AssignedXSelection_ByKey > 0.5f) {
				GLCOLOR(SelectionColor);
				local_fp_type last_tempo_pos_x = ((AssignedXSelection_ByKey - begin) / (end - begin) - 0.5f + CentralPoint) * HorizontalScaling;
				if (last_tempo_pos_x >= -0.5f) {
					last_tempo_pos_x = ((last_tempo_pos_x < 0.5f) ? last_tempo_pos_x : 0.5f) * Width;
					glBegin(GL_POLYGON);
					glVertex2f(CXpos - 0.5f * Width, CYpos + TargetHeight * 0.5f);
					glVertex2f(CXpos + last_tempo_pos_x, CYpos + TargetHeight * 0.5f);
					glVertex2f(CXpos + last_tempo_pos_x, CYpos - TargetHeight * 0.5f);
					glVertex2f(CXpos - 0.5f * Width, CYpos - TargetHeight * 0.5f);
					glEnd();
				}
			}
			if (AutoAdjusting) {
				if (min_value != max_value) {
					Shift = (Shift - min_value);
					ScaleCoef = std::max(ScaleCoef / (max_value - min_value), __epsilon*4);
				}
			}
			//std::cout << "min: " << min_value << " max: " << max_value << " shift: " << Shift << " ScaleC: " << ScaleCoef << std::endl;
			if (IsHovered) {
				local_fp_type cur_x = (((MXpos - CXpos) / Width) / HorizontalScaling - CentralPoint + 0.5f) * (end - begin) + begin;
				glLineWidth(1);
				GLCOLOR(Color);
				glBegin(GL_LINE_LOOP);
				glVertex2f(CXpos - 0.5f * Width, CYpos + TargetHeight * 0.5f);
				glVertex2f(CXpos + 0.5f * Width, CYpos + TargetHeight * 0.5f);
				glVertex2f(CXpos + 0.5f * Width, CYpos - TargetHeight * 0.5f);
				glVertex2f(CXpos - 0.5f * Width, CYpos - TargetHeight * 0.5f);
				glEnd();
				glBegin(GL_LINES);
				glVertex2f(MXpos, CYpos - TargetHeight * 0.5f);
				glVertex2f(MXpos, CYpos + TargetHeight * 0.5f);
				auto equal_u_bound = Graph->upper_bound(cur_x);
				auto lesser_one = equal_u_bound;
				if (equal_u_bound != Graph->begin())
					lesser_one--;
				local_fp_type last_tempo_pos_x = ((lesser_one->first - begin) / (end - begin) - 0.5f + CentralPoint) * HorizontalScaling;
				local_fp_type last_tempo_pos_y = CYpos - 0.5f * TargetHeight + (ScaleCoef * lesser_one->second + Shift) * TargetHeight;
				if (abs(last_tempo_pos_x) <= 0.5f) {
					last_tempo_pos_x = last_tempo_pos_x * Width + CXpos;
					GLCOLOR(NearestLineColor);
					glLineWidth(3);
					glVertex2f(last_tempo_pos_x, CYpos - TargetHeight * 0.5f);
					glVertex2f(last_tempo_pos_x, CYpos + TargetHeight * 0.5f);
					glEnd();
					GLCOLOR(PointColor);
					glPointSize(3);
					glBegin(GL_POINTS);
					glVertex2f(last_tempo_pos_x, last_tempo_pos_y);
					glEnd();
				}
				STL_Info->SafeStringReplace(std::to_string(lesser_one->first) + " : " + std::to_string(lesser_one->second));
			}
			else
				if (STL_Info->_CurrentText != "-:-")
					STL_Info->SafeStringReplace("-:-");
		}
		else if (Enabled) {
			if (STL_Info->_CurrentText != "NULL Graph")
				STL_Info->SafeStringReplace("NULL Graph");
		}
		else {
			glColor4f(0.f, 0.f, 0.f, 0.15f);
			glBegin(GL_POLYGON);
			glVertex2f(CXpos - 0.5f * Width, CYpos + TargetHeight * 0.5f);
			glVertex2f(CXpos + 0.5f * Width, CYpos + TargetHeight * 0.5f);
			glVertex2f(CXpos + 0.5f * Width, CYpos - TargetHeight * 0.5f);
			glVertex2f(CXpos - 0.5f * Width, CYpos - TargetHeight * 0.5f);
			glEnd();
			if (STL_Info->_CurrentText != "Graph disabled")
				STL_Info->SafeStringReplace("Graph disabled");
		}
		STL_Info->Draw();
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		CXpos += dx;
		CYpos += dy;
		STL_Info->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= CXpos;
		NewY -= CYpos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * Width,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * TargetHeight;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		if (!IsHovered || !Enabled)
			return;
		Lock.lock();
		switch (CH) {
		case 'W':
		case 'w':
			HorizontalScaling *= 1.1f;
			if (HorizontalScaling < 1)
				HorizontalScaling = 1;
			break;
		case 'S':
		case 's':
			HorizontalScaling /= 1.1f;
			if (HorizontalScaling < 1)
				HorizontalScaling = 1;
			break;
		case 'D':
		case 'd':
			CentralPoint -= 0.05f / HorizontalScaling;
			break;
		case 'A':
		case 'a':
			CentralPoint += 0.05f / HorizontalScaling;
			break;
		case 'r':
		case 'R':
			CentralPoint = 0;
			HorizontalScaling = 1;
			ScaleCoef = 0.001f;
			Shift = 0;
		}
		Lock.unlock();
		return;
	}
	void SafeStringReplace(std::string Meaningless) override {
		return;
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		if (abs(mx - CXpos) <= 0.5f * Width && abs(my - CYpos) <= 0.5f * TargetHeight) {
			IsHovered = true;
		}
		else
			IsHovered = false;
		MXpos = mx;
		MYpos = my;
		Lock.unlock();
		return 0;
	}
};

#endif 