#pragma once

#include "SettingsController.h"

#include <InterfaceDefs.h>

inline rgb_color
HaifyDropMarkerColor()
{
	HaifySettings settings = SettingsController::Load();
	if (settings.seekBarUseSystemColor)
		return ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	return (rgb_color) {
		(uint8)settings.seekBarColorRed,
		(uint8)settings.seekBarColorGreen,
		(uint8)settings.seekBarColorBlue,
		(uint8)settings.seekBarColorAlpha
	};
}
