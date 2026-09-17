#include "playlist/PlaylistRemovalMessages.h"
#include "Messages.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist removal message tests require assertions; compile without NDEBUG.
#endif

static PlaylistRemovalResult
Result()
{
	PlaylistRemovalResult result;
	result.requestId = (int64_t(1) << 40) + 7;
	result.playlistId = "playlist";
	result.ok = false;
	result.status = 409;
	result.partialUpdate = true;
	return result;
}

static void
ExpectRejected(const BMessage& message)
{
	PlaylistRemovalResult output;
	output.playlistId = "untouched";
	output.requestId = 99;
	assert(!ReadPlaylistRemovalMessage(message, output));
	assert(output.playlistId == "untouched" && output.requestId == 99);
}

static void
TestRoundTrip()
{
	for (bool ok : {true, false}) {
		auto expected = Result();
		expected.ok = ok;
		expected.partialUpdate = !ok;
		expected.status = ok ? 200 : 409;
		BMessage message = MakePlaylistRemovalMessage(expected);
		assert(message.what == MSG_PLAYLIST_REMOVAL_RESULT);
		PlaylistRemovalResult actual;
		assert(ReadPlaylistRemovalMessage(message, actual));
		assert(actual.playlistId == expected.playlistId);
		assert(actual.requestId == expected.requestId && actual.ok == ok);
		assert(actual.status == expected.status && actual.partialUpdate == !ok);
	}
}

static void
TestMalformedMessages()
{
	for (const char* field : {"request_id", "playlist_id", "ok", "status", "partial_update"}) {
		BMessage missing = MakePlaylistRemovalMessage(Result());
		missing.RemoveName(field);
		ExpectRejected(missing);
		missing.AddFloat(field, 1.0f);
		ExpectRejected(missing);
	}
	BMessage wrongCode = MakePlaylistRemovalMessage(Result());
	wrongCode.what = 0;
	ExpectRejected(wrongCode);
	for (int64_t requestId : {int64_t(0), int64_t(-1)}) {
		auto invalid = Result();
		invalid.requestId = requestId;
		ExpectRejected(MakePlaylistRemovalMessage(invalid));
	}
	auto invalid = Result();
	invalid.playlistId.clear();
	ExpectRejected(MakePlaylistRemovalMessage(invalid));
}

int
main()
{
	TestRoundTrip();
	TestMalformedMessages();
	std::puts("Playlist removal message tests passed.");
	return 0;
}
