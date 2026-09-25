#pragma once

#include <algorithm>
#include <cmath>

namespace UiScale {

constexpr float kBaseFontSize = 12.0f;

// Match Haiku's BRow default height; do not scale this a second time.
inline float
ListRowHeight(float fontSize)
{
	return std::max(16.0f, std::ceil(fontSize * 1.4f));
}

inline float
FontScale(float fontSize)
{
	return std::max(1.0f, fontSize / kBaseFontSize);
}

inline float
Scaled(float value, float scale)
{
	return std::ceil(value * std::max(1.0f, scale));
}

} // namespace UiScale
