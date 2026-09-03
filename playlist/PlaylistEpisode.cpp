#include "PlaylistEpisode.h"

#include <cctype>

namespace {

std::string
LowercaseSearchText(const std::string& title, const std::string& description)
{
	std::string text = title + "\n" + description;
	for (char& character : text)
		character = (char)tolower((unsigned char)character);
	return text;
}

}


PlaylistEpisode
MakePlaylistEpisode(int32 number, const std::string& title,
	const std::string& description, const std::string& date,
	const std::string& duration, const std::string& trackUri)
{
	PlaylistEpisode episode;
	episode.number = number;
	episode.title = title;
	episode.description = description;
	episode.date = date;
	episode.duration = duration;
	episode.trackUri = trackUri;
	episode.searchText = LowercaseSearchText(title, description);
	return episode;
}


PlaylistEpisode
PlaylistEpisodeFromCache(const PlaylistCacheDocument::Episode& cached)
{
	return MakePlaylistEpisode(cached.number, cached.title, cached.description,
		cached.date, cached.duration, cached.uri);
}


PlaylistCacheDocument::Episode
CacheEpisodeFromPlaylistEpisode(const PlaylistEpisode& episode)
{
	PlaylistCacheDocument::Episode cached;
	cached.number = episode.number;
	cached.title = episode.title;
	cached.description = episode.description;
	cached.date = episode.date;
	cached.duration = episode.duration;
	cached.uri = episode.trackUri;
	return cached;
}


std::string
NormalizePlaylistEpisodeFilter(const std::string& filter)
{
	std::string normalized = filter;
	for (char& character : normalized)
		character = (char)tolower((unsigned char)character);
	return normalized;
}


bool
PlaylistEpisodeMatchesFilter(const PlaylistEpisode& episode,
	const std::string& normalizedFilter)
{
	return normalizedFilter.empty()
		|| episode.searchText.find(normalizedFilter) != std::string::npos;
}


static bool
PlaylistEpisodeMatchesIdentity(const PlaylistEpisode& episode,
	const PlaylistEpisode& candidate)
{
	if (!candidate.trackUri.empty())
		return episode.trackUri == candidate.trackUri;
	return episode.trackUri.empty() && episode.title == candidate.title
		&& episode.date == candidate.date
		&& episode.duration == candidate.duration;
}


bool
PlaylistEpisodeListContains(const std::vector<PlaylistEpisode>& episodes,
	const PlaylistEpisode& candidate)
{
	for (const PlaylistEpisode& episode : episodes) {
		if (PlaylistEpisodeMatchesIdentity(episode, candidate))
			return true;
	}
	return false;
}


size_t
AppendMissingPlaylistEpisodes(std::vector<PlaylistEpisode>& episodes,
	const std::vector<PlaylistEpisode>& incoming)
{
	size_t firstNewEpisode = episodes.size();
	for (const PlaylistEpisode& episode : incoming) {
		if (!PlaylistEpisodeListContains(episodes, episode))
			episodes.push_back(episode);
	}
	return firstNewEpisode;
}


bool
CollectMissingPlaylistHeadEpisodes(
	const std::vector<PlaylistEpisode>& existing,
	const std::vector<PlaylistEpisode>& incoming,
	std::vector<PlaylistEpisode>& pending)
{
	for (const PlaylistEpisode& episode : incoming) {
		if (PlaylistEpisodeListContains(existing, episode))
			return true;
		pending.push_back(episode);
	}
	return false;
}
