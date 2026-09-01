#pragma once

#include "PlaylistCacheDocument.h"

#include <SupportDefs.h>

#include <string>

struct PlaylistEpisode {
	int32		number = 0;
	std::string	title;
	std::string	description;
	std::string	date;
	std::string	duration;
	std::string	trackUri;
	std::string	searchText;
};

PlaylistEpisode MakePlaylistEpisode(int32 number, const std::string& title,
	const std::string& description, const std::string& date,
	const std::string& duration, const std::string& trackUri);
PlaylistEpisode PlaylistEpisodeFromCache(
	const PlaylistCacheDocument::Episode& cached);
PlaylistCacheDocument::Episode CacheEpisodeFromPlaylistEpisode(
	const PlaylistEpisode& episode);
std::string NormalizePlaylistEpisodeFilter(const std::string& filter);
bool PlaylistEpisodeMatchesFilter(const PlaylistEpisode& episode,
	const std::string& normalizedFilter);
