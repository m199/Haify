#pragma once

#include "PlaybackDeviceResolver.h"

#include <Messenger.h>
#include <Rect.h>
#include <SupportDefs.h>

#include <vector>

static const uint32 kMsgPlaybackDeviceSelected = 'pbDs';
static const uint32 kMsgPlaybackDeviceStartLocal = 'pbDl';
static const uint32 kMsgPlaybackDevicePromptClosed = 'pbDx';

void ShowPlaybackDevicePrompt(BMessenger target,
	const std::vector<PlaybackDeviceChoice>& devices, bool librespotRunning,
	BRect ownerFrame);
