#include "playlist/PlaylistMetadataMessages.h"
#include "messages/Messages.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist metadata message tests require assertions; compile without NDEBUG.
#endif

static PlaylistMetadataResult
Result()
{
	PlaylistMetadataResult result;
	result.request = {PlaylistMetadataKind::Playlist, "playlist"};
	result.ok = true;
	result.status = 200;
	result.title = "Title";
	result.coverUrl = "cover";
	result.snapshotId = "snapshot";
	result.description = "Description";
	result.ownerId = "owner";
	result.isPublic = true;
	result.total = 123;
	result.userId = "account";
	result.legacyUserId = "legacy";
	return result;
}

static void
TestTypedRoundTrip()
{
	for (auto kind : {PlaylistMetadataKind::Playlist, PlaylistMetadataKind::Album,
		PlaylistMetadataKind::Podcast, PlaylistMetadataKind::CurrentUser}) {
		auto original = Result();
		original.request.kind = kind;
		BMessage message = MakePlaylistMetadataMessage(original);
		assert(message.what == MSG_PLAYLIST_METADATA_RESULT);
		PlaylistMetadataResult parsed;
		bool read = ReadPlaylistMetadataMessage(message, parsed);
		assert(read && parsed.request.kind == kind && parsed.request.id == "playlist");
		assert(parsed.ok && parsed.responseValid && parsed.status == 200 && parsed.retryAfter == -1);
		assert(parsed.title == "Title" && parsed.coverUrl == "cover");
		assert(parsed.snapshotId == "snapshot" && parsed.description == "Description");
		assert(parsed.ownerId == "owner" && parsed.isPublic && parsed.total == 123);
		assert(parsed.userId == "account" && parsed.legacyUserId == "legacy");
	}
}

static void
TestFailureHasNoSuccessfulPayload()
{
	for (bool valid : {false, true}) {
		auto failure = Result();
		failure.ok = false;
		failure.responseValid = valid;
		failure.status = 429;
		failure.retryAfter = 12;
		BMessage message = MakePlaylistMetadataMessage(failure);
		assert(!message.HasString("title") && !message.HasString("owner_id"));
		PlaylistMetadataResult parsed = Result();
		bool read = ReadPlaylistMetadataMessage(message, parsed);
		assert(read && !parsed.ok && parsed.responseValid == valid);
		assert(parsed.status == 429 && parsed.retryAfter == 12);
		assert(parsed.request.id == "playlist" && parsed.title.empty());
		assert(parsed.ownerId.empty() && !parsed.isPublic && parsed.total == -1);
	}
}

static void
ExpectRejected(const BMessage& message)
{
	auto result = Result();
	bool read = ReadPlaylistMetadataMessage(message, result);
	assert(!read && result.title == "Title" && result.total == 123);
	assert(result.request.kind == PlaylistMetadataKind::Playlist && result.ok);
}

static void
TestMalformedMessages()
{
	BMessage valid = MakePlaylistMetadataMessage(Result());
	for (const char* field : {"metadata_kind", "id", "ok", "response_valid",
		"status", "retry_after", "title", "cover_url", "snapshot_id", "description",
		"owner_id", "public", "total", "user_id", "legacy_user_id"}) {
		BMessage message(valid);
		message.RemoveName(field);
		ExpectRejected(message);
	}
	BMessage message(valid);
	message.what = MSG_PLAYLIST_TRACK_PAGE;
	ExpectRejected(message);
	for (int32 kind : {-1, 0, 99}) {
		message = valid;
		message.ReplaceInt32("metadata_kind", kind);
		ExpectRejected(message);
	}
	message = valid;
	message.RemoveName("public");
	message.AddInt32("public", 1);
	ExpectRejected(message);
	message = valid;
	message.ReplaceBool("response_valid", false);
	ExpectRejected(message);
	message = valid;
	message.RemoveName("total");
	message.AddString("total", "123");
	ExpectRejected(message);
}

static void
TestExistingTitleAndCoverWire()
{
	BMessage title = MakePlaylistTitleMessage("Renamed");
	BMessage cover = MakePlaylistCoverMessage("url");
	assert(title.what == 'uTtl' && cover.what == 'uCov');
	assert(std::string(title.GetString("title", "")) == "Renamed");
	assert(std::string(cover.GetString("url", "")) == "url");
	cover = MakePlaylistCoverMessage("");
	assert(cover.HasString("url") && std::string(cover.GetString("url", "missing")).empty());
}

static void
TestSnapshotResult()
{
	for (bool ok : {false, true}) {
		auto input = Result();
		input.ok = ok;
		BMessage message = MakePlaylistSnapshotMessage(input);
		PlaylistMetadataResult result;
		bool read = ReadPlaylistSnapshotMessage(message, result);
		assert(read && result.ok == ok && result.request.id == input.request.id);
		assert(message.what == MSG_PLAYLIST_SNAPSHOT_RESULT);
		if (ok) assert(result.snapshotId == input.snapshotId && result.total == input.total);
		message.what = MSG_PLAYLIST_METADATA_RESULT;
		read = ReadPlaylistSnapshotMessage(message, result);
		assert(!read);
		for (const char* field : {"metadata_kind", "id", "ok"}) {
			message = MakePlaylistSnapshotMessage(input);
			message.RemoveName(field);
			read = ReadPlaylistSnapshotMessage(message, result);
			assert(!read);
		}
		input.request.kind = PlaylistMetadataKind::Album;
		read = ReadPlaylistSnapshotMessage(MakePlaylistSnapshotMessage(input), result);
		assert(!read);
	}
}

int
main()
{
	TestTypedRoundTrip();
	TestFailureHasNoSuccessfulPayload();
	TestMalformedMessages();
	TestExistingTitleAndCoverWire();
	TestSnapshotResult();
	puts("Playlist metadata message tests passed.");
}
