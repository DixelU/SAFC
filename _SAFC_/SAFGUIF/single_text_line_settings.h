#pragma once
#ifndef SAFGUIF_STLS 
#define SAFGUIF_STLS

#include "header_utils.h"
#include "single_text_line.h"

#define CharWidthPerHeight_Fonted 0.666f
#define CharWidthPerHeight 0.5f
#define CharSpaceBetween(CharHeight) CharHeight/2.f
#define CharLineWidth(CharHeight) ceil(CharHeight/7.5f)
struct SingleTextLineSettings {
	std::string STLstring;
	float CXpos, CYpos, XUnitSize, YUnitSize;
	BYTE BasePoint, GradPoint, LineWidth, SpaceWidth;
	BIT isFonted;
	DWORD default_RGBAC, default_gRGBAC;
	DWORD RGBAColor, gRGBAColor;
	SingleTextLineSettings(std::string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, BYTE LineWidth, BYTE SpaceWidth, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint) {
		this->STLstring = Text;
		this->CXpos = CXpos;
		this->CYpos = CYpos;
		this->XUnitSize = XUnitSize;
		this->YUnitSize = YUnitSize;
		this->default_RGBAC = this->RGBAColor = RGBAColor;
		this->default_gRGBAC = this->gRGBAColor = gRGBAColor;
		this->LineWidth = LineWidth;
		this->SpaceWidth = SpaceWidth;
		this->BasePoint = BasePoint;
		this->GradPoint = GradPoint;
		this->isFonted = 0;
	}
	SingleTextLineSettings(std::string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, BYTE LineWidth, BYTE SpaceWidth, DWORD RGBAColor) :
		SingleTextLineSettings(Text, CXpos, CYpos, XUnitSize, YUnitSize, LineWidth, SpaceWidth, RGBAColor, 0, 255, 255) {}
	SingleTextLineSettings(std::string Text, float CXpos, float CYpos, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint) :
		SingleTextLineSettings(Text, CXpos, CYpos, CharHeight* CharWidthPerHeight / 2, CharHeight / 2, CharLineWidth(CharHeight), CharSpaceBetween(CharHeight), RGBAColor, gRGBAColor, BasePoint, GradPoint) {}
	SingleTextLineSettings(std::string Text, float CXpos, float CYpos, float CharHeight, DWORD RGBAColor) :
		SingleTextLineSettings(Text, CXpos, CYpos, CharHeight, RGBAColor, 0, 255, 255) {}
	SingleTextLineSettings(float XUnitSize, float YUnitSize, DWORD RGBAColor) :
		SingleTextLineSettings("_", 0, 0, YUnitSize, RGBAColor) {
		this->isFonted = 1;
		this->XUnitSize = XUnitSize;
		this->YUnitSize = YUnitSize;
		this->default_RGBAC = this->RGBAColor = RGBAColor;
		//this->default_gRGBAC = this->gRGBAColor = gRGBAColor;
	}
	SingleTextLineSettings(float CharHeight, DWORD RGBAColor) :
		SingleTextLineSettings(CharHeight* CharWidthPerHeight / 4, CharHeight / 2, RGBAColor) {}
	SingleTextLineSettings(SingleTextLine* Example, BIT KeepText = false) :
		SingleTextLineSettings(
		((KeepText) ? Example->_CurrentText : " "), Example->CXpos, Example->CYpos, Example->_XUnitSize, Example->_YUnitSize, Example->Chars.front()->LineWidth, Example->SpaceWidth, Example->RGBAColor, Example->gRGBAColor,
			((Example->isBicolored) ? (((BiColoredDottedSymbol*)(Example->Chars.front()))->_PointData & 0xF0) >> 4 : 0xF), ((Example->isBicolored) ? (((BiColoredDottedSymbol*)(Example->Chars.front()))->_PointData) & 0xF : 0xF)
			) {
		this->isFonted = Example->isListedFont;
	}
	SingleTextLine* CreateOne() {
		SingleTextLine* ptr = nullptr;
		if (GradPoint & 0xF0 && !isFonted)
			ptr = new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor);
		else if (!isFonted)
			ptr = new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, new DWORD(gRGBAColor), ((BasePoint & 0xF) << 4) | (GradPoint & 0xF));
		else
			ptr = new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, nullptr, 0xF, true);
		RGBAColor = default_RGBAC;
		gRGBAColor = default_gRGBAC;
		return ptr;
	}
	SingleTextLine* CreateOne(std::string TextOverride) {
		SingleTextLine* ptr = nullptr;
		if (GradPoint & 0xF0 && !isFonted)
			ptr = new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor);
		else if (!isFonted)
			ptr = new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, new DWORD(gRGBAColor), ((BasePoint & 0xF) << 4) | (GradPoint & 0xF));
		else
			ptr = new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, nullptr, 0xF, true);
		RGBAColor = default_RGBAC;
		gRGBAColor = default_gRGBAC;
		return ptr;
	}
	void SetNewPos(float NewXPos, float NewYPos) {
		this->CXpos = NewXPos;
		this->CYpos = NewYPos;
	}
	void Move(float dx, float dy) {
		this->CXpos += dx;
		this->CYpos += dy;
	}
};

#endif