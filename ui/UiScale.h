#pragma once

#include "ui/UiScalePolicy.h"
#include <Font.h>

namespace UiScale {

inline float
FontScale()
{
	return FontScale(be_plain_font->Size());
}


inline float
LineHeight(const BFont* font = be_plain_font)
{
	font_height height;
	font->GetHeight(&height);
	return std::ceil(height.ascent + height.descent + height.leading);
}


inline float
Scaled(float value)
{
	return Scaled(value, FontScale());
}

}
