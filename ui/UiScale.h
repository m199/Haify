#pragma once

#include <Font.h>

#include <algorithm>
#include <cmath>

namespace UiScale {

constexpr float kBaseFontSize = 12.0f;

inline float
FontScale()
{
	return std::max(1.0f, be_plain_font->Size() / kBaseFontSize);
}


inline float
LineHeight(const BFont* font = be_plain_font)
{
	font_height height;
	font->GetHeight(&height);
	return std::ceil(height.ascent + height.descent + height.leading);
}


inline float
Scaled(float value, float scale)
{
	return std::ceil(value * std::max(1.0f, scale));
}


inline float
Scaled(float value)
{
	return Scaled(value, FontScale());
}

}
