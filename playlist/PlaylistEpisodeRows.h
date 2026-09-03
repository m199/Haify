#pragma once

#include "PlaylistEpisode.h"
#include "PlaylistTrackRow.h"

#include <SupportDefs.h>

#include <string>
#include <vector>

class BMessage;

TrackRow* PlaylistEpisodeRowFromEpisode(const PlaylistEpisode& episode,
	const std::string& currentPlayingTrackUri);
std::vector<PlaylistEpisode> PlaylistEpisodesFromMessage(BMessage* message);
