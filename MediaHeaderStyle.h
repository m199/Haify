#pragma once

#include "UiScale.h"

#include <Size.h>
#include <View.h>

namespace MediaHeaderStyle {

constexpr float kArtworkSize = 128.0f;
constexpr float kTitleScale = 1.5f;
constexpr float kActionButtonMinWidth = 170.0f;


inline float
Scaled(float value)
{
	return UiScale::Scaled(value);
}


inline float
ArtworkSize(float baseSize = kArtworkSize)
{
	return Scaled(baseSize);
}


inline float
ActionButtonMinWidth()
{
	return Scaled(kActionButtonMinWidth);
}


inline void
ApplyArtworkSize(BView* view, float baseSize = kArtworkSize)
{
	if (!view)
		return;
	float size = ArtworkSize(baseSize);
	view->SetExplicitMinSize(BSize(size, size));
	view->SetExplicitMaxSize(BSize(size, size));
	view->SetExplicitPreferredSize(BSize(size, size));
}

}
