#pragma once

#include "PlaylistCacheDocument.h"
#include "PlaylistTrackRow.h"

#include <SupportDefs.h>

#include <string>
#include <vector>

class BColumnListView;

PlaylistCacheDocument::Track CachedTrackFromRow(TrackRow* row,
	int32 rowIndex);
TrackRow* CachedTrackRowFromCache(const PlaylistCacheDocument::Track& track,
	const std::string& currentPlayingTrackUri);
void AddCachedTrackRows(const std::vector<PlaylistCacheDocument::Track>& tracks,
	BColumnListView* trackList, const std::string& currentPlayingTrackUri);
