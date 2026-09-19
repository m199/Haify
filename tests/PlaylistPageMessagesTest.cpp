#include "playlist/PlaylistPageMessages.h"
#include "messages/Messages.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist page message tests require assertions; compile without NDEBUG.
#endif

static PlaylistPageResult
Page(PlaylistPageSource source)
{
	PlaylistPageResult result;
	result.request.source = source;
	result.request.id = "content";
	result.request.token = {4294967300LL, 4294967310LL};
	result.request.offset = 50;
	result.request.searchGeneration = 7;
	result.ok = true;
	result.total = 100;
	result.pageCount = 3;
	result.nextOffset = 53;
	PlaylistPageRow row;
	row.number = 52;
	row.title = "Title";
	row.uri = "spotify:track:item";
	row.duration = "1:23";
	row.artist = "Artist";
	row.artistUri = "spotify:artist:id";
	row.album = "Album";
	row.albumUri = "spotify:album:id";
	row.description = "Description";
	row.date = "2026-01-02";
	result.rows.push_back(row);
	return result;
}

static void
TestTrackWireCompatibility()
{
	auto page = Page(PlaylistPageSource::Playlist);
	BMessage message = MakePlaylistPageMessage(page, "Unavailable");
	assert(message.what == MSG_PLAYLIST_TRACK_PAGE && message.what == 'pLdt');
	assert(message.GetBool("append", false));
	int32 wrongType = 0;
	assert(message.FindInt32("append", &wrongType) != B_OK);
	assert(message.GetInt32("number", 0) == 52);
	assert(message.GetInt32("page_count", 0) == 3 && message.GetInt32("next_offset", 0) == 53);
	assert(message.GetInt32("total", 0) == 100);
	for (const auto& field : std::vector<std::pair<std::string, std::string>>{
		{"title", "Title"}, {"trackUri", "spotify:track:item"}, {"duration", "1:23"},
		{"artist", "Artist"}, {"artistUri", "spotify:artist:id"}, {"bpm", ""}, {"key", ""},
		{"album", "Album"}, {"albumUri", "spotify:album:id"}}) {
		const char* value = nullptr;
		assert(message.FindString(field.first.c_str(), 0, &value) == B_OK);
		assert(value && field.second == value);
		assert(message.FindString(field.first.c_str(), 1, &value) != B_OK);
	}
	page.request.offset = 0;
	message = MakePlaylistPageMessage(page, "Unavailable");
	assert(!message.GetBool("append", true));
}

static void
TestEpisodeWireCompatibility()
{
	auto page = Page(PlaylistPageSource::Podcast);
	PlaylistPageRow unavailable;
	unavailable.number = 53;
	unavailable.unavailable = true;
	page.rows.push_back(unavailable);
	BMessage message = MakePlaylistPageMessage(page, "Nicht verfuegbar");
	assert(message.what == MSG_PLAYLIST_EPISODE_PAGE && message.what == 'pEpL');
	assert(message.GetInt32("append", 0) == 1);
	bool wrongType = false;
	assert(message.FindBool("append", &wrongType) != B_OK);
	assert(std::string(message.GetString("description", "")) == "Description");
	assert(std::string(message.GetString("date", "")) == "2026-01-02");
	const char* text = nullptr;
	assert(message.FindString("title", 1, &text) == B_OK);
	assert(std::string(text) == "Nicht verfuegbar");
	for (const char* field : {"trackUri", "description", "date", "duration"}) {
		assert(message.FindString(field, 1, &text) == B_OK);
		assert(text && std::string(text).empty());
	}
	page.request.offset = 0;
	message = MakePlaylistPageMessage(page, "Unavailable");
	assert(message.GetInt32("append", -1) == 0);
}

static void
TestPageIdentityAndFailureRoundTrip()
{
	for (auto source : {PlaylistPageSource::LikedSongs, PlaylistPageSource::Playlist,
		PlaylistPageSource::Album, PlaylistPageSource::Podcast}) {
		auto page = Page(source);
		for (bool ok : {false, true}) {
			page.ok = ok;
			BMessage message = MakePlaylistPageMessage(page, "Unavailable");
			PlaylistPageResult parsed;
			bool read = ReadPlaylistPageHeader(message, parsed);
			assert(read && parsed.ok == ok && parsed.request.id == "content");
			assert(parsed.request.token.generation == 4294967300LL);
			assert(parsed.request.token.requestId == 4294967310LL);
			assert(parsed.request.source == source && parsed.request.offset == 50);
			assert(parsed.request.searchGeneration == 7 && parsed.rows.empty());
			if (ok)
				assert(parsed.total == 100 && parsed.pageCount == 3 && parsed.nextOffset == 53);
		}
	}
	auto page = Page(PlaylistPageSource::Podcast);
	page.request.headRefresh = true;
	BMessage message = MakePlaylistPageMessage(page, "Unavailable");
	PlaylistPageResult parsed;
	bool read = ReadPlaylistPageHeader(message, parsed);
	assert(read && parsed.request.headRefresh && parsed.request.offset == 50);
}

static void
ExpectRejected(const BMessage& message)
{
	auto result = Page(PlaylistPageSource::Playlist);
	bool read = ReadPlaylistPageHeader(message, result);
	assert(!read && result.request.id == "content" && result.total == 100);
	assert(result.request.token.generation == 4294967300LL);
}

static void
TestMissingOrMismatchedIdentity()
{
	BMessage valid = MakePlaylistPageMessage(Page(PlaylistPageSource::Playlist), "Unavailable");
	for (const char* field : {"load_generation", "page_request_id", "page_source",
		"content_id", "offset", "search_generation", "ok", "response_valid", "status",
		"retry_after", "total", "page_count", "next_offset"}) {
		BMessage message(valid);
		message.RemoveName(field);
		ExpectRejected(message);
	}
	BMessage message(valid);
	message.RemoveName("load_generation");
	message.AddInt32("load_generation", 1);
	ExpectRejected(message);
	message = valid;
	message.ReplaceInt64("page_request_id", 0);
	ExpectRejected(message);
	message = valid;
	message.ReplaceInt32("page_source", static_cast<int32>(PlaylistPageSource::Unsupported));
	ExpectRejected(message);
	message = valid;
	message.what = MSG_PLAYLIST_EPISODE_PAGE;
	ExpectRejected(message);
	message = valid;
	message.ReplaceBool("ok", false);
	ExpectRejected(message);
}

static void
TestSearchTimerMessages()
{
	for (bool retry : {false, true}) {
		BMessage message = MakePlaylistSearchMessage(retry, 15);
		assert(message.what == (retry ? 'rEps' : 'aEps'));
		int32_t generation = 42;
		bool read = ReadPlaylistSearchGeneration(message, generation);
		assert(read && generation == 15);
		message.RemoveName("search_generation");
		read = ReadPlaylistSearchGeneration(message, generation);
		assert(!read && generation == 15);
	}
}

static void
TestFailureAndHeadMessages()
{
	auto page = Page(PlaylistPageSource::Podcast);
	page.ok = false;
	page.status = 429;
	page.retryAfter = 12;
	BMessage message = MakePlaylistPageMessage(page, "Unavailable");
	assert(message.what == MSG_PLAYLIST_PAGE_FAILED && message.what == 'pLdF');
	assert(message.GetInt32("search_generation", -1) == 7);
	assert(message.GetInt32("status", -1) == 429 && message.GetInt32("retry_after", -1) == 12);
	assert(message.GetBool("response_valid", false));
	int32 number = 0;
	assert(message.FindInt32("number", &number) != B_OK);
	page.responseValid = false;
	page.request.headRefresh = true;
	message = MakePlaylistPageMessage(page, "Unavailable");
	assert(message.what == MSG_PLAYLIST_PODCAST_HEAD_PAGE && message.what == 'pEpR');
	assert(!message.GetBool("ok", true) && !message.GetBool("response_valid", true));
	assert(message.GetInt32("offset", -1) == 50);
	assert(message.FindInt32("total", &number) != B_OK);
	page.ok = true;
	page.responseValid = true;
	message = MakePlaylistPageMessage(page, "Unavailable");
	assert(message.GetBool("ok", false) && message.GetInt32("next_offset", 0) == 53);
	assert(message.FindInt32("append", &number) != B_OK);
}

int
main()
{
	TestTrackWireCompatibility();
	TestEpisodeWireCompatibility();
	TestFailureAndHeadMessages();
	TestPageIdentityAndFailureRoundTrip();
	TestMissingOrMismatchedIdentity();
	TestSearchTimerMessages();
	std::puts("Playlist page message tests passed.");
}
