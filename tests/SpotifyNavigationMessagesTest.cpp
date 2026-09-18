#include "navigation/SpotifyNavigationMessages.h"
#include "Messages.h"

#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error Message tests require assertions; compile without NDEBUG.
#endif

static SpotifyShowResolution
Sample()
{
	return {{"spotify:show:book", "Title", "https://cover", false},
		SpotifyShowResolutionKind::LegacyShowAfterApiFailure, "spotify:show:book", 429, 17};
}

static void
ExpectRejected(const BMessage& message)
{
	auto result = Sample();
	result.status = 999;
	assert(!ReadSpotifyShowResolutionMessage(message, result));
	assert(result.status == 999 && result.request.coverUrl == "https://cover");
}

static void
TestLegacyOpen()
{
	BMessage legacy('open');
	legacy.AddString("uri", "spotify:album:album");
	SpotifyOpenRequest parsed;
	assert(ReadSpotifyOpenMessage(legacy, parsed));
	assert(parsed.title.empty() && parsed.coverUrl.empty() && !parsed.skipAudiobookResolution);
	legacy.AddString("cover_url", "alias");
	assert(ReadSpotifyOpenMessage(legacy, parsed) && parsed.coverUrl == "alias");
	legacy.AddString("coverUrl", "preferred");
	assert(ReadSpotifyOpenMessage(legacy, parsed) && parsed.coverUrl == "preferred");
	legacy.ReplaceString("coverUrl", "");
	assert(ReadSpotifyOpenMessage(legacy, parsed) && parsed.coverUrl == "alias");
	legacy.AddString("title", "Original title");
	legacy.AddBool("skip_audiobook_resolution", true);
	assert(ReadSpotifyOpenMessage(legacy, parsed) && parsed.skipAudiobookResolution);
	assert(parsed.title == "Original title");
	SpotifyOpenRequest copy;
	assert(ReadSpotifyOpenMessage(MakeSpotifyOpenMessage(parsed), copy));
	assert(copy.uri == parsed.uri && copy.coverUrl == "alias" && copy.skipAudiobookResolution);
}

static void
TestMalformedOpen()
{
	for (const char* field : {"uri", "title", "coverUrl", "cover_url", "skip_audiobook_resolution"}) {
		BMessage message = MakeSpotifyOpenMessage(Sample().request);
		message.RemoveName(field);
		message.AddInt32(field, 1);
		SpotifyOpenRequest parsed{"unchanged", "unchanged", "unchanged", true};
		assert(!ReadSpotifyOpenMessage(message, parsed));
		assert(parsed.uri == "unchanged" && parsed.skipAudiobookResolution);
	}
	for (const char* field : {"uri", "title", "coverUrl", "cover_url"}) {
		BMessage message = MakeSpotifyOpenMessage(Sample().request);
		if (std::string(field) == "cover_url")
			message.AddString(field, "first");
		message.AddString(field, "duplicate");
		SpotifyOpenRequest parsed;
		assert(!ReadSpotifyOpenMessage(message, parsed));
	}
	BMessage message = MakeSpotifyOpenMessage(Sample().request);
	message.AddBool("skip_audiobook_resolution", false);
	SpotifyOpenRequest parsed;
	assert(!ReadSpotifyOpenMessage(message, parsed));
	message = MakeSpotifyOpenMessage({});
	assert(!ReadSpotifyOpenMessage(message, parsed));
	message = MakeSpotifyOpenMessage(Sample().request);
	message.what = MSG_PLAY_URI;
	assert(!ReadSpotifyOpenMessage(message, parsed));
}

static void
TestResolutionRoundTrips()
{
	using Kind = SpotifyShowResolutionKind;
	for (Kind kind : {Kind::ApiAudiobook, Kind::SynthesizedAudiobookUri,
			Kind::LegacyShowAfterApiFailure, Kind::LegacyShowAfterMalformedResponse}) {
		auto result = Sample();
		result.kind = kind;
		if (kind == Kind::ApiAudiobook || kind == Kind::SynthesizedAudiobookUri)
			result.resolvedUri = "spotify:audiobook:book";
		SpotifyShowResolution parsed;
		assert(ReadSpotifyShowResolutionMessage(MakeSpotifyShowResolutionMessage(result), parsed));
		assert(parsed.kind == kind && parsed.resolvedUri == result.resolvedUri);
		assert(parsed.status == result.status && parsed.retryAfter == result.retryAfter);
		assert(parsed.request.uri == result.request.uri && parsed.request.title == result.request.title);
		assert(parsed.request.coverUrl == result.request.coverUrl && !parsed.request.skipAudiobookResolution);
	}
}

static void
TestResolutionFields()
{
	BMessage valid = MakeSpotifyShowResolutionMessage(Sample());
	for (const char* field : {"request", "resolution_kind", "resolved_uri", "status", "retry_after"}) {
		BMessage missing(valid);
		assert(missing.RemoveName(field) == B_OK);
		ExpectRejected(missing);
		missing.AddFloat(field, 1.0f);
		ExpectRejected(missing);
		BMessage duplicate(valid);
		if (std::string(field) == "request") {
			BMessage request = MakeSpotifyOpenMessage(Sample().request);
			duplicate.AddMessage(field, &request);
		} else if (std::string(field) == "resolved_uri")
			duplicate.AddString(field, "spotify:show:book");
		else
			duplicate.AddInt32(field, 1);
		ExpectRejected(duplicate);
	}
	valid.what = MSG_OPEN_SPOTIFY_URI;
	ExpectRejected(valid);
}

static void
TestResolutionValues()
{
	for (const char* field : {"status", "retry_after", "resolution_kind"}) {
		BMessage message = MakeSpotifyShowResolutionMessage(Sample());
		message.ReplaceInt32(field, -2);
		ExpectRejected(message);
	}
	BMessage message = MakeSpotifyShowResolutionMessage(Sample());
	message.ReplaceString("resolved_uri", "spotify:audiobook:wrong");
	ExpectRejected(message);
	message = MakeSpotifyShowResolutionMessage(Sample());
	BMessage request = MakeSpotifyOpenMessage({"spotify:track:wrong"});
	message.ReplaceMessage("request", &request);
	ExpectRejected(message);
	request = MakeSpotifyOpenMessage(Sample().request);
	request.ReplaceBool("skip_audiobook_resolution", true);
	message.ReplaceMessage("request", &request);
	ExpectRejected(message);
}

int
main()
{
	TestLegacyOpen();
	TestMalformedOpen();
	TestResolutionRoundTrips();
	TestResolutionFields();
	TestResolutionValues();
	std::cout << "Spotify navigation message tests passed.\n";
}
