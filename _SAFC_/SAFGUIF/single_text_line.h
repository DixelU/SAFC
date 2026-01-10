#pragma once
#ifndef SAFGUIF_STL 
#define SAFGUIF_STL

#include "header_utils.h"
#include "symbols.h"


struct SingleTextLine 
{
	std::string _CurrentText;
	float CXpos, CYpos;
	float SpaceWidth;
	std::uint32_t RGBAColor, gRGBAColor;
	float CalculatedWidth, CalculatedHeight;
	bool isBicolored, isListedFont;
	float _XUnitSize, _YUnitSize;
	std::vector<DottedSymbol*> Chars;

	~SingleTextLine() 
	{
		for (auto i = Chars.begin(); i != Chars.end(); ++i)
			delete *i;
		Chars.clear();
	}
	SingleTextLine(const SingleTextLine&) = delete;
	SingleTextLine(std::string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, float SpaceWidth, std::uint8_t LineWidth = 2, std::uint32_t RGBAColor = 0xFFFFFFFF, std::uint32_t* RGBAGradColor = NULL, std::uint8_t OrigNGradPoints = ((5 << 4) | 5), bool isListedFont = false) 
	{
		if (!Text.size())Text = " ";
		this->_CurrentText = Text;
		CalculatedHeight = 2 * YUnitSize;
		CalculatedWidth = Text.size() * 2.f * XUnitSize + (Text.size() - 1) * SpaceWidth;
		this->CXpos = CXpos;
		this->CYpos = CYpos;
		this->RGBAColor = RGBAColor;
		this->gRGBAColor = 0;
		if (RGBAGradColor)
			this->gRGBAColor = *RGBAGradColor;
		this->SpaceWidth = SpaceWidth;
		this->_XUnitSize = XUnitSize;
		this->_YUnitSize = YUnitSize;
		this->isListedFont = isListedFont;
		float CharXPosition = CXpos - (CalculatedWidth * 0.5) + XUnitSize, CharXPosIncrement = 2.f * XUnitSize + SpaceWidth;
		for (int i = 0; i < Text.size(); i++)
		{
			if (!RGBAGradColor && !isListedFont)Chars.push_back(
				new DottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new std::uint32_t{RGBAColor})
				);
			else if (RGBAGradColor && !isListedFont) Chars.push_back(
				new BiColoredDottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new std::uint32_t{ RGBAColor }, new std::uint32_t{ this->gRGBAColor }, (std::uint8_t)(OrigNGradPoints >> 4), (std::uint8_t)(OrigNGradPoints & 0xF))
				);
			else Chars.push_back(
				new lFontSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, RGBAColor)
				);
			CharXPosition += CharXPosIncrement;
		}
		if (RGBAGradColor)
		{
			this->isBicolored = true;
			this->gRGBAColor = *RGBAGradColor;
			delete RGBAGradColor;
		}
		else
			this->isBicolored = false;

		RecalculateWidth();
	}
	void SafeColorChange(std::uint32_t NewRGBAColor)
	{
		if (isBicolored)
		{
			SafeColorChange(NewRGBAColor, gRGBAColor,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData >> 4,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData & 0xF
				);
			return;
		}
		std::uint8_t R = (NewRGBAColor >> 24), G = (NewRGBAColor >> 16) & 0xFF, B = (NewRGBAColor >> 8) & 0xFF, A = (NewRGBAColor) & 0xFF;
		RGBAColor = NewRGBAColor;
		for (int i = 0; i < Chars.size(); i++)
		{
			Chars[i]->R = R;
			Chars[i]->G = G;
			Chars[i]->B = B;
			Chars[i]->A = A;
		}
	}
	void SafeColorChange(std::uint32_t NewBaseRGBAColor, std::uint32_t NewGRGBAColor, std::uint8_t BasePoint, std::uint8_t gPoint)
	{
		if (!isBicolored)
			return SafeColorChange(NewBaseRGBAColor);
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->RefillGradient(new std::uint32_t{ NewBaseRGBAColor }, new std::uint32_t{ NewGRGBAColor }, BasePoint, gPoint);
	}
	void SafeChangePosition(float NewCXPos, float NewCYPos)
	{
		NewCXPos = NewCXPos - CXpos;
		NewCYPos = NewCYPos - CYpos;
		SafeMove(NewCXPos, NewCYPos);
	}
	void SafeMove(float dx, float dy)
	{
		CXpos += dx;
		CYpos += dy;
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->SafeCharMove(dx, dy);
	}
	bool SafeReplaceChar(int i, char CH)
	{
		if (i >= Chars.size()) return 0;
		if (isListedFont)
		{
			auto ch = dynamic_cast<lFontSymbol*>(Chars[i]);

			if (!ch)
			{
				std::cerr << "Error during safe char replacement [1]" << std::endl;
				return 0;
			}

			ch->Symb = CH;
			ch->ReinitGlyphMetrics();
		}
		else
		{
			Chars[i]->RenderWay = ASCII[CH];
			Chars[i]->UpdatePointPlacementPositions();
		}
		return 1;
	}
	bool SafeReplaceChar(int i, const std::string& CHrenderway)
	{
		if (i >= Chars.size())return 0;
		if (isListedFont) return 0;
		Chars[i]->RenderWay = CHrenderway;
		Chars[i]->UpdatePointPlacementPositions();
		return 1;
	}
	void RecalculateWidth()
	{
		CalculatedWidth = (isListedFont) ? 
			HorizontallyRepositionFontedSymbols() :
			LegacyCalculateWidthAndRepositionNonfonteds();
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float newX, float newY)
	{
		///STL_CHANGE_POSITION_ARGUMENT_LEFT
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * CalculatedWidth,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * CalculatedHeight;
		SafeChangePosition(newX + CW, newY + CH);
	}
	void SafeStringReplace(std::string NewString)
	{
		if (!NewString.size()) NewString = " ";
		_CurrentText = NewString;
		while (NewString.size() > Chars.size())
		{
			if (isBicolored)
				Chars.push_back(new BiColoredDottedSymbol((*((BiColoredDottedSymbol*)(Chars.front())))));
			else if (isListedFont)
				Chars.push_back(new lFontSymbol(*((lFontSymbol*)(Chars.front()))));
			else
				Chars.push_back(new DottedSymbol(*(Chars.front())));
		}
		while (NewString.size() < Chars.size())
		{
			delete Chars.back();
			Chars.pop_back();
		}
		for (int i = 0; i < Chars.size(); i++)
			SafeReplaceChar(i, NewString[i]);
		RecalculateWidth();
	}

	inline float DefaultWidthFormulae()
	{
		return Chars.size() * (2.f * _XUnitSize) + (Chars.size() - 1) * SpaceWidth;
	}

	float LegacyCalculateWidthAndRepositionNonfonteds()
	{
		auto width = DefaultWidthFormulae();
		float CharXPosition = CXpos - (width * 0.5f) + _XUnitSize, CharXPosIncrement = 2.f * _XUnitSize + SpaceWidth;

		for (auto& ch : Chars)
		{
			ch->Xpos = CharXPosition;
			CharXPosition += CharXPosIncrement;
		}
		return width;
	}

	float HorizontallyRepositionFontedSymbols()
	{
		float PixelSize = (internal_range * 2) / window_base_width;
		float TotalWidth = 0;
		ptrdiff_t TotalPixelWidth = 0;

		for (auto& ch : Chars)
		{
			auto fontedSymb = dynamic_cast<lFontSymbol*>(ch);
			if (!fontedSymb)
			{
				std::cerr << "Error during repositionment of a character [1]" << std::endl;
				return TotalPixelWidth * PixelSize;
			}
			TotalPixelWidth += fontedSymb->GM.gmCellIncX;
		}

		if (Chars.size())
		{
			auto lastChar = dynamic_cast<lFontSymbol*>(Chars.back());
			if (!lastChar)
			{
				std::cerr << "Error during repositionment of a character [2]" << std::endl;
				return TotalPixelWidth * PixelSize;
			}

			TotalPixelWidth -= ((ptrdiff_t)lastChar->GM.gmCellIncX - (ptrdiff_t)lastChar->GM.gmBlackBoxX);
		}

		TotalWidth = TotalPixelWidth * PixelSize;
		ptrdiff_t LinearPixelHorizontalPosition = 0;
		float LinearHorizontalPosition = CXpos - (TotalWidth * 0.5f);

		for (auto& ch : Chars)
		{
			auto fontedSymb = dynamic_cast<lFontSymbol*>(ch);
			fontedSymb->SafePositionChange(
				LinearHorizontalPosition + (LinearPixelHorizontalPosition * PixelSize),
				fontedSymb->Ypos);
			LinearPixelHorizontalPosition += fontedSymb->GM.gmCellIncX;
			/*std::cout << fontedSymb->Symb << "(" << std::hex << std::setw(2) << (int)fontedSymb->Symb << ")" << std::dec << std::setw(0)
				<< " gmCellIncX: " << fontedSymb->GM.gmCellIncX
				<< " gmBlackBoxX: " << fontedSymb->GM.gmBlackBoxX
				<< "\t gmptGlyphOrigin.x: " << fontedSymb->GM.gmptGlyphOrigin.x
				<< "\t LPHP: " << LinearPixelHorizontalPosition << std::endl;*/
		}
		/*std::cout << "Width: " << TotalPixelWidth << std::endl;
		std::cout << "====" << std::endl;*/

		return TotalWidth;
	}

	void Draw()
	{
		if (CalculatedWidth < std::numeric_limits<float>::epsilon())
			RecalculateWidth();

		for (auto& ch: Chars)
			ch->Draw();
	}
};

#endif