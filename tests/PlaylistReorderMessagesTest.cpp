#include "playlist/PlaylistReorderMessages.h"
#include "messages/Messages.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist reorder message tests require assertions; compile without NDEBUG.
#endif

static PlaylistReorderResult
Result(bool ok = true)
{
	PlaylistReorderResult result;
	result.requestId = (int64_t(1) << 40) + 9;
	result.playlistId = "list";
	result.ok = ok;
	result.status = ok ? 200 : 409;
	result.snapshotId = "new";
	return result;
}

static void
ExpectRejected(const BMessage& message)
{
	PlaylistReorderResult output;
	output.playlistId = "untouched";
	output.requestId = 99;
	assert(!ReadPlaylistReorderMessage(message, output));
	assert(output.playlistId == "untouched" && output.requestId == 99);
}

static void
TestRoundTrip()
{
	for (bool ok : {true, false}) {
		auto expected = Result(ok);
		BMessage message = MakePlaylistReorderMessage(expected);
		assert(message.what == MSG_PLAYLIST_REORDER_RESULT);
		PlaylistReorderResult actual;
		assert(ReadPlaylistReorderMessage(message, actual));
		assert(actual.requestId == expected.requestId && actual.playlistId == expected.playlistId);
		assert(actual.ok == ok && actual.status == expected.status);
		assert(actual.snapshotId == (ok ? "new" : ""));
	}
	auto missingSnapshot = Result();
	missingSnapshot.snapshotId.clear();
	PlaylistReorderResult actual;
	assert(ReadPlaylistReorderMessage(MakePlaylistReorderMessage(missingSnapshot), actual));
	assert(actual.ok && actual.snapshotId.empty());
}

static void
TestMalformedMessages()
{
	for (const char* field : {"request_id", "playlist_id", "ok", "status", "snapshot_id"}) {
		BMessage message = MakePlaylistReorderMessage(Result());
		message.RemoveName(field);
		ExpectRejected(message);
		message.AddFloat(field, 1.0f);
		ExpectRejected(message);
	}
	BMessage wrongCode = MakePlaylistReorderMessage(Result());
	wrongCode.what = MSG_PLAYLIST_REMOVAL_RESULT;
	ExpectRejected(wrongCode);
	for (int64_t request : {int64_t(0), int64_t(-1)}) {
		auto invalid = Result();
		invalid.requestId = request;
		ExpectRejected(MakePlaylistReorderMessage(invalid));
	}
	auto invalid = Result();
	invalid.playlistId.clear();
	ExpectRejected(MakePlaylistReorderMessage(invalid));
}

int
main()
{
	TestRoundTrip();
	TestMalformedMessages();
	std::puts("Playlist reorder message tests passed.");
	return 0;
}
