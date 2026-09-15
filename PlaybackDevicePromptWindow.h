#pragma once

#include "PlaybackDeviceResolver.h"
#include "Messages.h"

#include <Messenger.h>
#include <Rect.h>
#include <SupportDefs.h>

#include <vector>

void ShowPlaybackDevicePrompt(BMessenger target,
	const std::vector<PlaybackDeviceChoice>& devices, bool librespotRunning,
	BRect ownerFrame);
