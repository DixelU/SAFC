#pragma once
#ifndef SAFGUIF_STLSPRESETS
#define SAFGUIF_STLSPRESETS

#include "single_text_line_settings.h"

SingleTextLineSettings
																			* _STLS_WhiteSmall = new SingleTextLineSettings("_", 0, 0, 5, 0xFFFFFFFF),
																			* _STLS_BlackSmall = new SingleTextLineSettings("_", 0, 0, 5, 0x000000FF),

					* System_Black = (is_fonted) ? new SingleTextLineSettings(10, 0x000000FF) :	 new SingleTextLineSettings("_", 0, 0, 5, 0x000000FF),
					* System_White = (is_fonted) ? new SingleTextLineSettings(10, 0xFFFFFFFF) :	 new SingleTextLineSettings("_", 0, 0, 5, 0xFFFFFFFF),
					* System_Red =	(is_fonted) ? new SingleTextLineSettings(10, 0xFF7F3FFF) :	 new SingleTextLineSettings("_", 0, 0, 5, 0xFF7F3FFF),
					* System_Blue = (is_fonted) ? new SingleTextLineSettings(10, 0x9FCFFFFF) :	 new SingleTextLineSettings("_", 0, 0, 5, 0x9FCFFFFF);
#endif