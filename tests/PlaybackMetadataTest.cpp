#include "playback/PlaybackMetadata.h"

#include <cstdio>
#include <cstdlib>

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)


using Source = PlaybackMetadataSource;

static PlaybackMetadata
CurrentBook()
{
	PlaybackMetadata current;
	current.title.value = "Chapter one";
	current.artist.value = "Book";
	current.itemKind.value = "chapter";
	current.openUri.value = "spotify:audiobook:book";
	current.parentUri = current.openUri;
	current.parentKind.value = "audiobook";
	current.audiobookId.value = "book";
	current.artworkUrl.value = "book-cover";
	return current;
}


static void
CheckSameItemAndEmptyState()
{
	PlaybackMetadata current;
	current.title.value = "Known title";
	current.artist.value = "Known artist";
	current.albumId.value = "album";
	current.artistId.value = "artist";
	current.artworkUrl.value = "old-cover";
	auto result = ResolvePlaybackMetadata({}, current, {"spotify:track:one"});
	Check(result.title.value == "Known title" && result.title.source == Source::Retained);
	Check(result.artistId.value == "artist" && result.albumId.value == "album");
	Check(result.artworkUrl.value.empty());
	Check(result.openUri.value == "spotify:track:one"
		&& result.openUri.source == Source::ItemUriFallback);
	result = ResolvePlaybackMetadata({}, current, {"", false, true});
	StorePlaybackMetadata(current, result, false);
	Check(current.title.value.empty() && current.artist.value.empty());
	Check(current.albumId.value.empty() && current.artistId.value.empty());
	// Artwork is committed separately at the owner's publication point.
	Check(current.artworkUrl.value == "old-cover");
}


static void
CheckOptimisticAndArtwork()
{
	PlaybackMetadata current;
	current.title.value = "Previous";
	current.artworkUrl.value = "previous-cover";
	PlaybackMetadata reported;
	reported.title.value = "Next";
	auto result = ResolvePlaybackMetadata(reported, current,
		{"spotify:track:next", true, true});
	Check(result.title.value == "Next" && result.title.source == Source::Reported);
	Check(result.artworkUrl.value == "previous-cover"
		&& result.artworkUrl.source == Source::Retained);
	StorePlaybackMetadata(current, {}, true);
	Check(current.title.value == "Previous");
	reported.artworkUrl.value = "reported-cover";
	result = ResolvePlaybackMetadata(reported, current,
		{"spotify:track:next", false, true, true});
	Check(result.artworkUrl.value == "previous-cover"
		&& result.artworkUrl.source == Source::Retained);
	result = ResolvePlaybackMetadata(reported, current,
		{"spotify:track:next", false, true, false});
	Check(result.artworkUrl.value == "reported-cover"
		&& result.artworkUrl.source == Source::Reported);
}


static void
CheckBookContext()
{
	const auto current = CurrentBook();
	PlaybackMetadata reported;
	reported.title.value = "Updated chapter";
	reported.artist.value = "API subtitle";
	reported.itemKind.value = "episode";
	reported.openUri.value = "spotify:episode:one";
	reported.albumId.value = "stale-album";
	reported.artistId.value = "stale-artist";
	auto result = ResolvePlaybackMetadata(reported, current, {"spotify:episode:one"});
	Check(result.title.value == "Updated chapter" && result.artist.value == "Book");
	Check(result.itemKind.value == "chapter" && result.itemKind.source == Source::Retained);
	Check(result.openUri.value == current.openUri.value && result.audiobookId.value == "book");
	Check(result.albumId.value.empty() && result.artistId.source == Source::ClearedForItem);
	result = ResolvePlaybackMetadata(reported, current, {"spotify:episode:two", false, true});
	Check(result.artist.value == "API subtitle" && result.itemKind.value == "episode");
	Check(result.parentKind.value == "audiobook" && result.audiobookId.source == Source::Retained);
	// A reported show prevents carrying a previous book into a podcast chapter.
	reported.openUri.value = "spotify:show:podcast";
	result = ResolvePlaybackMetadata(reported, current, {"spotify:episode:two", false, true});
	Check(result.openUri.value == "spotify:show:podcast" && result.audiobookId.value.empty());
}


static void
CheckIdClearingOnOptimisticStore()
{
	PlaybackMetadata current;
	current.albumId.value = "album";
	current.artistId.value = "artist";
	PlaybackMetadata reported;
	reported.itemKind.value = "episode";
	reported.openUri.value = "spotify:show:podcast";
	auto result = ResolvePlaybackMetadata(reported, current,
		{"spotify:episode:one", true, true});
	StorePlaybackMetadata(current, result, true);
	Check(current.albumId.value.empty() && current.artistId.value.empty());
	Check(current.albumId.source == Source::ClearedForItem);
}


int
main()
{
	CheckSameItemAndEmptyState();
	CheckOptimisticAndArtwork();
	CheckBookContext();
	CheckIdClearingOnOptimisticStore();
	std::puts("Playback metadata tests passed");
	return 0;
}
