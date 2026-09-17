#pragma once

#include "PlaylistContent.h"
#include "spotify/api/SpotifyApiTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class PlaylistPageSource { LikedSongs, Playlist, Album, Podcast, Unsupported };

struct PlaylistPageToken {
	int64_t generation = 0;
	int64_t requestId = 0;
};

struct PlaylistPageRequest {
	PlaylistPageSource source = PlaylistPageSource::Unsupported;
	std::string id;
	int32_t offset = 0;
	int32_t limit = 50;
	int32_t searchGeneration = -1;
	std::string albumName = "Album";
	bool headRefresh = false;
	PlaylistPageToken token = {};
};

// Presentation data retains source numbering even when unavailable tracks are
// omitted. Podcast placeholders keep their position, with localization in UI glue.
struct PlaylistPageRow {
	int32_t number = 0;
	std::string title;
	std::string uri;
	std::string duration;
	std::string artist;
	std::string artistUri;
	std::string album;
	std::string albumUri;
	std::string description;
	std::string date;
	bool unavailable = false;
};

struct PlaylistPageResult {
	PlaylistPageRequest request;
	bool ok = false;
	bool responseValid = true;
	int32_t status = -1;
	int32_t retryAfter = -1;
	int32_t total = -1;
	int32_t pageCount = 0;
	int32_t nextOffset = 0;
	std::vector<PlaylistPageRow> rows;
};

PlaylistPageRequest MakePlaylistPageRequest(const PlaylistContentTarget& target,
	int32_t offset, int32_t limit);
int64_t PlaylistPageRetryDelay(int32_t status, int32_t retryAfter,
	int32_t retryCount, bool responseValid);

class PlaylistPageController {
public:
	using CollectionGetter = std::function<void(int32_t, int32_t, JsonCallback)>;
	using ContentGetter = std::function<void(const std::string&, int32_t,
		int32_t, JsonCallback)>;
	using Completion = std::function<void(const PlaylistPageResult&)>;

	PlaylistPageController(CollectionGetter likedSongs, ContentGetter playlist,
		ContentGetter album, ContentGetter podcast);
	// False means unsupported/invalid input, with no request or callback. Async
	// callbacks own copied request data and never refer to this controller.
	bool Load(const PlaylistPageRequest& request, Completion complete) const;

private:
	CollectionGetter fLikedSongs;
	ContentGetter fPlaylist;
	ContentGetter fAlbum;
	ContentGetter fPodcast;
};
