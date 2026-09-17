#include "playlist/PlaylistPresentation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#ifdef NDEBUG
#error Playlist presentation tests require assertions; compile without NDEBUG.
#endif

static void TestMenus()
{
	auto playlist = ResolvePlaylistContentTarget("spotify:playlist:list");
	for (int mask = 0; mask < 32; mask++) {
		bool owned = mask & 1, items = mask & 2, mutation = mask & 4;
		bool cover = mask & 8, deleting = mask & 16;
		auto state = ResolvePlaylistMenuState(playlist, owned, items, mutation, cover, deleting);
		assert(state.edit == (owned && !deleting));
		assert(state.cover == (owned && !deleting && !cover));
		assert(state.clear == (owned && items && !deleting && !mutation));
		assert(state.remove == (!mutation && !cover && !deleting));
		assert(state.owned == owned);
	}
	for (const char* uri : {"spotify:collection", "spotify:album:a", "spotify:show:s", "spotify:playlist:", "bad"}) {
		auto state = ResolvePlaylistMenuState(ResolvePlaylistContentTarget(uri), true, true, false, false, false);
		assert(!state.edit && !state.cover && !state.clear && !state.remove && !state.owned);
	}
}

static void TestHeaders()
{
	struct Case { const char* uri; const char* prefix; bool podcast; bool album; bool liked; };
	const Case cases[] = {
		{"spotify:collection", "", false, false, true},
		{"spotify:playlist:p", "Playlist: ", false, false, false},
		{"spotify:album:a", "Album: ", false, true, false},
		{"spotify:show:s", "Podcast: ", true, false, false},
		{"spotify:artist:a", "Artist: ", false, false, false},
		{"bad", "", false, false, false}
	};
	for (const auto& item : cases) {
		auto header = ResolvePlaylistHeader(item.uri);
		assert(std::string(header.titlePrefix) == item.prefix);
		assert(header.isPodcast == item.podcast && header.isAlbum == item.album && header.isLikedSongs == item.liked);
		assert(header.minimumWidth == (item.podcast ? 560 : 420));
		assert(header.minimumHeight == (item.podcast ? 300 : 260));
	}
	for (float scale : {1.0f, 1.5f, 2.0f}) {
		auto metrics = ResolvePodcastHeaderMetrics(20 * scale, 100 * scale, 170 * scale);
		assert(std::fabs(metrics.infoWidth - 170 * scale) < 0.01f);
		assert(std::fabs(metrics.titleHeight - 64 * scale) < 0.01f);
		assert(std::fabs(metrics.searchInfoHeight - 28 * scale) < 0.01f);
	}
	auto wideButton = ResolvePodcastHeaderMetrics(20, 250, 170);
	assert(wideButton.infoWidth == 250);
}

int main()
{
	TestMenus();
	TestHeaders();
	std::puts("Playlist presentation policy tests passed.");
}
